#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"

#include "WS2812.h"

#define SPI_CLOCK 1000000

#define MCP_RST 15
#define PMW_RST 16
#define INT_B 17
#define INT_A 18
#define PMW_CS 10
#define MOSI 11
#define SCLK 12
#define MISO 13
#define MCP_CS 14
#define MOTION_PIN 21
#define ENCA 38
#define ENCB 37
//BUTTONS
/*
Standard Mouse Buttons:
Bit 0: Left button
Bit 1: Right button
Bit 2: Middle button
Forward and Back Buttons:
Bit 3: Back button
 (commonly used for navigation, e.g., "Browser Back")
Bit 4: Forward button 
(commonly used for navigation, e.g., "Browser Forward")
*/
//port b
#define B6 0
#define B7 1

//
#define MRB 3
#define MFB 4
#define LMB 0
#define MMB 2
#define RMB 1
//port a
//
#define B1 3
#define B2 4
#define B3 5
#define B4 6
#define B5 7


//LEDS
#define LED_DEFAULT_STATE 0
#define LED_SET_CPI_STATE 1


uint8_t const desc_hid_report[] = {
    0x05, 0x01,         // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,         // Usage (Mouse)
    0xA1, 0x01,         // Collection (Application)
    0x05, 0x01,         //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,         //   Usage (Mouse)
    0xA1, 0x02,         //   Collection (Logical)
    0x85, 0x01,         //     Report ID (1)
    0x09, 0x01,         //     Usage (Pointer)
    0xA1, 0x00,         //     Collection (Physical)
    0x05, 0x09,         //       Usage Page (Button)
    0x19, 0x01,         //       Usage Minimum (0x01)
    0x29, 0x08,         //       Usage Maximum (0x08)
    0x95, 0x08,         //       Report Count (8)
    0x75, 0x01,         //       Report Size (1)
    0x25, 0x01,         //       Logical Maximum (1)
    0x81, 0x02,         //       Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,         //       Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,         //       Usage (X)
    0x09, 0x31,         //       Usage (Y)
    0x95, 0x02,         //       Report Count (2)
    0x75, 0x10,         //       Report Size (16)
    0x16, 0x00, 0x80,   //       Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,   //       Logical Maximum (32767)
    0x81, 0x06,         //       Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xA1, 0x02,         //       Collection (Logical)
    0x85, 0x02,         //         Report ID (2)
    0x09, 0x48,         //         Usage (Resolution Multiplier)
    0x95, 0x01,         //         Report Count (1)
    0x75, 0x02,         //         Report Size (2)
    0x15, 0x00,         //         Logical Minimum (0)
    0x25, 0x01,         //         Logical Maximum (1)
    0x35, 0x01,         //         Physical Minimum (1)
    0x45, 0x78,         //         Physical Maximum (240)
    0xB1, 0x02,         //         Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x85, 0x01,         //         Report ID (1)
    0x09, 0x38,         //         Usage (Wheel)
    0x35, 0x00,         //         Physical Minimum (0)
    0x45, 0x00,         //         Physical Maximum (0)
    0x16, 0x00, 0x80,   //         Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,   //         Logical Maximum (32767)
    0x75, 0x10,         //         Report Size (16)
    0x81, 0x06,         //         Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,               //       End Collection
    0xA1, 0x02,         //       Collection (Logical)
    0x85, 0x02,         //         Report ID (2)
    0x09, 0x48,         //         Usage (Resolution Multiplier)
    0x75, 0x02,         //         Report Size (2)
    0x15, 0x00,         //         Logical Minimum (0)
    0x25, 0x01,         //         Logical Maximum (1)
    0x35, 0x01,         //         Physical Minimum (1)
    0x45, 0x78,         //         Physical Maximum (120)
    0xB1, 0x02,         //         Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x35, 0x00,         //         Physical Minimum (0)
    0x45, 0x00,         //         Physical Maximum (0)
    0x75, 0x04,         //         Report Size (4)
    0xB1, 0x03,         //         Feature (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x85, 0x01,         //         Report ID (1)
    0x05, 0x0C,         //         Usage Page (Consumer)
    0x16, 0x00, 0x80,   //         Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,   //         Logical Maximum (32767)
    0x75, 0x10,         //         Report Size (16)
    0x0A, 0x38, 0x02,   //         Usage (AC Pan)
    0x81, 0x06,         //         Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,               //       End Collection
    0xC0,               //     End Collection
    0xC0,               //   End Collection
    0xC0,               // End Collection
};