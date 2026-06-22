/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include "WS2812.h"
 #include <esp_log.h>
 
 // Enable logging for debugging
 static const char *TAG = "led_encoder";
 
 // Global RMT handles
 rmt_encoder_handle_t led_encoder = NULL;
 rmt_channel_handle_t led_chan = NULL;
 
 // RMT configurations
 led_strip_encoder_config_t encoder_config = {
     .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
 };
 
 rmt_tx_channel_config_t tx_chan_config = {
     .clk_src = RMT_CLK_SRC_DEFAULT,
     .gpio_num = RMT_LED_STRIP_GPIO_NUM,
     .mem_block_symbols = 64, // Increase if flickering occurs
     .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
     .trans_queue_depth = 4,
 };
 
 rmt_transmit_config_t tx_config = {
     .loop_count = 0, // No transfer loop
 };
 
 typedef struct {
     rmt_encoder_t base;
     rmt_encoder_t *bytes_encoder;
     rmt_encoder_t *copy_encoder;
     int state;
     rmt_symbol_word_t reset_code;
 } rmt_led_strip_encoder_t;
 
 static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
 {
     rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
     rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
     rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
     rmt_encode_state_t session_state = RMT_ENCODING_RESET;
     rmt_encode_state_t state = RMT_ENCODING_RESET;
     size_t encoded_symbols = 0;
 
     switch (led_encoder->state) {
     case 0: // Send RGB data
         encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
         if (session_state & RMT_ENCODING_COMPLETE) {
             led_encoder->state = 1; // Switch to next state
         }
         if (session_state & RMT_ENCODING_MEM_FULL) {
             state |= RMT_ENCODING_MEM_FULL;
             goto out;
         }
         // Fall-through
     case 1: // Send reset code
         encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                sizeof(led_encoder->reset_code), &session_state);
         if (session_state & RMT_ENCODING_COMPLETE) {
             led_encoder->state = RMT_ENCODING_RESET;
             state |= RMT_ENCODING_COMPLETE;
         }
         if (session_state & RMT_ENCODING_MEM_FULL) {
             state |= RMT_ENCODING_MEM_FULL;
             goto out;
         }
     }
 out:
     *ret_state = state;
     return encoded_symbols;
 }
 
 static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
 {
     rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
     if (led_encoder->bytes_encoder) rmt_del_encoder(led_encoder->bytes_encoder);
     if (led_encoder->copy_encoder) rmt_del_encoder(led_encoder->copy_encoder);
     free(led_encoder);
     return ESP_OK;
 }
 
 static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
 {
     rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
     rmt_encoder_reset(led_encoder->bytes_encoder);
     rmt_encoder_reset(led_encoder->copy_encoder);
     led_encoder->state = RMT_ENCODING_RESET;
     return ESP_OK;
 }
 
 esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
 {
     esp_err_t ret = ESP_OK;
     rmt_led_strip_encoder_t *led_encoder = NULL;
 
     // Validate inputs
     if (!config || !ret_encoder) {
         ESP_LOGE(TAG, "Invalid argument: config or ret_encoder is NULL");
         return ESP_ERR_INVALID_ARG;
     }
 
     led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));
     if (!led_encoder) {
         ESP_LOGE(TAG, "No memory for led strip encoder");
         return ESP_ERR_NO_MEM;
     }
 
     led_encoder->base.encode = rmt_encode_led_strip;
     led_encoder->base.del = rmt_del_led_strip_encoder;
     led_encoder->base.reset = rmt_led_strip_encoder_reset;
 
     // Configure bytes encoder for WS2812 timing
     rmt_bytes_encoder_config_t bytes_encoder_config = {
         .bit0 = {
             .level0 = 1,
             .duration0 = 0.3 * config->resolution / 1000000, // T0H=0.3us
             .level1 = 0,
             .duration1 = 0.9 * config->resolution / 1000000, // T0L=0.9us
         },
         .bit1 = {
             .level0 = 1,
             .duration0 = 0.9 * config->resolution / 1000000, // T1H=0.9us
             .level1 = 0,
             .duration1 = 0.3 * config->resolution / 1000000, // T1L=0.3us
         },
         .flags.msb_first = 1 // WS2812 bit order: G7...G0R7...R0B7...B0
     };
     ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder));
 
     // Configure copy encoder
     rmt_copy_encoder_config_t copy_encoder_config = {};
     ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder));
 
     // Configure reset code (50us low)
     uint32_t reset_ticks = config->resolution / 1000000 * 50 / 2;
     led_encoder->reset_code = (rmt_symbol_word_t) {
         .level0 = 0,
         .duration0 = reset_ticks,
         .level1 = 0,
         .duration1 = reset_ticks,
     };
 
     *ret_encoder = &led_encoder->base;
     return ESP_OK;
 }
 
 void led_init(void)
 {
     ESP_LOGI(TAG, "Initializing LED strip RMT channel and encoder");
 
     // Initialize RMT TX channel
     if (rmt_new_tx_channel(&tx_chan_config, &led_chan) != ESP_OK) {
         ESP_LOGE(TAG, "Failed to create RMT TX channel");
         return;
     }
 
     // Initialize LED strip encoder
     if (rmt_new_led_strip_encoder(&encoder_config, &led_encoder) != ESP_OK) {
         ESP_LOGE(TAG, "Failed to create LED strip encoder");
         return;
     }
 
     // Enable RMT channel
     if (rmt_enable(led_chan) != ESP_OK) {
         ESP_LOGE(TAG, "Failed to enable RMT channel");
         return;
     }
 }
 
 void led_send_data(uint8_t *led_strip_pixels)
 {
     if (!led_chan || !led_encoder || !led_strip_pixels) {
         ESP_LOGE(TAG, "Invalid state: RMT channel, encoder, or data is NULL");
         return;
     }
 
     ESP_LOGI(TAG, "Transmitting LED data");
 
     // Ensure previous transmission is complete
     esp_err_t err = rmt_tx_wait_all_done(led_chan, portMAX_DELAY);
     if (err != ESP_OK) {
         ESP_LOGE(TAG, "rmt_tx_wait_all_done failed: %s", esp_err_to_name(err));
         return;
     }
 
     // Transmit new data
     err = rmt_transmit(led_chan, led_encoder, led_strip_pixels, LED_NUMBERS * 3, &tx_config);
     if (err != ESP_OK) {
         ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
         return;
     }
 }