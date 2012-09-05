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

#define VNET_CH_PATH_BOOT0	"/dev/umts_boot0"
#define VNET_CH_PATH_IPC0		"/dev/umts_ipc0"

#define IOCTL_MODEM_STATUS		_IO('o', 0x27)

void vnet_start_cp_ramdump()
{
	int ret;
	ret = system("/usr/bin/xmm6262-boot -o u &");
	dbg("system(/usr/bin/xmm6262-boot -o u &) ret[%d]",ret)
}

void vnet_start_cp_reset()
{
	int ret;
	ret = system("/usr/bin/xmm6262-boot &");
	dbg("system(/usr/bin/xmm6262-boot &) ret[%d]",ret)
}

int vnet_get_cp_state( int fd )
{
	enum vnet_cp_state state = VNET_CP_STATE_ONLINE;

	state =  ioctl( fd, IOCTL_MODEM_STATUS );

	switch ( state ) {
	case VNET_CP_STATE_OFFLINE:
		dbg("cp state : offline");
		break;

	case VNET_CP_STATE_CRASH_RESET:
		dbg("cp state : crash_reset");
		break;

	case VNET_CP_STATE_CRASH_EXIT:
		dbg("cp state : crash_exit");
		break;

	case VNET_CP_STATE_BOOTING:
		dbg("cp state : boot");
		break;

	case VNET_CP_STATE_ONLINE:
		dbg("cp state : online");
		break;

	case VNET_CP_STATE_NV_REBUILDING:
		dbg("cp state : nv rebuild");
		break;

	case VNET_CP_STATE_LOADER_DONE:
		dbg("cp state : loader done");
		break;

	default:
		dbg("cp state : unknown state");
		return -1;
	}

	return (int)state;
}

int vnet_ipc0_open()
{
	int state;
	int fd = 0, cnt = 0;

	fd = open ( VNET_CH_PATH_BOOT0, O_RDWR );
	if ( fd < 0 ) {
		dbg("error : open [ %s ] [ %s ]", VNET_CH_PATH_BOOT0, strerror(errno));
		return -1;
	}

	state = vnet_get_cp_state( fd );
	if ( (enum vnet_cp_state)state != VNET_CP_STATE_ONLINE ) {
		return -1;
	} else {
		fd = open ( VNET_CH_PATH_IPC0, O_RDWR );
		if ( fd < 0 ) {
			dbg("error : open [ %s ] [ %s ]", VNET_CH_PATH_IPC0, strerror(errno));
			return -1;
		}
	}
	return fd;
}
