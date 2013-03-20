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

#include "vnet.h"
#include "config.h"

#define IMC_HAL_NAME						"imcmodem"
#define IMC_BUFFER_LEN_MAX					4096

#define IMC_CP_POWER_ON_TIMEOUT				500

#define IMC_MAX_CP_POWER_ON_RETRIES			20

#define IMC_DEVICE_NAME_LEN_MAX				16
#define IMC_DEVICE_NAME_PREFIX				"pdp"

#define VNET_CH_PATH_BOOT0  "/dev/umts_boot0"
#define IOCTL_CG_DATA_SEND  _IO('o', 0x37)

struct vnet_channel {
	int fd;
	guint watch_id;
	gboolean on;
};

struct custom_data {
	struct vnet_channel ipc0;
};

typedef gboolean(*cb_func)(GIOChannel *channel, GIOCondition condition, gpointer data);

static gboolean _on_recv_ipc_message(GIOChannel *channel, GIOCondition condition, gpointer data);

static guint _register_gio_watch(TcoreHal *plugin, int fd, void *callback);


/* Utility function to dump the Input/Output bytes (TX/RX Data) */
static void _util_hex_dump(char *pad, int size, const void *data)
{
	char buffer[255] = {0, };
	char hex[4] = {0, };
	int i;
	unsigned char *ptr;

	if (size <= 0) {
		msg("[%s] NO data", pad);
		return;
	}

	ptr = (unsigned char *)data;

	snprintf(buffer, 255, "%s%04X: ", pad, 0);
	for (i = 0; i < size; i++) {
		snprintf(hex, 4, "%02X ", ptr[i]);
		strcat(buffer, hex);

		if ((i + 1) % 8 == 0) {
			if ((i + 1) % 16 == 0) {
				msg("%s", buffer);
				memset(buffer, 0, 255);
				snprintf(buffer, 255, "%s%04X: ", pad, i + 1);
			} else {
				strcat(buffer, "  ");
			}
		}
	}

	msg("%s", buffer);
}

static guint _register_gio_watch(TcoreHal *hal, int fd, void *callback)
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

static gboolean _ipc0_init(TcoreHal *hal, struct vnet_channel *ch, cb_func recv_message)
{
	dbg("Entry");

	/* Remove and close the Watch ID and 'fd' if they already exist */
	if (ch->fd >= 0) {
		g_source_remove(ch->watch_id);
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

static void _ipc0_deinit(struct vnet_channel *ch)
{
	g_source_remove(ch->watch_id);
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
	struct custom_data *user_data = tcore_hal_ref_user_data(hal);
	dbg("Entry");

	switch (cond) {
	case G_IO_HUP: {
		state = vnet_get_cp_state(fd);
		if (state == VNET_CP_STATE_UNKNOWN) {
			dbg("[ error ] vnet_get_cp_state()");
			break;
		}

		switch (state) {
		case VNET_CP_STATE_CRASH_EXIT:
		{
			err("CP Crash: Start ramdump");

			_ipc0_deinit(&user_data->ipc0);
			vnet_start_cp_ramdump();

			state = VNET_CP_STATE_CRASH_RESET;
		}
		break;

		case VNET_CP_STATE_CRASH_RESET:
		{
			err("CP Crash Reset");

			_ipc0_deinit(&user_data->ipc0);

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

static gboolean _power_on(gpointer data)
{
	struct custom_data *user_data;
	TcoreHal *hal;
	gboolean ret;

	static int count = 0;
	dbg("Entry");

	hal = (TcoreHal*)data;

	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL) {
		err("HAL Custom data is NULL");
		return TRUE;
	}

	/* Increment the 'count' */
	count++;

	/* Create and Open interface to CP */
	ret = _ipc0_init(hal, &user_data->ipc0, _on_recv_ipc_message);
	if (ret == FALSE) {
		err("Failed to Create/Open CP interface - Try count: [%d]", count);

		if (count > IMC_MAX_CP_POWER_ON_RETRIES) {
			err("Maximum timeout reached: [%d]", count);
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

static enum tcore_hook_return _on_hal_send(TcoreHal *hal,
		unsigned int data_len, void *data, void *user_data)
{
	msg("\n====== TX data DUMP ======\n");
	_util_hex_dump("          ", data_len, data);
	msg("\n====== TX data DUMP ======\n");

	return TCORE_HOOK_RETURN_CONTINUE;
}

static void _on_hal_recv(TcoreHal *hal,
		unsigned int data_len, const void *data, void *user_data)
{
	msg("\n====== RX data DUMP ======\n");
	_util_hex_dump("          ", data_len, data);
	msg("\n====== RX data DUMP ======\n");
}

static gboolean _on_recv_ipc_message(GIOChannel *channel,
							GIOCondition condition, gpointer data)
{
	TcoreHal *hal = data;
	struct custom_data *custom;
	char recv_buffer[IMC_BUFFER_LEN_MAX];
	int recv_len = 0;
	TReturn ret;

	custom = tcore_hal_ref_user_data(hal);

	/* If the received input is NOT IO, then we need to handle the exception */
	if (condition != G_IO_IN) {
		err("[ERROR] Not IO input");
		return _do_exception_operation(hal, custom->ipc0.fd, condition);
	}

	memset(recv_buffer, 0x0, IMC_BUFFER_LEN_MAX);

	/* Receive data from device */
	recv_len = read(custom->ipc0.fd, (guchar *)recv_buffer, IMC_BUFFER_LEN_MAX);
	if (recv_len < 0) {
		err("[READ] recv_len: [%d] Error: [%s]", recv_len,  strerror(errno));
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

static TReturn _hal_send(TcoreHal *hal, unsigned int data_len, void *data)
{
	int ret;
	struct custom_data *user_data;

	if (tcore_hal_get_power_state(hal) == FALSE)
		return TCORE_RETURN_FAILURE;

	user_data = tcore_hal_ref_user_data(hal);
	if (!user_data)
		return TCORE_RETURN_FAILURE;

	dbg("write (fd=%d, len=%d)", user_data->ipc0.fd, data_len);

	ret = write(user_data->ipc0.fd, (guchar *) data, data_len);
	if (ret < 0)
		return TCORE_RETURN_FAILURE;

	return TCORE_RETURN_SUCCESS;;
}

static TReturn _hal_setup_netif(CoreObject *co,
				TcoreHalSetupNetifCallback func,
				void *user_data, unsigned int cid,
				gboolean enable)
{
	if (enable == TRUE) {
		int fd;
		char ifname[IMC_DEVICE_NAME_LEN_MAX];
		int ret = -1;

		dbg("ACTIVATE");

		/* Open device to send IOCTL command */
		fd = open(VNET_CH_PATH_BOOT0, O_RDWR);
		if (fd < 0) {
			err("Failed to Open [%s] Error: [%s]", VNET_CH_PATH_BOOT0, strerror(errno));
			return TCORE_RETURN_FAILURE;
		}

		/*
		 * Send IOCTL to change the Channel to Data mode
		 *
		 * Presently only 2 Contexts are suported
		 */
		switch (cid) {
		case 1:
		{
			dbg("Send IOCTL: arg 0x05 (0101) HSIC1, cid: [%d]", cid);
			ret = ioctl(fd, IOCTL_CG_DATA_SEND, 0x05);
		}
		break;

		case 2:
		{
			dbg("Send IOCTL: arg 0x0A (1010) HSIC2, cid: [%d]", cid);
			ret = ioctl(fd, IOCTL_CG_DATA_SEND, 0xA);
		}
		break;

		default:
		{
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
	.send = _hal_send,
	.setup_netif = _hal_setup_netif,
};

static gboolean on_load()
{
	dbg("Load!!!");

	return TRUE;
}

static gboolean on_init(TcorePlugin *plugin)
{
	TcoreHal *hal;
	struct custom_data *data;
	dbg("Init!!!");

	if (plugin == NULL) {
		err("'plugin' is NULL");
		return FALSE;
	}

#if 1	/* TODO - Need to remove this */
	/*
	 * CP is NOT coming to ONLINE state,
	 * but when it is forceffuly reset using the command -
	 *			xmm6262-boot
	 * it comes back to ONLINE state.
	 *
	 * We need to look into this aspect
	 */
	dbg("====== TRIGGERING CP RESET ======");
	vnet_start_cp_reset();
	dbg("====== CP RESET TRIGGERED ======");
	sleep(2);
#endif	/* TODO - Need to remove this */

	/* Custom data for Modem Interface Plug-in */
	data = g_try_new0(struct custom_data, 1);
	if (data == NULL) {
		err("Failed to allocate memory for Custom data");
		return FALSE;
	}
	dbg("Created custom data memory");

	/* Create Physical HAL */
	hal = tcore_hal_new(plugin, IMC_HAL_NAME, &hal_ops, TCORE_HAL_MODE_AT);
	if (hal == NULL) {
		err("Failed to Create Physical HAL");
		g_free(data);
		return FALSE;
	}
	dbg("HAL [0x%x] created", hal);

	/* Link Custom data to HAL's 'user_data' */
	tcore_hal_link_user_data(hal, data);

	/* Add callbacks for Send/Receive Hooks */
	tcore_hal_add_send_hook(hal, _on_hal_send, NULL);
	tcore_hal_add_recv_callback(hal, _on_hal_recv, NULL);
	dbg("Added Send hook and Receive callback");

	/* Set HAL state to Power OFF (FALSE) */
	tcore_hal_set_power_state(hal, FALSE);
	dbg("HAL Power State: Power OFF");

	/* Resgister to Server */
	tcore_server_register_modem(tcore_plugin_ref_server(plugin), plugin);

	/* Check CP Power ON */
	g_timeout_add_full(G_PRIORITY_HIGH, IMC_CP_POWER_ON_TIMEOUT, _power_on, hal, 0);

	return TRUE;
}

static void on_unload(TcorePlugin *plugin)
{
	dbg("Unload!!!");

	if (plugin == NULL)
		return;

	/* Need to Unload Modem Plugin */
}

/* Modem Interface Plug-in descriptor */
struct tcore_plugin_define_desc plugin_define_desc = {
	.name = "imcmodem",
	.priority = TCORE_PLUGIN_PRIORITY_HIGH,
	.version = 1,
	.load = on_load,
	.init = on_init,
	.unload = on_unload
};
