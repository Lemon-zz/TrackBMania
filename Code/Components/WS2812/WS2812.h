/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"


#include "driver/spi_master.h"
#include <stdbool.h>
#include "driver/spi_common.h"

#include "freertos/task.h"
#include "rom/ets_sys.h"

#define GREEN                       0
#define RED                         1
#define BLUE                        2

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM      42

#define LED_NUMBERS                 20


typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} led_strip_encoder_config_t;


esp_err_t   rmt_new_led_strip_encoder   (const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);
void        led_init();
void        led_send_data(uint8_t *led_strip_pixels);