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

#ifndef __IMCMODEM_H__
#define __IMCMODEM_H__

typedef struct vnet_channel {
	int fd;
	guint watch_id;
	gboolean on;
} ImcmodemVnetChannel;

typedef struct custom_data  {
	TcoreModem *modem;
	ImcmodemVnetChannel ipc0;
} ImcmodemCustomData;

/* Initialize */
gboolean imcmodem_init(TcorePlugin *plugin);

/* De-initialize */
void imcmodem_deinit(TcorePlugin *plugin);

#endif  /* __IMCMODEM_H__ */
