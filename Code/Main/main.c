

#include "User.h"
#include "MCP23S17.h"
#include "WS2812.h"
#include "PMW3360.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "esp_timer.h"
#include <stdio.h>



#define ms portTICK_PERIOD_MS 

uint8_t resolution_multiplier = 0;


spi_bus_config_t buscfg;
spi_device_interface_config_t devcfg_mcp23S17;
spi_device_handle_t handle_spi_mcp23S17;
spi_device_handle_t handle_spi_PMW3360;
//task handles 
TaskHandle_t xTaskGetDataFromSPI = NULL; 
TaskHandle_t xTaskGetSensorData = NULL; 
TaskHandle_t xTaskLEDHandle = NULL; 
TaskHandle_t xTaskMakeHIDReport = NULL; 
 
//queue handles 
QueueHandle_t xButtonsQueue, xSensorQueue, xReportQueue, xLEDStateQueue, xCPIEncoder, xSensorCPIQueue, xEncoderQueue; 
 
 
//queue structures 
struct buttons_t{ 
    uint16_t Buttons; 
    int16_t encoder; 
}; 

struct sensor_t{ 
    int16_t dx; 
    int16_t dy; 
}; 


struct __attribute__((packed)) hid_report_t { 
    uint8_t buttons; 
    int16_t dx; 
    int16_t dy; 
    int16_t vwheel; 
	int16_t hwheel;
}; 
 


spi_device_interface_config_t	devcfg_PMW3360={
		.address_bits = 0,
		.command_bits = 0,
		.dummy_bits = 0,
		.mode = 3,
		.duty_cycle_pos = 0,
		.cs_ena_posttrans = 0, //500ns 2mhz
		.cs_ena_pretrans = 0,
		.clock_speed_hz = SPI_CLOCK,   
		.spics_io_num = -1,
		.flags = 0,
		.queue_size = 7,
		.pre_cb = NULL,
		.post_cb = NULL
	};

spi_device_interface_config_t devcfg_mcp23S17={
	.address_bits = 0,
	.command_bits = 0,
	.dummy_bits = 0,
	.mode = 0,
	.duty_cycle_pos = 0,
	.cs_ena_posttrans = 0,
	.cs_ena_pretrans = 0,
	.clock_speed_hz = SPI_CLOCK,   
	.spics_io_num = MCP_CS,
	.flags = 0,
	.queue_size = 7,
	.pre_cb = NULL,
	.post_cb = NULL,

};





void initSPI(void)
{
	// spi_bus_config_t
	buscfg.sclk_io_num = SCLK;   		// GPIO pin for Spi CLocK signal, or -1 if not used.
	buscfg.mosi_io_num = MOSI;   		// GPIO pin for Master Out Slave In (=spi_d) signal, or -1 if not used.
	buscfg.miso_io_num = MISO;  	    // GPIO pin for Master In Slave Out (=spi_q) signal, or -1 if not used.O
	buscfg.quadwp_io_num = -1;  					// GPIO pin for WP (Write Protect) signal which is used as D2 in 4-bit communication modes, or -1 if not used.
	buscfg.quadhd_io_num = -1;  					// GPIO pin for HD (HolD) signal which is used as D3 in 4-bit communication modes, or -1 if not used.
	buscfg.max_transfer_sz = 5000;  					// Maximum transfer size, in bytes. Defaults to 4094 if 0.
	ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)); 		// Use dma_chan 1
 
	ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg_mcp23S17, &handle_spi_mcp23S17));
	ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg_PMW3360, &handle_spi_PMW3360));
}


volatile int16_t encoder_accumulated_delta = 0;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR encoder_isr_handler(void* arg) {
    static uint8_t prev_enc_state = 0;

    uint8_t enca = gpio_get_level(ENCA);
    uint8_t encb = gpio_get_level(ENCB);
    uint8_t enc_state = (enca << 1) | encb;

    int16_t delta = 0;
    switch (prev_enc_state) {
        case 0: // 00
            if (enc_state == 1) delta = 1; // CW
            else if (enc_state == 2) delta = -1; // CCW
            break;
        case 1: // 01
            if (enc_state == 3) delta = 1; // CW
            else if (enc_state == 0) delta = -1; // CCW
            break;
        case 2: // 10
            if (enc_state == 0) delta = 1; // CW
            else if (enc_state == 3) delta = -1; // CCW
            break;
        case 3: // 11
            if (enc_state == 2) delta = 1; // CW
            else if (enc_state == 1) delta = -1; // CCW
            break;
    }
	delta *= resolution_multiplier;
	if (delta != 0) {
        // Enter critical section to update shared variable
        portENTER_CRITICAL_ISR(&mux);
        encoder_accumulated_delta += delta;
        portEXIT_CRITICAL_ISR(&mux);
    }
	/*
    if (delta != 0) {
        xQueueSendFromISR(xEncoderQueue, &delta, NULL);
    }
*/
    prev_enc_state = enc_state;
}



void init_pins(){

	gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = ((1ULL << MCP_RST)  | (1ULL <<PMW_RST) | (1ULL <<PMW_CS) );
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = true;
    gpio_config(&io_conf);
	gpio_set_level(MCP_RST, HIGH);
	gpio_set_level(PMW_RST, LOW);
	vTaskDelay(50/ms);
	gpio_set_level(PMW_RST, HIGH);
	vTaskDelay(50/ms);
	gpio_set_level(PMW_CS, LOW);
	vTaskDelay(10/ms);
	gpio_set_level(PMW_CS, HIGH);
	//gpio_reset_pin(PMW_CS);

	// Configure encoder pins with interrupts, no pull-ups/pull-downs
    gpio_config_t enc_conf = {
        .pin_bit_mask = (1ULL << ENCA) | (1ULL << ENCB),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // Disabled as it helped
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Disabled as it helped
        .intr_type = GPIO_INTR_ANYEDGE
    };
    esp_err_t ret = gpio_config(&enc_conf);
    if (ret != ESP_OK) {
        printf("Failed to configure GPIO: %s\n", esp_err_to_name(ret));
        return;
    }

	// Install ISR service
    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK) {
        printf("Failed to install ISR service: %s\n", esp_err_to_name(ret));
        return;
    }

    // Add ISR handlers
    ret = gpio_isr_handler_add(ENCA, encoder_isr_handler, NULL);
    if (ret != ESP_OK) {
        printf("Failed to add ISR for ENCA: %s\n", esp_err_to_name(ret));
        return;
    }
    ret = gpio_isr_handler_add(ENCB, encoder_isr_handler, NULL);
    if (ret != ESP_OK) {
        printf("Failed to add ISR for ENCB: %s\n", esp_err_to_name(ret));
        return;
    }

	    // Configure input pin (GPIO 0)

		gpio_config_t input_conf = {};
		input_conf.pin_bit_mask = (1ULL << 0);
		input_conf.mode = GPIO_MODE_INPUT;
		input_conf.pull_up_en = GPIO_PULLUP_DISABLE;
		input_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		gpio_config(&input_conf);
}


void MCP_init(){
	MCP23S17_Initalize(handle_spi_mcp23S17);
	
	// Set port B as output and PortA as input
	mcp23S17_GpioMode(0xFFFF, handle_spi_mcp23S17);  		// PORTB | PORTA	
	// Enable the pullups on PortA
	mcp23S17_PullupMode(0x0000, handle_spi_mcp23S17);  	// PORTB | PORTA	
}

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return desc_hid_report;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    if (report_id == 2 && reqlen >= 1) {
        memcpy(buffer, &resolution_multiplier, 1);
        return 1;
    }
  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
 if (report_id == 2 && bufsize >= 1) {
        memcpy(&resolution_multiplier, buffer, 1);
    }
}

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief String descriptor
 */

const char* hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "DPVL LLC",             // 1: Manufacturer
    "TrackBMania",      // 2: Product
    "00001",              // 3: Serials, should use chip ID
    "HID interface",  // 4: HID
};

static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(desc_hid_report), 0x81, 16, 10),
};

void usb_init(){
 const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

}




void init_hw(){

	init_pins();

	initSPI();

	PMW3360_Init(PMW_CS, handle_spi_PMW3360);

	MCP_init();

	led_init();

	
	//debig/flash mode
	
	uint16_t buttons_buf =  mcp23S17_ReadPorts(GPIOA, handle_spi_mcp23S17); 
    if(!(buttons_buf & (1<<RMB))){ 
		usb_init();
	}

}




void vTaskGetDataFromSPI( void * pvParameters ) 
{  
		uint16_t buttons_buf; 
		uint16_t hid_buttons; 
		uint8_t enc_buf = 0x00;
		uint8_t prev_enc_state = 0x00;
		int16_t encoder_value = 0x00;
		uint8_t cpi_encoder = 0x10;

		struct buttons_t _buttons; 
		struct sensor_t PMW_data;

		xButtonsQueue 	= 	xQueueCreate(10, 	(sizeof(int16_t))*2); 
		xCPIEncoder 	= 	xQueueCreate(2, 	sizeof(uint8_t));
		xLEDStateQueue 	= 	xQueueCreate(2, 	sizeof(uint8_t));
		xSensorQueue 	= 	xQueueCreate(10, 	(sizeof(int16_t))*2);
		xEncoderQueue 	=	xQueueCreate(100, sizeof(int16_t));

  while(1) 
  { 
	/*BUTTONS*/
    buttons_buf =  mcp23S17_ReadPorts(GPIOA, handle_spi_mcp23S17); 

    //mouse keys bits in hid 
	//printf("Buttons: %d\n", buttons_buf); 
    for (int i = 0; i < 16; ++i) 
    { 
      switch (i) {
		case GPA0: 
		if((buttons_buf & (1<<i))){//not zero
			hid_buttons |= (1<<LMB); //mrb
			//printf("B1\n"); 
			}
			break; 
		case GPA1: 
			//Buttons |= (buttons_buf & (1<<i)) << LMB; 
			if((buttons_buf & (1<<i))){//not zero
				hid_buttons |= (1<<RMB); //mfb
				//printf("B1\n"); 
				}
			break; 
		case GPA2: 
			//Buttons |= (buttons_buf & (1<<i)) << LMB; 
			break; 
		case GPA3: //B1
			if((buttons_buf & (1<<i))){//not zero
			hid_buttons |= (1<<B1);
			//printf("B1\n"); 
			}
			break; 
		case GPA4: //B2
			if((buttons_buf & (1<<i))){//not zero
			hid_buttons |= (1<<B2);
			//printf("B2\n"); 
			}
			break; 
		case GPA5: //B3
			if((buttons_buf & (1<<i))){//not zero
			hid_buttons |= (1<<B3);
			//printf("B3\n"); 
			}
			break; 
		case GPA6: //B4
			if((buttons_buf & (1<<i))){//not zero
			hid_buttons |= (1<<B4);
			//printf("B4\n"); 
			cpi_encoder--;
			
			if(cpi_encoder > 0x77){
				cpi_encoder = 0x00;
			}
			
			PMW3360_WriteByte(Config1, cpi_encoder, handle_spi_PMW3360);
			vTaskDelay(500 / portTICK_PERIOD_MS); 
			}
			break; 
		case GPA7: //B5
			if((buttons_buf & (1<<i))){//not zero
			hid_buttons |= (1<<B5);
			//printf("B5\n"); 
			cpi_encoder++;
			
			if(cpi_encoder > 0x77){
				cpi_encoder = 0x00;
			}

			PMW3360_WriteByte(Config1, cpi_encoder, handle_spi_PMW3360);
			vTaskDelay(500 / portTICK_PERIOD_MS); 
  } 
			
			break; 
		case GPB0: //B6
	
			break; 
		case GPB1: //B7
		
			break; 
		case GPB2: //ENCA
		
			break; 
		case GPB3: //ENCB
		
			break; 
		case GPB4: 

			break; 
		case GPB5: //LMB
			if((buttons_buf & (1<<i))){//not zero
				hid_buttons |= (1<< MRB); //RMB
				
			}
			break; 
		case GPB6: //MmB
			if((buttons_buf & (1<<i))){//not zero
				hid_buttons |= (1<< MFB); //mmb
				
			}
			break; 
		case GPB7: //RMB
			if((buttons_buf & (1<<i))){//not zero
				hid_buttons |= (1<< MMB); //lmb
				
			}
		default: 
			break; 
	  	}	
    } 
 
    _buttons.Buttons = hid_buttons; 
	/*ENCODER*/
/*
00
01
10
11 // rest pos //2 0 1 3 - cw // 1 0 2 3 - CCW
10
00
*/
/* ENCODER */
        // Read encoder pins directly from IO37 (ENCA) and IO38 (ENCB)
		/*
    uint8_t enca = gpio_get_level(ENCA); // ENCA
	uint8_t encb = gpio_get_level(ENCB); // ENCB
	//printf("enca: %u\n", enca);
	//printf("encb: %u\n", encb);	
        enc_buf = (enca << 1) | encb; // Combine into 2-bit state (0b00, 0b01, 0b10, 0b11)

   	if (enc_buf != prev_enc_state){
		switch(enc_buf){
			case 0:
				if (prev_enc_state == 2)
				{
					//mid cw rot
				}
				if (prev_enc_state == 1)
				{
					//mid ccw rot
				}
				break;
			case 1:
				if (prev_enc_state == 0)
				{
					//mid cw rot
				}
				if (prev_enc_state == 3)
				{
					//start ccw rot
				}
				break;
			case 2:
				if (prev_enc_state == 3)
				{
					//start cw rot
				}
				if (prev_enc_state == 0)
				{
					//mid ccw rot
				}
				break;
			case 3:
				if (prev_enc_state == 1)
				{
					//printf("CW:\n");
					encoder_value = encoder_value + (3*resolution_multiplier);
					_buttons.encoder = encoder_value;

				}
				if (prev_enc_state == 2)
				{
					//printf("CCW:\n");
					encoder_value = encoder_value - (3*resolution_multiplier);
					_buttons.encoder = encoder_value;


				}
				break;
			default:
				break;
		}
		prev_enc_state=enc_buf;
	}
	*/
	/* ENCODER */
        // Read accumulated delta with critical section
        int16_t encoder_value;
        portENTER_CRITICAL(&mux);
        encoder_value = encoder_accumulated_delta;
		encoder_accumulated_delta *= 0.80; // Reset after reading
        portEXIT_CRITICAL(&mux);
	/*ENCODER*/
	/*
        int16_t encoder_value = 0;
        int16_t delta;
        // Process all available deltas
        while (xQueueReceive(xEncoderQueue, &delta, 0)) {
            encoder_value += delta;
        }*/
	_buttons.encoder = encoder_value;
	/*CPI CHANGE*/
		if ((buttons_buf & (1<<B1))) //b7 pressed - change cpi
		{	
			uint8_t send = LED_SET_CPI_STATE;

			if(encoder_value !=0){
				if(encoder_value > 0){
					cpi_encoder = cpi_encoder+1;
				}
				if(encoder_value < 0){
					cpi_encoder = cpi_encoder-1;
				}
    		}

			if(cpi_encoder > 0x77){
				cpi_encoder = 0x00;
			}

			PMW3360_WriteByte(Config1, cpi_encoder, handle_spi_PMW3360);

			xQueueSend(xCPIEncoder, &cpi_encoder, 0);
			xQueueSend(xLEDStateQueue, &send, 0);
		}
    
	xQueueSend(xButtonsQueue, &_buttons, 0); 

	PMW3360_WriteByte(Motion, 0x00, handle_spi_PMW3360);
	PMW3360_ReadByte(Motion, handle_spi_PMW3360);

	PMW_data.dx = (PMW3360_ReadByte(Delta_X_L, handle_spi_PMW3360) | (PMW3360_ReadByte(Delta_X_H, handle_spi_PMW3360) << 8));
	PMW_data.dy = (PMW3360_ReadByte(Delta_Y_L, handle_spi_PMW3360) | (PMW3360_ReadByte(Delta_Y_H, handle_spi_PMW3360) << 8));
		
	xQueueSend(xSensorQueue, &PMW_data, 0);

	//***********Clear variables************* */
	enc_buf = 0x00;
	buttons_buf = 0x00; 
    hid_buttons = 0x00; 
	if(encoder_value !=0){
	if(encoder_value > 0){
			encoder_value = encoder_value -1;
			_buttons.encoder = encoder_value;
		}
	if(encoder_value < 0){
			encoder_value = encoder_value + 1;
			_buttons.encoder = encoder_value;
		}
    }
	/*LOOP TASK*/
    vTaskDelay(5 / portTICK_PERIOD_MS); 
  } 
} 


void vTaskLEDHandle( void * pvParameters ) {
	static uint8_t led_strip_pixels[LED_NUMBERS * 3];
	uint8_t REQ_LED_STATE;

    while (1) {

			if (uxQueueMessagesWaiting(xLEDStateQueue)) {
				xQueueReceive(xLEDStateQueue, &REQ_LED_STATE, 0);
				if (REQ_LED_STATE == LED_SET_CPI_STATE) {
					uint8_t cpi_enc_buf;
					xQueueReceive(xCPIEncoder, &cpi_enc_buf, 0);

					if (cpi_enc_buf <= 0x35) {
						for (int j = 0; j < LED_NUMBERS; j++) {
							led_strip_pixels[j * 3 + RED] = cpi_enc_buf;
							led_strip_pixels[j * 3 + GREEN] = 0x35 - cpi_enc_buf;
							led_strip_pixels[j * 3 + BLUE] = 0;
						}
					} else if (cpi_enc_buf > 0x35)
					 {
						for (int j = 0; j < LED_NUMBERS; j++) {
							led_strip_pixels[j * 3 + RED] = 0;
							led_strip_pixels[j * 3 + GREEN] = 0x77 - cpi_enc_buf;
							led_strip_pixels[j * 3 + BLUE] = cpi_enc_buf - 0x35;
						}
					}

					for (int i = 0; i < 75; i++) {
						led_send_data(led_strip_pixels);
						//vTaskDelay(pdMS_TO_TICKS(10));
					}
				}
				memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
				REQ_LED_STATE = 0;
		}	


		else{
		//breath animation*********************************
		if (tud_ready()){
			memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
			for (int i = 0; i < 75; i++) {
				for (int j = 0; j < LED_NUMBERS; j++) {
					led_strip_pixels[j * 3 + RED] = i;
					led_strip_pixels[j * 3 + BLUE] = i;
				}
				led_send_data(led_strip_pixels);
				vTaskDelay(pdMS_TO_TICKS(10));
			}
			for (int i = 75; i > 1; i--) {
				for (int j = 0; j < LED_NUMBERS; j++) {
					led_strip_pixels[j * 3 + RED] = i;
					led_strip_pixels[j * 3 + BLUE] = i;
				}
				led_send_data(led_strip_pixels);
				vTaskDelay(pdMS_TO_TICKS(100));
			}
			vTaskDelay(pdMS_TO_TICKS(10));
		}

		else{//turn off and sleep when no usb connection
			
				memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
				led_send_data(led_strip_pixels);
				vTaskDelay(pdMS_TO_TICKS(100));
			
			vTaskDelay(pdMS_TO_TICKS(3000));
		}
		}
	}
}

/*HID Report TASK*/

struct hid_report_t report;

void vTaskMakeHIDReport( void * pvParameters ) {
	
	struct sensor_t 	PMW_data;
	struct buttons_t 	_buttons;
	while(1){
		if (tud_mounted()){
		if ((uxQueueMessagesWaiting(xSensorQueue) || uxQueueMessagesWaiting(xButtonsQueue))) //if any new data exists
		{
			xQueueReceive(xSensorQueue, &PMW_data, 0);
			xQueueReceive(xButtonsQueue, &_buttons, 0);
			//fill
			report.dx 		= 	PMW_data.dx; //reverse dy

			report.dy 		= -	PMW_data.dy;

			report.buttons 	= 	_buttons.Buttons;

			report.vwheel 	= 	_buttons.encoder;

			//send
			tud_hid_report(1, &report, sizeof(report));

			//clear
			memset(&report, 0, sizeof(report));
		}
		}
		vTaskDelay(10/ms);
	}

}

/*MAIN*/
void app_main(void)

{  
	init_hw();

	xTaskCreate(&vTaskGetDataFromSPI, "GetButtonTask", 2048, NULL, 17, &xTaskGetDataFromSPI); 

	xTaskCreate(&vTaskLEDHandle, "LedTask", 3072, NULL, 17, &xTaskLEDHandle); 

	xTaskCreate(&vTaskMakeHIDReport, "HIDTask", 2048, NULL, 17, &xTaskMakeHIDReport); 

	while(1){

    	vTaskDelay(10000 / portTICK_PERIOD_MS);
	}
}