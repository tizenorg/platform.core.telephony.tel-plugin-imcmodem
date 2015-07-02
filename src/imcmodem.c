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
#include <fcntl.h>
#include <sys/ioctl.h>

#include <glib.h>

#include <tcore.h>
#include <server.h>
#include <plugin.h>
#include <storage.h>
#include <user_request.h>
#include <hal.h>

#include "imcmodem.h"
#include "vnet.h"
#include "config.h"

#define IMC_HAL_NAME					"imcmodem"
#define IMC_BUFFER_LEN_MAX				4096

#define IMC_CP_POWER_ON_TIMEOUT			500

#define IMC_MAX_CP_POWER_ON_RETRIES			20

#define IMC_DEVICE_NAME_LEN_MAX			16
#define IMC_DEVICE_NAME_PREFIX			"pdp"

#define VNET_CH_PATH_BOOT0  "/dev/umts_boot0"
#define IOCTL_CG_DATA_SEND  _IO('o', 0x37)

static gboolean _on_recv_ipc_message(GIOChannel *channel, GIOCondition condition, gpointer data);

static guint _register_gio_watch(TcoreHal *hal, int fd, GIOFunc callback);
static void _deregister_gio_watch(guint watch_id);


static guint _register_gio_watch(TcoreHal *hal, int fd, GIOFunc callback)
{
	GIOChannel *channel = NULL;
	guint source;

	if ((fd < 0) || (callback == NULL))
		return 0;

	/* Create Unix Watch channel */
	channel = g_io_channel_unix_new(fd);

	/* Add to Watch list for IO and HUP events */
	source = g_io_add_watch(channel, G_IO_IN | G_IO_HUP, (GIOFunc) callback, hal);
	g_io_channel_unref(channel);
	channel = NULL;

	return source;
}

static void _deregister_gio_watch(guint watch_id)
{
	dbg("[IMCMODEM] Deregister Watch ID: [%d]", watch_id);

	/* Remove source */
	g_source_remove(watch_id);
}

static gboolean _ipc0_init(TcoreHal *hal, ImcmodemVnetChannel *ch, GIOFunc recv_message)
{
	dbg("Entry");

	/* Remove and close the Watch ID and 'fd' if they already exist */
	if (ch->fd >= 0) {
		_deregister_gio_watch(ch->watch_id);
		close(ch->fd);
	}

	/* Open new 'fd' to communicate to CP */
	ch->fd = vnet_ipc0_open();
	if (ch->fd < 0) {
		err("Failed to Open Communiation Channel to CP: [%d]", ch->fd);
		return FALSE;
	}
	dbg("AP-CP Communication channel opened - fd: [%d]", ch->fd);

	/* Register Channel for IO */
	ch->watch_id = _register_gio_watch(hal, ch->fd, recv_message);

	ch->on = TRUE;

	return ch->on;
}

static void _ipc0_deinit(ImcmodemVnetChannel *ch)
{
	/* Remove and close the Watch ID and 'fd' */
	dbg("Watch ID: [%d]", ch->watch_id);
	if (ch->watch_id > 0)
		_deregister_gio_watch(ch->watch_id);

	dbg("fd: [%d]", ch->fd);
	if (ch->fd > 0)
		close(ch->fd);

	ch->watch_id = 0;
	ch->fd = 0;

	ch->on = FALSE;
}

static gboolean _silent_reset(TcoreHal *hal)
{
	dbg("[ERROR] Silent Reset");

	/* Set HAL Poer State to OFF (FALSE) */
	tcore_hal_set_power_state(hal, FALSE);

	/* TODO: Need to handle Silent Reset */

	return FALSE;
}

static gboolean _do_exception_operation(TcoreHal *hal, int fd, GIOCondition cond)
{
	enum vnet_cp_state state = VNET_CP_STATE_UNKNOWN;
	ImcmodemCustomData *custom_data = tcore_hal_ref_user_data(hal);
	dbg("Entry");

	switch (cond) {
	case G_IO_HUP: {
		state = vnet_get_cp_state(fd);
		if (state == VNET_CP_STATE_UNKNOWN) {
			dbg("[ error ] vnet_get_cp_state()");
			break;
		}

		switch (state) {
		case VNET_CP_STATE_CRASH_EXIT: {
			err("CP Crash: Start ramdump");

			_ipc0_deinit(&(custom_data->ipc0));
			vnet_start_cp_ramdump();

			state = VNET_CP_STATE_CRASH_RESET;
		}
		break;

		case VNET_CP_STATE_CRASH_RESET: {
			err("CP Crash Reset");

			_ipc0_deinit(&(custom_data->ipc0));

			if (tcore_hal_get_power_state(hal) == TRUE)  {
				state = VNET_CP_STATE_CRASH_RESET;

				 if (_silent_reset(hal) == FALSE) {
					 err("Silent Reset failed!!!");
					 break;
				 }
			}

			/*
			 * if current hal power state is FALSE, 'cp_reset' mean normal power off
			 * (it's because of kernel concept)
			 */
			state = VNET_CP_STATE_OFFLINE;

		}
		break;

		default:
			err("Unwanted State: [0x%x]", state);
			return TRUE;
		}
	}
	break;

	case G_IO_IN:
	case G_IO_OUT:
	case G_IO_PRI:
	case G_IO_ERR:
	case G_IO_NVAL:
		dbg("Unknown/Undefined problem - condition: [0x%x]", cond);
	break;
	}

	/* Emit receive callback */
	tcore_hal_emit_recv_callback(hal, sizeof(int), &state);
	return TRUE;
}

static gboolean imcmodem_power_on(gpointer data)
{
	ImcmodemCustomData *custom_data;
	TcoreHal *hal;
	gboolean ret;

	static int count = 0;
	dbg("Entry");

	hal = (TcoreHal *)data;

	custom_data = tcore_hal_ref_user_data(hal);
	if (custom_data == NULL) {
		err("HAL Custom data is NULL");
		return TRUE;
	}

	/* Increment the 'count' */
	count++;

	/* Create and Open interface to CP */
	ret = _ipc0_init(hal, &custom_data->ipc0, _on_recv_ipc_message);
	if (ret == FALSE) {
		err("Failed to Create/Open CP interface - Try count: [%d]", count);

		if (count > IMC_MAX_CP_POWER_ON_RETRIES) {
			TcorePlugin *plugin = tcore_hal_ref_plugin(hal);
			Server *server = tcore_plugin_ref_server(plugin);
			struct tnoti_modem_power modem_power;

			err("Maximum timeout reached: [%d]", count);

			modem_power.state = MODEM_STATE_ERROR;

			/* Notify server a modem error occured */
			tcore_server_send_notification(server, NULL,
				TNOTI_MODEM_POWER,
				sizeof(struct tnoti_modem_power), &modem_power);

			tcore_hal_free(hal);
			g_free(custom_data);

			return FALSE;
		}

		return TRUE;
	}
	dbg("Created AP-CP interface");

	/* Set HAL Power State ON (TRUE) */
	tcore_hal_set_power_state(hal, TRUE);
	dbg("HAL Power State: Power ON");

	/* CP is ONLINE, send AT+CPAS */
	config_check_cp_power(hal);

	/* To stop the cycle need to return FALSE */
	return FALSE;
}

static void _on_cmux_channel_close(TcoreHal *hal, gpointer user_data)
{
	ImcmodemCustomData *custom_data;

	if (hal == NULL) {
		err("HAL is NULL");
		return;
	}

	custom_data = tcore_hal_ref_user_data(hal);
	if (custom_data == NULL) {
		err("custom_data is NULL");
		return;
	}

	/* Remove mapping Table */
	tcore_server_remove_cp_mapping_tbl_entry(custom_data->modem, hal);
}

static enum tcore_hook_return imcmodem_on_hal_send(TcoreHal *hal,
		unsigned int data_len, void *data, void *user_data)
{
	msg("\n====== TX data DUMP ======\n");
	tcore_util_hex_dump("          ", data_len, data);
	msg("\n====== TX data DUMP ======\n");

	return TCORE_HOOK_RETURN_CONTINUE;
}

static void imcmodem_on_hal_recv(TcoreHal *hal,
		unsigned int data_len, const void *data, void *user_data)
{
	msg("\n====== RX data DUMP ======\n");
	tcore_util_hex_dump("          ", data_len, data);
	msg("\n====== RX data DUMP ======\n");
}

static gboolean _on_recv_ipc_message(GIOChannel *channel,
							GIOCondition condition, gpointer data)
{
	TcoreHal *hal = data;
	ImcmodemCustomData *custom_data;
	char recv_buffer[IMC_BUFFER_LEN_MAX];
	int recv_len = 0;
	TReturn ret;
	char buf[256];
	const char *str = NULL;

	custom_data = tcore_hal_ref_user_data(hal);
	if (custom_data == NULL) {
		err("custom_data is NULL");
		return TRUE;
	}

	/* If the received input is NOT IO, then we need to handle the exception */
	if (condition != G_IO_IN) {
		err("[ERROR] Not IO input");
		return _do_exception_operation(hal, custom_data->ipc0.fd, condition);
	}

	memset(recv_buffer, 0x0, IMC_BUFFER_LEN_MAX);

	/* Receive data from device */
	recv_len = read(custom_data->ipc0.fd, (guchar *)recv_buffer, IMC_BUFFER_LEN_MAX);
	if (recv_len < 0) {
		str = strerror_r(errno, buf, 256);
		err("[READ] recv_len: [%d] Error: [%s]", recv_len, str);
		return TRUE;
	}

	msg("\n---------- [RECV] Length of received data: [%d] ----------\n", recv_len);

	/* Emit response callback */
	tcore_hal_emit_recv_callback(hal, recv_len, recv_buffer);

	/* Dispatch received data to response handler */
	ret = tcore_hal_dispatch_response_data(hal, 0, recv_len, recv_buffer);
	msg("\n---------- [RECV FINISH] Receive processing: [%d] ----------\n", ret);

	return TRUE;
}

static TReturn imcmodem_hal_send(TcoreHal *hal, unsigned int data_len, void *data)
{
	int ret;
	ImcmodemCustomData *custom_data;

	if (tcore_hal_get_power_state(hal) == FALSE)
		return TCORE_RETURN_FAILURE;

	custom_data = tcore_hal_ref_user_data(hal);
	if (custom_data == NULL) {
		err("custom_data is NULL");
		return TCORE_RETURN_FAILURE;
	}

	dbg("write (fd=%d, len=%d)", custom_data->ipc0.fd, data_len);

	ret = write(custom_data->ipc0.fd, (guchar *) data, data_len);
	if (ret < 0)
		return TCORE_RETURN_FAILURE;

	return TCORE_RETURN_SUCCESS;;
}

static TReturn imcmodem_hal_setup_netif(CoreObject *co,
	TcoreHalSetupNetifCallback func, void *user_data,
	unsigned int cid, gboolean enable)
{
	if (enable == TRUE) {
		int fd;
		char ifname[IMC_DEVICE_NAME_LEN_MAX];
		int ret = -1;
		char buf[256];
		const char *str = NULL;

		dbg("ACTIVATE");

		/* Open device to send IOCTL command */
		fd = open(VNET_CH_PATH_BOOT0, O_RDWR);
		if (fd < 0) {
			str = strerror_r(errno, buf, 256);
			err("Failed to Open [%s] Error: [%s]", VNET_CH_PATH_BOOT0, str);
			return TCORE_RETURN_FAILURE;
		}

		/*
		 * Send IOCTL to change the Channel to Data mode
		 *
		 * Presently only 2 Contexts are suported
		 */
		switch (cid) {
		case 1: {
			dbg("Send IOCTL: arg 0x05 (0101) HSIC1, cid: [%d]", cid);
			ret = ioctl(fd, IOCTL_CG_DATA_SEND, 0x05);
		}
		break;

		case 2: {
			dbg("Send IOCTL: arg 0x0A (1010) HSIC2, cid: [%d]", cid);
			ret = ioctl(fd, IOCTL_CG_DATA_SEND, 0xA);
		}
		break;

		default: {
			err("More than 2 Contexts are not supported right now!!! cid: [%d]", cid);
		}
		}

		/* Close 'fd' */
		close(fd);

		/* TODO - Need to handle Failure case */
		if (ret < 0) {
			err("[ERROR] IOCTL_CG_DATA_SEND - FAIL [0x%x]", IOCTL_CG_DATA_SEND);

			/* Invoke callback function */
			if (func)
				func(co, ret, NULL, user_data);

			return TCORE_RETURN_FAILURE;
		} else {
			dbg("[OK] IOCTL_CG_DATA_SEND - PASS [0x%x]", IOCTL_CG_DATA_SEND);

			/* Device name */
			snprintf(ifname, IMC_DEVICE_NAME_LEN_MAX, "%s%d", IMC_DEVICE_NAME_PREFIX, (cid - 1));
			dbg("Interface Name: [%s]", ifname);

			/* Invoke callback function */
			if (func)
				func(co, ret, ifname, user_data);

			return TCORE_RETURN_SUCCESS;
		}
	} else {
		dbg("DEACTIVATE");
		return TCORE_RETURN_SUCCESS;
	}
}

/* HAL Operations */
static struct tcore_hal_operations hal_ops = {
	.power = NULL,
	.send = imcmodem_hal_send,
	.setup_netif = imcmodem_hal_setup_netif,
};

/* Initialize */
gboolean imcmodem_init(TcorePlugin *plugin)
{
	TcoreHal *hal;
	ImcmodemCustomData *custom_data;

	/* Custom data for Modem Interface Plug-in */
	custom_data = g_try_new0(ImcmodemCustomData, 1);
	if (custom_data == NULL) {
		err("Failed to allocate memory for Custom data");
		return FALSE;
	}
	dbg("Created custom data memory");

	/* Register to Server */
	custom_data->modem = tcore_server_register_modem(tcore_plugin_ref_server(plugin), plugin);
	if (custom_data->modem == NULL)  {
		err("Failed to Register modem");
		g_free(custom_data);
		return FALSE;
	}

	/* Create Physical HAL */
	hal = tcore_hal_new(plugin, IMC_HAL_NAME, &hal_ops, TCORE_HAL_MODE_AT);
	if (hal == NULL) {
		err("Failed to Create Physical HAL");
		g_free(custom_data);
		return FALSE;
	}
	dbg("HAL [0x%x] created", hal);

	/* Set HAL as Modem Interface Plug-in's User data */
	tcore_plugin_link_user_data(plugin, hal);

	/* Link Custom data to HAL's 'user_data' */
	tcore_hal_link_user_data(hal, custom_data);

	/* Add callbacks for Send/Receive Hooks */
	tcore_hal_add_send_hook(hal, imcmodem_on_hal_send, NULL);
	tcore_hal_add_recv_callback(hal, imcmodem_on_hal_recv, NULL);
	dbg("Added Send hook and Receive callback");

	/* Set HAL state to Power OFF (FALSE) */
	tcore_hal_set_power_state(hal, FALSE);
	dbg("HAL Power State: Power OFF");

	/* Check CP Power ON */
	g_timeout_add_full(G_PRIORITY_HIGH, IMC_CP_POWER_ON_TIMEOUT, imcmodem_power_on, hal, 0);

	return TRUE;
}

/* De-initialize */
void imcmodem_deinit(TcorePlugin *plugin)
{
	Server *s;
	TcoreHal *hal;
	ImcmodemCustomData *custom_data;

	s = tcore_plugin_ref_server(plugin);

	/* Unload Modem Plug-in */
	tcore_server_unload_modem_plugin(s, plugin);
	dbg("Unloaded modem plug-in");

	/* HAL cleanup */
	hal = tcore_plugin_ref_user_data(plugin);
	if (hal == NULL) {
		err("HAL is NULL");
		return;
	}

	custom_data = tcore_hal_ref_user_data(hal);
	if (custom_data != NULL) {
		/* Unregister Modem Interface Plug-in from Server */
		tcore_server_unregister_modem(s, custom_data->modem);
		dbg("Unregistered from Server");

		/* Deinitialize the Physical Channel */
		_ipc0_deinit(&custom_data->ipc0);
		dbg("Deinitialized the Channel");

		/* Free custom data */
		g_free(custom_data);
	}

	/* Close CMUX and CMUX channels */
	tcore_cmux_close(hal, _on_cmux_channel_close, NULL);
	dbg("CMUX is closed");


	/* Free HAL */
	tcore_hal_free(hal);
	dbg("Freed HAL");
}
