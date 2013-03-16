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

#include <glib.h>

#include <tcore.h>
#include <server.h>
#include <plugin.h>
#include <storage.h>
#include <user_request.h>
#include <core_object.h>
#include <hal.h>
#include <at.h>
#include <mux.h>

#include "config.h"

#define IMC_MODEM_PLUGIN_NAME				"imc-plugin.so"

#define IMC_CMUX_MAX_CHANNELS				7
#define IMC_CMUX_MAX_BUFFER_SIZE			1024

/* CP States */
#define IMC_AT_CPAS_RESULT_READY			0
#define IMC_AT_CPAS_RESULT_UNAVAIL			1
#define IMC_AT_CPAS_RESULT_UNKNOWN			2
#define IMC_AT_CPAS_RESULT_RINGING			3
#define IMC_AT_CPAS_RESULT_CALL_PROGRESS	4
#define IMC_AT_CPAS_RESULT_ASLEEP			5

/* Maximum Core objects per Logical HAL (indirectly per Channel) */
#define MAX_CORE_OBJECTS_PER_CHANNEL		3

/*
 * List of supported Core Object types
 *
 * CMUX Channels [0-7] -
 * Channel 0 - Control Channel for CMUX
 * Channel 1 - CALL & SS
 * Channel 2 - SIM & PHONEBOOK
 * Channel 3 - SAT & SAP
 * Channel 4 - SMS
 * Channel 5 - PS
 * Channel 6 - NETWORK & GPS
 * Channel 7 - MODEM & PS
 */
unsigned int
	supported_modules[IMC_CMUX_MAX_CHANNELS+1][MAX_CORE_OBJECTS_PER_CHANNEL] =
{
	/*
	 * Channel 0 - CMUX Control Channel
	 * No Core Objects would be assigned to this channel
	 */
	{0, 0, 0},
	/* Channel 1 */
	{CORE_OBJECT_TYPE_CALL, CORE_OBJECT_TYPE_SS, 0},
	/* Channel 2 */
	{CORE_OBJECT_TYPE_SIM, CORE_OBJECT_TYPE_PHONEBOOK, 0},
	/* Channel 3 */
	{CORE_OBJECT_TYPE_SAT, CORE_OBJECT_TYPE_SAP, 0},
	/* Channel 4 */
	{CORE_OBJECT_TYPE_SMS, 0, 0},
	/* Channel 5 */
	{CORE_OBJECT_TYPE_PS, 0, 0},
	/* Channel 6 */
	{CORE_OBJECT_TYPE_NETWORK, CORE_OBJECT_TYPE_GPS, 0},
	/* Channel 7 */
	{CORE_OBJECT_TYPE_MODEM, 0, 0},
};

static gboolean _check_cp_poweron(TcoreHal *hal);
static void _send_enable_logging_command(TcoreHal *hal);

static void _on_confirmation_send_message(TcorePending *pending,
									gboolean result, void *user_data)
{
	dbg("Message send confirmation");

	if (result == FALSE) {		/* Fail */
		dbg("SEND FAIL");
	} else {
		dbg("SEND OK");
	}
}
static void _assign_objects_to_hal(int channel_id, TcoreHal *hal)
{
	TcorePlugin *plugin;
	gboolean ret;
	int i;

	plugin = tcore_hal_ref_plugin(hal);

	for (i = 0 ; i < MAX_CORE_OBJECTS_PER_CHANNEL ; i++) {
		if (supported_modules[channel_id][i] == 0)
			continue;

		/* Add Core Object type for specific 'hal' */
		ret = tcore_server_add_cp_mapping_tbl_entry(plugin,
				supported_modules[channel_id][i], hal);
		if (ret == TRUE) {
			dbg("Core Object Type: [0x%x] - Success");
		} else {
			err("Core Object Type: [0x%x] - Fail");
		}
	}
}

static void _on_cmux_setup_complete(gpointer user_data)
{
	TcoreHal *hal = user_data;
	TcorePlugin *plugin;
	dbg("MUX Setup - COMPLETE");

	if (user_data == NULL)
		return;

	plugin = tcore_hal_ref_plugin(hal);

	/* Print all the HALs and Core Objects for the Plug-in */
	tcore_server_print_modems(plugin);

	/* Load Modem Plug-in */
	tcore_server_load_modem_plugin(tcore_plugin_ref_server(plugin),
							plugin, IMC_MODEM_PLUGIN_NAME);
}

static void _on_cmux_channel_setup(int channel_id, TcoreHal *hal,
									gpointer user_data)
{
	TcorePlugin *plugin;
	TcoreHal *phy_hal;
	if ((hal == NULL) || (user_data == NULL))
		return;

	if ((channel_id == 0)
			|| (channel_id > IMC_CMUX_MAX_CHANNELS)) {
		err("Control Channel");
		return;
	}

	phy_hal = user_data;
	plugin = tcore_hal_ref_plugin(hal);

	dbg("Channel ID: [%d] Logical HAL: [0x%x]", channel_id, hal);

	/* Assign specifc Core Object types to the Logical HAL (CMUX Channel) */
	_assign_objects_to_hal(channel_id, hal);

	/* Set HAL state to Power ON (TRUE) */
	tcore_hal_set_power_state(hal, TRUE);
	dbg("HAL Power State: Power ON")
}

static void _on_response_cmux_init(TcorePending *p, int data_len,
										const void *data, void *user_data)
{
	const TcoreATResponse *resp = data;
	TcoreHal *hal = user_data;
	TReturn ret;

	if ((resp != NULL)
			&& resp->success) {
		dbg("Initialize CMUX - [OK]");

		/* Setup Internal CMUX */
		ret = tcore_cmux_setup_internal_mux(CMUX_MODE_BASIC,
							IMC_CMUX_MAX_CHANNELS,
							IMC_CMUX_MAX_BUFFER_SIZE, hal,
							_on_cmux_channel_setup, hal,
							_on_cmux_setup_complete, hal);
	} else {
		err("Initialize CMUX - [NOK]");
	}
}

static void _on_response_enable_logging(TcorePending *p,
					int data_len, const void *data, void *user_data)
{
	const TcoreATResponse *resp = data;
	TcoreHal *hal = user_data;
	TReturn ret;

	if ((resp != NULL)
			&& resp->success) {
		dbg("Enable CP logging - [OK]");
	} else {
		err("Enable CP logging - [NOK]");
	}

	/* Initialize Internal MUX (CMUX) */
	ret = tcore_cmux_init(hal, 0, _on_response_cmux_init, hal);
	if (ret != TCORE_RETURN_SUCCESS) {
		err("Failed to initialize CMUX - Error: [0x%x]", ret);
	} else {
		dbg("Successfully sent CMUX init to CP");
	}
}

static void _on_timeout_check_cp_poweron(TcorePending *p, void *user_data)
{
	TcoreHal *hal = user_data;
	unsigned int data_len = 0;
	char *data = "AT+CPAS";

	data_len = sizeof(data);

	dbg("Resending Command: [%s] Command Length: [%d]", data, data_len);

	/*
	 * Retransmit 1st AT command (AT+CPAS) directly via HAL without disturbing
	 * pending queue.
	 * HAL was passed as user_data, re-using it
	 */
	tcore_hal_send_data(hal, data_len, (void *)data);
}

static void _on_response_check_cp_poweron(TcorePending *pending,
					int data_len, const void *data, void *user_data)
{
	const TcoreATResponse *resp = data;
	TcoreHal *hal = user_data;

	GSList *tokens = NULL;
	const char *line;
	gboolean bpoweron = FALSE;
	int response = 0;

	if ((resp != NULL)
			&& resp->success) {
		dbg("Check CP POWER - [OK]");

		/* Parse AT Response */
		if (resp->lines) {
			line = (const char *) resp->lines->data;
			tokens = tcore_at_tok_new(line);
			if (g_slist_length(tokens) != 1) {
				err("Invalid message");
				goto ERROR;
			}
		}

		response = atoi(g_slist_nth_data(tokens, 0));
		dbg("CPAS State: [%d]", response);

		switch (response) {
		case IMC_AT_CPAS_RESULT_READY:
		case IMC_AT_CPAS_RESULT_RINGING:
		case IMC_AT_CPAS_RESULT_CALL_PROGRESS:
		case IMC_AT_CPAS_RESULT_ASLEEP:
			dbg("CP Power ON!!!");
			bpoweron = TRUE;
		break;

		case IMC_AT_CPAS_RESULT_UNAVAIL:
		case IMC_AT_CPAS_RESULT_UNKNOWN:
		default:
			err("Value is Unvailable/Unknown - but CP responded - proceed with Power ON!!!");
			bpoweron = TRUE;
		break;
		}
	} else {
		dbg("Check CP POWER - [NOK]");
	}

ERROR:
	/* Free tokens */
	tcore_at_tok_free(tokens);

	if (bpoweron == TRUE) {
		dbg("CP Power ON received");

		/* Enable Logging */
		_send_enable_logging_command(hal);
	} else {
		err("CP is not ready, send CPAS again");
		_check_cp_poweron(hal);
	}
	return;
}

static void _send_enable_logging_command(TcoreHal *hal)
{
	TcoreATRequest *at_req = NULL;
	TcorePending *pending = NULL;

	dbg("Sending Trace enabling command for CP logging");

	/* Create Pending request */
	pending = tcore_pending_new(NULL, 0);

	/* Create AT Request */
	at_req = tcore_at_request_new("at+xsystrace=1,\"digrf=1;bb_sw=1;3g_sw=1\",\"digrf=0x84\",\"oct=4\";+xsystrace=11;+trace=1",
							NULL, TCORE_AT_NO_RESULT);


	dbg("AT-Command: [%s] Prefix(if any): [%s] Command length: [%d]",
						at_req->cmd, at_req->prefix, strlen(at_req->cmd));

	/* Set request data and register Response and Send callbacks */
	tcore_pending_set_request_data(pending, 0, at_req);
	tcore_pending_set_response_callback(pending, _on_response_enable_logging, hal);
	tcore_pending_set_send_callback(pending, _on_confirmation_send_message, NULL);

	/* Send command to CP */
	if (tcore_hal_send_request(hal, pending) != TCORE_RETURN_SUCCESS) {
		err("Failed to send Trace logging command");
	} else {
		dbg("Successfully sent Trace logging command");
	}
}

static gboolean _check_cp_poweron(TcoreHal *hal)
{
	TcoreATRequest *at_req;
	TcorePending *pending = NULL;

	/* Create Pending request */
	pending = tcore_pending_new(NULL, 0);

	/* Create AT Request */
	at_req = tcore_at_request_new("AT+CPAS", "+CPAS:", TCORE_AT_SINGLELINE);

	dbg("AT-Command: [%s] Prefix(if any): [%s] Command length: [%d]",
					at_req->cmd, at_req->prefix, strlen(at_req->cmd));

	tcore_pending_set_priority(pending, TCORE_PENDING_PRIORITY_DEFAULT);

	/* Set timeout value and timeout callback */
	tcore_pending_set_timeout(pending, 10);
	tcore_pending_set_timeout_callback(pending, _on_timeout_check_cp_poweron, hal);

	/* Set request data and register Response and Send callbacks */
	tcore_pending_set_request_data(pending, 0, at_req);
	tcore_pending_set_response_callback(pending, _on_response_check_cp_poweron, hal);
	tcore_pending_set_send_callback(pending, _on_confirmation_send_message, NULL);

	/* Send command to CP */
	if (tcore_hal_send_request(hal, pending) != TCORE_RETURN_SUCCESS) {
		err("Failed to send CPAS");
		return FALSE;
	} else {
		dbg("Successfully sent CPAS");
		return TRUE;
	}
}

void config_check_cp_power(TcoreHal *hal)
{
	gboolean ret;
	dbg("Entry");

	if (hal == NULL)
		return;

	ret = _check_cp_poweron(hal);
	if (ret == TRUE) {
		dbg("Successfully sent check CP Power ON command");
	} else {
		err("Failed to send check CP Power ON command");
	}
}
