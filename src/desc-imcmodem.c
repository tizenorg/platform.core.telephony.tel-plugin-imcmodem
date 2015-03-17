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

#include <glib.h>

#include <tcore.h>
#include <server.h>
#include <plugin.h>

#include "imcmodem.h"

static gboolean on_load()
{
	dbg("Load!!!");

	return TRUE;
}

static gboolean on_init(TcorePlugin *plugin)
{
	dbg("Init!!!");

	if (plugin == NULL) {
		err("'plugin' is NULL");
		return FALSE;
	}

	return imcmodem_init(plugin);
}

static void on_unload(TcorePlugin *plugin)
{
	dbg("Unload!!!");

	if (plugin == NULL) {
		err("Modem Interface Plug-in is NULL");
		return;
	}

	imcmodem_deinit(plugin);
	dbg("Unloaded MODEM Interface Plug-in");
}

/* Modem Interface Plug-in descriptor - IMC modem */
struct tcore_plugin_define_desc plugin_define_desc = {
	.name = "imcmodem",
	.priority = TCORE_PLUGIN_PRIORITY_HIGH,
	.version = 1,
	.load = on_load,
	.init = on_init,
	.unload = on_unload
};
