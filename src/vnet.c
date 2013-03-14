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
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <linux/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <net/if.h>

#include <glib.h>

#include <log.h>

#include "vnet.h"

#ifndef __USE_GNU
#define __USE_GNU
#endif

#define MODEM_IMAGE_PATH		"/boot/modem.bin"
#define NV_DIR_PATH				"/csa/nv"
#define NV_FILE_PATH			NV_DIR_PATH"/nvdata.bin"

/*
 * AP-CP comunication devices
 */
/* To track CP bootup */
#define VNET_CH_PATH_BOOT0		"/dev/umts_boot0"

/* Control communication channel */
#define VNET_CH_PATH_IPC0		"/dev/umts_ipc0"

#define IOCTL_MODEM_STATUS		_IO('o', 0x27)

void vnet_start_cp_ramdump()
{
	int ret;
	ret = system("/usr/bin/xmm6262-boot -o u &");
	dbg("system(/usr/bin/xmm6262-boot -o u &) ret[%d]", ret)
}

void vnet_start_cp_reset()
{
	int ret;
	ret = system("/usr/bin/xmm6262-boot &");
	dbg("system(/usr/bin/xmm6262-boot &) ret[%d]", ret)
}

enum vnet_cp_state vnet_get_cp_state(int fd)
{
	enum vnet_cp_state state = VNET_CP_STATE_UNKNOWN;
	dbg("Entry");

	/* Get CP state */
	state = ioctl(fd, IOCTL_MODEM_STATUS);

	switch (state) {
	case VNET_CP_STATE_OFFLINE:
		dbg("CP State: OFFLINE");
		break;

	case VNET_CP_STATE_CRASH_RESET:
		dbg("CP State: CRASH RESET");
		break;

	case VNET_CP_STATE_CRASH_EXIT:
		dbg("CP State: CRASH EXIT");
		break;

	case VNET_CP_STATE_BOOTING:
		dbg("CP State: BOOT");
		break;

	case VNET_CP_STATE_ONLINE:
		dbg("CP State: ONLINE");
		break;

	case VNET_CP_STATE_NV_REBUILDING:
		dbg("CP State: NV REBUILD");
		break;

	case VNET_CP_STATE_LOADER_DONE:
		dbg("CP State: LOADER DONE");
		break;

	case VNET_CP_STATE_UNKNOWN:
	default:
		dbg("CP State: UNKNOWN State - [%d]", state);
		break;
	}

	return state;
}

int vnet_ipc0_open()
{
	enum vnet_cp_state state;
	int fd;
	dbg("Entry");

	/* Opening device to track CP state */
	fd = open(VNET_CH_PATH_BOOT0, O_RDWR);
	if (fd < 0) {
		err("Failed to Open [%s] Error: [%s]", VNET_CH_PATH_BOOT0, strerror(errno));
		return -1;
	}

	/* Track the state of CP */
	state = vnet_get_cp_state(fd);
	dbg("CP State: [%d]", state);
	if (state != VNET_CP_STATE_ONLINE) {
		err("CP is NOT yet Online!!!");
		return -1;
	} else {
		/* Opening AP-CP Control communication device */
		fd = open(VNET_CH_PATH_IPC0, O_RDWR);
		if (fd < 0) {
			err("Failed to Open [%s] Error: [%s]", VNET_CH_PATH_IPC0, strerror(errno));
			return -1;
		}
	}

	return fd;
}
