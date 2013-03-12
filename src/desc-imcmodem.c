/*
 * tel-plugin-imcmodem
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: Kyoungyoup Park <gynaru.park@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/utsname.h>

#include <glib.h>

#include <tcore.h>
#include <server.h>
#include <plugin.h>
#include <storage.h>
#include <user_request.h>
#include <hal.h>

#include "vnet.h"

struct vnet_channel {
	int fd;
	guint watch_id;
	gboolean on;
};

struct custom_data {
	struct vnet_channel ipc0;
};

typedef gboolean(*cb_func)(GIOChannel *channel, GIOCondition condition, gpointer data);

static gboolean on_recv_ipc_message(GIOChannel *channel, GIOCondition condition, gpointer data);

static guint register_gio_watch(TcoreHal *p, int fd, void *callback);

static TReturn hal_power(TcoreHal *h, gboolean flag);

static gboolean _ipc0_init( TcoreHal *h, struct vnet_channel *ch, cb_func recv_message )
{
	if ( !ch->fd ) {
		g_source_remove( ch->watch_id );
		close( ch->fd );
	}

	ch->fd = vnet_ipc0_open();
	if ( ch->fd < 0 ) {
		dbg("[ error ] vnet_ipc0_open()");
		return FALSE;
	}

	ch->watch_id = register_gio_watch(h, ch->fd, recv_message);

	ch->on = TRUE;

	return ch->on;
}

static void _ipc0_deinit( struct vnet_channel *ch )
{
	g_source_remove( ch->watch_id );
	close( ch->fd );

	ch->watch_id = 0;
	ch->fd = 0;
	ch->on = FALSE;
}


static gboolean _silent_reset( TcoreHal *h )
{
	dbg("[ error ] cp crash : strat silent reset");
	tcore_hal_set_power_state(h, FALSE);

	dbg("[ error ] do nothing.");
#if 0 // this is not applicable code, now silent reset is not guaranteed ( 2012, 04, 19 )

	vnet_start_cp_reset();

	if ( !hal_power( h, TRUE ) ) {
		dbg("[ check ] cp crash : silent reset done");
		return TRUE;
	}

#endif

	return FALSE;
}

static gboolean _do_exception_operation( TcoreHal *h, int fd, GIOCondition con )
{
	int state = -1;
	struct custom_data *user_data = tcore_hal_ref_user_data( h );

	dbg("entrance");

	switch ( con ) {
	case G_IO_HUP: {
		state = vnet_get_cp_state( fd );
		if ( state < 0 ) {
			dbg("[ error ] vnet_get_cp_state()");
			break;
		}

		switch ( (enum vnet_cp_state)state ) {
			case VNET_CP_STATE_CRASH_EXIT: {
				dbg("[ error ] cp crash : strat ramdump");
				_ipc0_deinit( &user_data->ipc0 );
				vnet_start_cp_ramdump();

				state = VNET_CP_STATE_CRASH_RESET;


			} break;

			case VNET_CP_STATE_CRASH_RESET: {

				_ipc0_deinit( &user_data->ipc0 );

				if (tcore_hal_get_power_state( h ))  {

					state = VNET_CP_STATE_CRASH_RESET;

					 if ( !_silent_reset( h ) ) {
						 dbg("[ error ] _silent_reset()");
						 break;
					 }
				}

				/*
				* if current hal power state is FALSE, 'cp_reset' mean normal power off
				* ( it's because of kernel concept )
				*/
				state = VNET_CP_STATE_OFFLINE;

			} break;

			default:
				dbg("useless state, state : (0x%x)", state);
				return TRUE;
		}

	} break;

	case G_IO_IN:
	case G_IO_OUT:
	case G_IO_PRI:
	case G_IO_ERR:
	case G_IO_NVAL:
		dbg("[ error ] unknown problem, con : (0x%x)", con);
		break;

	}

	tcore_hal_emit_recv_callback(h, sizeof(int), &state);

	dbg("done");

	return TRUE;
}

static gboolean _power_on( gpointer data )
{
	struct custom_data *user_data = 0;
	TcoreHal *h = (TcoreHal*)data;
	gboolean ret = 0;
	static int count = 0;

	user_data = tcore_hal_ref_user_data(h);
	if (!user_data) {
		dbg("[ error ] tcore_hal_ref_user_data()");
		return TRUE;
	}

	count++;

	ret = _ipc0_init( h, &user_data->ipc0, on_recv_ipc_message );
	if ( !ret ) {
		dbg("[ error ] _ipc0_init()");

		if ( count > 20 ) {
			dbg("[ error ] _ipc0_init() timeout");
			return FALSE;
		}

		return TRUE;
	}

	tcore_hal_set_power_state(h, TRUE);

	return FALSE;
}

static TReturn hal_power(TcoreHal *h, gboolean flag)
{
	if (flag == FALSE) {
		/* power off not support */
		return TCORE_RETURN_FAILURE;
	}

	g_timeout_add_full( G_PRIORITY_HIGH, 500, _power_on, h, 0 );

	return TCORE_RETURN_SUCCESS;
}

static TReturn hal_send(TcoreHal *h, unsigned int data_len, void *data)
{
	int ret;
	struct custom_data *user_data;

	if (tcore_hal_get_power_state(h) == FALSE)
		return TCORE_RETURN_FAILURE;

	user_data = tcore_hal_ref_user_data(h);
	if (!user_data)
		return TCORE_RETURN_FAILURE;

	dbg("write (fd=%d, len=%d)", user_data->ipc0.fd, data_len);

	ret = write( user_data->ipc0.fd, (guchar *) data, data_len );
	if (ret < 0)
		return TCORE_RETURN_FAILURE;

	return TCORE_RETURN_SUCCESS;;
}

static struct tcore_hal_operations hops =
{
	.power = hal_power,
	.send = hal_send,
};

static gboolean on_recv_ipc_message(GIOChannel *channel, GIOCondition condition, gpointer data)
{
	TcoreHal *h = data;
	struct custom_data *custom;

	#define BUF_LEN_MAX 4096
	char buf[BUF_LEN_MAX];
	int n = 0;

	custom = tcore_hal_ref_user_data(h);

	if ( condition != G_IO_IN ) {
		dbg("[ error ] svnet has a problem");
		return _do_exception_operation( h, custom->ipc0.fd, condition );
	}

	memset(buf, 0, BUF_LEN_MAX);

	n = read(custom->ipc0.fd, (guchar *) buf, BUF_LEN_MAX);
	if (n < 0) {
		err("read error. return_valute = %d", n);
		return TRUE;
	}

	msg("--[RECV]-------------------------");
	dbg("recv (len = %d)", n);

	tcore_hal_emit_recv_callback(h, n, buf);
	msg("--[RECV FINISH]------------------\n");

	/* Notice: Mode of any HAL is TCORE_HAL_MODE_AT as default. */
	tcore_hal_dispatch_response_data(h, 0, n, buf);

	return TRUE;
}

static guint register_gio_watch(TcoreHal *h, int fd, void *callback)
{
	GIOChannel *channel = NULL;
	guint source;

	if (fd < 0 || !callback)
		return 0;

	channel = g_io_channel_unix_new(fd);
	source = g_io_add_watch(channel, G_IO_IN | G_IO_HUP, (GIOFunc) callback, h);
	g_io_channel_unref(channel);
	channel = NULL;

	return source;
}

static gboolean on_load()
{
	struct utsname u;
	char *vnet_models[] = { "SMDK4410", "SMDK4212", "TRATS2", "SLP_PQ_LTE", "SLP_NAPLES", "REDWOOD", "TRATS", NULL };
	int i = 0;

	memset(&u, 0, sizeof(struct utsname));
	uname(&u);

	dbg("u.nodename: [%s]", u.nodename);

	for (i = 0; vnet_models[i]; i++ ) {
		if (!g_strcmp0(u.nodename, vnet_models[i])) {
			return TRUE;
		}
	}

	/*
	 * Not supported model
	 *  - unload this plugin.
	 */

	return FALSE;
}

static gboolean on_init(TcorePlugin *p)
{
	TcoreHal *h;
	struct custom_data *data;

	if (!p)
		return FALSE;

	dbg("i'm init!");

	data = calloc(sizeof(struct custom_data), 1);
	memset(data, 0, sizeof(struct custom_data));

	
	/*
	 * HAL init
	 */
	h = tcore_hal_new(p, "6262", &hops, TCORE_HAL_MODE_AT);

	tcore_hal_set_power_state(h, FALSE);
	tcore_hal_link_user_data(h, data);

	return TRUE;
}

static void on_unload(TcorePlugin *p)
{
	if (!p)
		return;

	dbg("i'm unload");
}

struct tcore_plugin_define_desc plugin_define_desc =
{
	.name = "imcmodem",
	.priority = TCORE_PLUGIN_PRIORITY_HIGH,
	.version = 1,
	.load = on_load,
	.init = on_init,
	.unload = on_unload
};
