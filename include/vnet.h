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


#ifndef _VNET_H_
#define _VNET_H_


#ifdef __cplusplus
extern "C" {
#endif

enum vnet_cp_state {
	VNET_CP_STATE_OFFLINE,
	VNET_CP_STATE_CRASH_RESET,
	VNET_CP_STATE_CRASH_EXIT,
	VNET_CP_STATE_BOOTING,
	VNET_CP_STATE_ONLINE,
	VNET_CP_STATE_NV_REBUILDING,
	VNET_CP_STATE_LOADER_DONE,
};

void	vnet_start_cp_ramdump( void );
void	vnet_start_cp_reset( void );
int		vnet_get_cp_state( int fd );

int		vnet_rfs0_open( void );
int		vnet_ipc0_open( void );



#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif

