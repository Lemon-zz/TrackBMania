#include "PMW3360.h"
#include "PMW3360_FIRMWARE.h"
spi_transaction_t trans_PMW3360;

uint8_t PMW_CS = 0x00;


void PMW_write_SROM(spi_device_handle_t handle_spi_PMW3360){
    
    uint8_t tx_data[2];
    // Step 1: Write the address
    tx_data[0] = SROM_Load_Burst | 0x80;

    trans_PMW3360.tx_buffer = tx_data;
    trans_PMW3360.rx_buffer = NULL;  // We're not receiving data
    trans_PMW3360.length = 8;  // 16 bits
    gpio_set_level(PMW_CS, LOW);
    ESP_ERROR_CHECK(spi_device_transmit(handle_spi_PMW3360, &trans_PMW3360));
    ets_delay_us(20);
    for (size_t i = 0; i < pmw_firmware_length; i++)
    {
      tx_data[0] = pmw_firmware_data[i];
      trans_PMW3360.tx_buffer = tx_data;
      ESP_ERROR_CHECK(spi_device_transmit(handle_spi_PMW3360, &trans_PMW3360));
      ets_delay_us(20);
    }
    gpio_set_level(PMW_CS, HIGH);
	ets_delay_us(200);
}


void PMW3360_WriteByte(uint8_t address, uint8_t data,  spi_device_handle_t handle_spi_PMW3360)
{
	
    uint8_t tx_data[2];
    // Step 1: Write the address
    //address |= 0x80;
    tx_data[0]                  = address | 0x80;
    tx_data[1]                  = data;
    trans_PMW3360.tx_buffer     = tx_data;
    trans_PMW3360.rx_buffer     = NULL;  // We're not receiving data
    trans_PMW3360.length        = 16;  // 16 bits
    gpio_set_level(PMW_CS, LOW);
    ESP_ERROR_CHECK(spi_device_transmit(handle_spi_PMW3360, &trans_PMW3360));
	gpio_set_level(PMW_CS, HIGH);
    ets_delay_us(200);
}


uint8_t PMW3360_ReadByte(uint8_t address, spi_device_handle_t handle_spi_PMW3360)
{
    uint8_t tx_data[2];
    uint8_t rx_data[2];

    // Step 1: Write the address
    tx_data[0]                  = address & 0x7f;
    trans_PMW3360.tx_buffer     = tx_data;
    trans_PMW3360.rx_buffer     = NULL;  // We're not receiving data yet
    trans_PMW3360.length        = 8;  // 8 bits
    gpio_set_level(PMW_CS, LOW);
    ESP_ERROR_CHECK(spi_device_transmit(handle_spi_PMW3360, &trans_PMW3360));

    // Step 2: Wait for tSRAD
    ets_delay_us(200);  // Adjust the delay as needed

    // Step 3: Read the data
    tx_data[0] = 0x00;  // Send dummy data
    trans_PMW3360.tx_buffer = tx_data;
    trans_PMW3360.rx_buffer = rx_data;  // Now we're receiving data

    ESP_ERROR_CHECK(spi_device_transmit(handle_spi_PMW3360, &trans_PMW3360));
    gpio_set_level(PMW_CS, HIGH);
    ets_delay_us(50);
    return rx_data[0];
}

void PMW3360_Init(uint8_t GPIO, spi_device_handle_t handle_spi_PMW3360)
{
        PMW_CS = GPIO;
		uint8_t state = PMW3360_ReadByte(Product_ID, handle_spi_PMW3360);
        
		printf("Product_ID: %x\r\n", state);
        
		PMW3360_ReadByte(Motion, handle_spi_PMW3360);

		PMW3360_ReadByte(Delta_X_L, handle_spi_PMW3360);

		PMW3360_ReadByte(Delta_X_H, handle_spi_PMW3360);

		PMW3360_ReadByte(Delta_Y_L, handle_spi_PMW3360);

		PMW3360_ReadByte(Delta_Y_H, handle_spi_PMW3360);
		
		PMW3360_WriteByte(Config2, 0x00, handle_spi_PMW3360);

		PMW3360_WriteByte(SROM_Enable, 0x1D, handle_spi_PMW3360);
    	vTaskDelay(10/ms);

		PMW3360_WriteByte(SROM_Enable, 0x18, handle_spi_PMW3360);

		PMW_write_SROM(handle_spi_PMW3360);

		state = PMW3360_ReadByte(SROM_ID, handle_spi_PMW3360);

        vTaskDelay(1/ms);

		PMW3360_WriteByte(Config2, 0x00, handle_spi_PMW3360);

        //set defauit cpi to 00
        PMW3360_WriteByte(Config1, 0x10, handle_spi_PMW3360);
        printf("Finished SROM: %x\r\n", state);
}

