/* * OSS-7 - An opensource implementation of the DASH7 Alliance Protocol for ultra
 * lowpower wireless sensor communication
 *
 * Copyright 2015 University of Antwerp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** DASH7 IDs **
 *  Matthias 	2644018719666950248
 *  Jan 		2644018719666949598
 *  Girmi 		2654130928103788049
 */

#include "hwleds.h"
#include "hwsystem.h"
#include "scheduler.h"
#include "timer.h"
#include "assert.h"
#include "platform.h"


#include <stdio.h>
#include <stdlib.h>

#include "hwlcd.h"
#include "hwadc.h"
#include "d7ap_stack.h"
#include "fs.h"
#include "log.h"

#if (!defined PLATFORM_EFM32GG_STK3700 && !defined PLATFORM_EFM32HG_STK3400 && !defined PLATFORM_EZR32LG_WSTK6200A)
	#error Mismatch between the configured platform and the actual platform.
#endif

#include "userbutton.h"
#include "platform_sensors.h"
#include "platform_lcd.h"

#define SENSOR_FILE_ID           0x40
#define SENSOR_FILE_SIZE         8
#define ACTION_FILE_ID           0x41

#define APP_MODE_LEDS		1
#define APP_MODE_LCD		1 << 1
#define APP_MODE_CONSOLE	1 << 2

uint8_t app_mode_status = 0xFF;
uint8_t app_mode_status_changed = 0x00;
uint8_t app_mode = 0;


// Toggle different operational modes
void userbutton_callback(button_id_t button_id)
{
	#ifdef PLATFORM_EFM32GG_STK3700
	lcd_write_string("Butt %d", button_id);
	#else
	  lcd_write_string("button: %d\n", button_id);
	#endif
}

void execute_sensor_measurement()
{
#ifdef PLATFORM_EFM32GG_STK3700
  float internal_temp = hw_get_internal_temperature();
  lcd_write_temperature(internal_temp*10, 1);

  uint32_t vdd = hw_get_battery();


  fs_write_file(SENSOR_FILE_ID, 0, (uint8_t*)&internal_temp, sizeof(internal_temp)); // File 0x40 is configured to use D7AActP trigger an ALP action which broadcasts this file data on Access Class 0
#endif

#if (defined PLATFORM_EFM32HG_STK3400  || defined PLATFORM_EZR32LG_WSTK6200A)
  lcd_clear();
  float internal_temp = hw_get_internal_temperature();
  lcd_write_string("Int T: %2d.%d C\n", (int)internal_temp, (int)(internal_temp*10)%10);
  log_print_string("Int T: %2d.%d C\n", (int)internal_temp, (int)(internal_temp*10)%10);

  uint32_t rhData;
  uint32_t tData;
  getHumidityAndTemperature(&rhData, &tData);

  lcd_write_string("Ext T: %d.%d C\n", (tData/1000), (tData%1000)/100);
  log_print_string("Temp: %d.%d C\n", (tData/1000), (tData%1000)/100);

  lcd_write_string("Ext H: %d.%d\n", (rhData/1000), (rhData%1000)/100);
  log_print_string("Hum: %d.%d\n", (rhData/1000), (rhData%1000)/100);

  uint32_t vdd = hw_get_battery();

  lcd_write_string("Batt %d mV\n", vdd);
  log_print_string("Batt: %d mV\n", vdd);

  //TODO: put sensor values in array

  uint8_t sensor_values[8];
  uint16_t *pointer =  (uint16_t*) sensor_values;
  *pointer++ = (uint16_t) (internal_temp * 10);
  *pointer++ = (uint16_t) (tData /100);
  *pointer++ = (uint16_t) (rhData /100);
  *pointer++ = (uint16_t) (vdd /10);

  fs_write_file(SENSOR_FILE_ID, 0, (uint8_t*)&sensor_values,8);
#endif

  timer_post_task_delay(&execute_sensor_measurement, TIMER_TICKS_PER_SEC * 1);
}

void init_user_files()
{
    // file 0x40: contains our sensor data + configure an action file to be executed upon write
    fs_file_header_t file_header = (fs_file_header_t){
        .file_properties.action_protocol_enabled = 1,
        .file_properties.action_file_id = ACTION_FILE_ID,
        .file_properties.action_condition = ALP_ACT_COND_WRITE,
        .file_properties.storage_class = FS_STORAGE_VOLATILE,
        .file_properties.permissions = 0, // TODO
        .length = SENSOR_FILE_SIZE
    };

    fs_init_file(SENSOR_FILE_ID, &file_header, NULL);

    // configure file notification using D7AActP: write ALP command to broadcast changes made to file 0x40 in file 0x41
    // first generate ALP command consisting of ALP Control header, ALP File Data Request operand and D7ASP interface configuration
    alp_control_t alp_ctrl = {
        .group = false,
        .response_requested = false,
        .operation = ALP_OP_READ_FILE_DATA
    };

    alp_operand_file_data_request_t file_data_request_operand = {
        .file_offset = {
            .file_id = SENSOR_FILE_ID,
            .offset = 0
        },
        .requested_data_length = SENSOR_FILE_SIZE,
    };

    d7asp_fifo_config_t d7asp_fifo_config = {
        .fifo_ctrl_nls = false,
        .fifo_ctrl_stop_on_error = false,
        .fifo_ctrl_preferred = false,
        .fifo_ctrl_state = SESSION_STATE_PENDING,
        .qos = {
            .qos_ctrl_resp_mode = SESSION_RESP_MODE_NONE
        },
        .dormant_timeout = 0,
        .start_id = 0,
        .addressee = {
            .addressee_ctrl_has_id = false,
            .addressee_ctrl_virtual_id = false,
            .addressee_ctrl_access_class = 0,
            .addressee_id = 0
        }
    };

    // finally, register D7AActP file
    fs_init_file_with_D7AActP(ACTION_FILE_ID, &d7asp_fifo_config, &alp_ctrl, (uint8_t*)&file_data_request_operand);
}

void bootstrap()
{
	log_print_string("Device booted\n");

    dae_access_profile_t access_classes[1] = {
        {
            .control_scan_type_is_foreground = false,
            .control_csma_ca_mode = CSMA_CA_MODE_UNC,
            .control_number_of_subbands = 1,
            .subnet = 0x00,
            .scan_automation_period = 0,
            .transmission_timeout_period = 0xFF, // TODO compressed time value
            .subbands[0] = (subband_t){
                .channel_header = {
                    .ch_coding = PHY_CODING_PN9,
                    .ch_class = PHY_CLASS_NORMAL_RATE,
#ifdef PLATFORM_EZR32LG_WSTK6200A
                    .ch_freq_band = PHY_BAND_868
#else
                    .ch_freq_band = PHY_BAND_433
#endif
                },
                .channel_index_start = 16,
                .channel_index_end = 16,
                .eirp = 10,
                .ccao = 0
            }
        }
    };

    fs_init_args_t fs_init_args = (fs_init_args_t){
        .fs_user_files_init_cb = &init_user_files,
        .access_profiles_count = 1,
        .access_profiles = access_classes
    };

    d7ap_stack_init(&fs_init_args, NULL, false);

    initSensors();

    ubutton_register_callback(0, &userbutton_callback);
    ubutton_register_callback(1, &userbutton_callback);

    sched_register_task((&execute_sensor_measurement));

    timer_post_task_delay(&execute_sensor_measurement, TIMER_TICKS_PER_SEC * 1);

    lcd_write_string("EFM32 Sensor\n");
}

