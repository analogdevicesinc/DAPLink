/**
 * @file    max32xxx.c
 * @brief   board ID for the Maxim Integrated's MAX32XXX boards
 *
 * DAPLink Interface Firmware
 * Copyright (c) 2009-2021, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "DAP_config.h"
#include "target_config.h"

#include "target_family.h"
#include "target_board.h"


const char *board_id_max32660evsys = "0421";
const char *board_id_max32666fthr = "0422";
const char *board_id_max78000fthr = "0423";
const char *board_id_max32670evkit = "0424";
const char *board_id_max32650fthr = "0425";

const char *board_name_max32660evsys = "MAX32660EVSYS";
const char *board_name_max32666fthr = "MAX32666FTHR";
const char *board_name_max78000fthr = "MAX78000FTHR";
const char *board_name_max32670evkit = "MAX32670EVKIT";
const char *board_name_max32650fthr = "MAX32650FTHR";

const char *daplink_target_url_max32660evsys = "http://www.maximintegrated.com/max32660-evsys";
const char *daplink_target_url_max32666fthr = "http://www.maximintegrated.com/max32666fthr";
const char *daplink_target_url_max32670evkit = "http://www.maximintegrated.com/max32670evkit";
const char *daplink_target_url_max78000fthr = "http://www.maximintegrated.com/max78000fthr";
const char *daplink_target_url_max32650fthr = "http://www.maximintegrated.com/max32650FTHR";

uint16_t family_id_max3262X = kMaxim_MAX3262X_FamilyID;
uint16_t family_id_max3262X = kMaxim_MAX3262X_FamilyID;

extern target_cfg_t target_device_max32650;
extern target_cfg_t target_device_max32660;
extern target_cfg_t target_device_max32666;
extern target_cfg_t target_device_max32670;
extern target_cfg_t target_device_max78000;

target_cfg_t target_device;
static uint8_t device_type;
static char board_name[16];

static void set_target_device(uint32_t device)
{
    device_type = device;
	switch(device_type) {
		case 0:
        target_device = target_device_max32650;
		break;
		
		case 1:
        target_device = target_device_max32660;
		break;
		
		case 2:
        target_device = target_device_max32666;
		break;
		
		case 3:
        target_device = target_device_max32670;
		break;
		
		case 4:
        target_device = target_device_max78000;
		break;
	}
}

static void max32xxx_prerun_board_config(void)
{
	int devID = 0;
	
	// Todo get dev ID list
	switch (devID) {
		case 0://
		set_target_device(0);
        target_device.rt_family_id = kMaxim_MAX3266X_FamilyID;
        target_device.rt_board_id = board_id_max32660evsys;
        strcpy(board_name, board_name_max32660evsys);
		break;
		
		case 1://
		set_target_device(1);
        target_device.rt_family_id = kMaxim_MAX3266X_FamilyID;
        target_device.rt_board_id = board_id_max32666fthr;
        strcpy(board_name, board_name_max32666fthr);
		break;
		
		case 2://
		set_target_device(2);
        target_device.rt_family_id = kMaxim_MAX3266X_FamilyID;
        target_device.rt_board_id = board_id_max78000fthr;
        strcpy(board_name, board_name_max78000fthr);
		break;
		
		case 3://
		set_target_device(3);
        target_device.rt_family_id = kMaxim_MAX3266X_FamilyID;
        target_device.rt_board_id = board_id_max32670evkit;
        strcpy(board_name, board_name_max32670evkit);
		break;
		
		case 4://
		set_target_device(4);
        target_device.rt_family_id = kMaxim_MAX3266X_FamilyID;
        target_device.rt_board_id = board_id_max32650fthr;
        strcpy(board_name, board_name_max32650fthr);
		break;
	}
}

static void max32xxx_swd_set_target_reset(uint8_t asserted){

    // TODO: PIN_SWDIO | PIN_SWCLK | PIN_nRESET;
}

const board_info_t g_board_info = {
    .info_version = kBoardInfoVersion,
    .flags = kEnablePageErase,
    .prerun_board_config = max32xxx_prerun_board_config,
    .swd_set_target_reset = max32xxx_swd_set_target_reset,
    .target_cfg = &target_device,
    .board_vendor = "Maxim Integrated",
    .board_name = board_name,
};
