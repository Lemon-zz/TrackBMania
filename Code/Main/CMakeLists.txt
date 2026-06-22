idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES "driver" "MCP23S17" "PMW3360" "WS2812" "nvs_flash" "esp_timer"
                    REQUIRES espressif__tinyusb)