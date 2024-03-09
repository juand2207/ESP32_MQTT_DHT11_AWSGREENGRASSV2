/*------------------------------------------------------------------------------
    DHT22 temperature & humidity sensor AM2302 (DHT22) driver for ESP32
    Jun 2017:	Ricardo Timmermann, new for DHT22  	
    Code Based on Adafruit Industries and Sam Johnston and Coffe & Beer. Please help
    to improve this code. 
    
    This example code is in the Public Domain (or CC0 licensed, at your option.)
    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
    PLEASE KEEP THIS CODE IN LESS THAN 0XFF LINES. EACH LINE MAY CONTAIN ONE BUG !!!
---------------------------------------------------------------------------------*/

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "DHT11.h"

// == global defines =============================================

static const char* TAG = "DHT";

int DHTgpio = 4;				// my default DHT pin = 4
float humidity = 0.;
float temperature = 0.;

// == set the DHT used pin=========================================

void setDHTgpio( int gpio )
{
    DHTgpio = gpio;
}

// == get temp & hum =============================================

float getHumidity() { return humidity; }
float getTemperature() { return temperature; }

// == error handler ===============================================

void errorHandler(int response)
{
    switch(response) {
    
        case DHT_TIMEOUT_ERROR :
            ESP_LOGE( TAG, "Sensor Timeout\n" );
            break;

        case DHT_CHECKSUM_ERROR:
            ESP_LOGE( TAG, "CheckSum error\n" );
            break;

        case DHT_OK:
            break;

        default :
            ESP_LOGE( TAG, "Unknown error\n" );
    }
}

/*-------------------------------------------------------------------------------
;
;	get next state 
;
;	I don't like this logic. It needs some interrupt blocking / priority
;	to ensure it runs in realtime.
;
;--------------------------------------------------------------------------------*/

int getSignalLevel( int usTimeOut, bool state )
{

    int uSec = 0;
    while( gpio_get_level(DHTgpio)==state ) {

        if( uSec > usTimeOut ) 
            return -1;
        
        ++uSec;
        ets_delay_us(1);		// uSec delay
    }
    
    return uSec;
}

/*----------------------------------------------------------------------------
;
;	read DHT22 sensor
copy/paste from AM2302/DHT22 Docu:
DATA: Hum = 16 bits, Temp = 16 Bits, check-sum = 8 Bits
Example: MCU has received 40 bits data from AM2302 as
0000 0010 1000 1100 0000 0001 0101 1111 1110 1110
16 bits RH data + 16 bits T data + check sum
1) we convert 16 bits RH data from binary system to decimal system, 0000 0010 1000 1100 → 652
Binary system Decimal system: RH=652/10=65.2%RH
2) we convert 16 bits T data from binary system to decimal system, 0000 0001 0101 1111 → 351
Binary system Decimal system: T=351/10=35.1°C
When highest bit of temperature is 1, it means the temperature is below 0 degree Celsius. 
Example: 1000 0000 0110 0101, T= minus 10.1°C: 16 bits T data
3) Check Sum=0000 0010+1000 1100+0000 0001+0101 1111=1110 1110 Check-sum=the last 8 bits of Sum=11101110
Signal & Timings:
The interval of whole process must be beyond 2 seconds.
To request data from DHT:
1) Sent low pulse for > 1~10 ms (MILI SEC)
2) Sent high pulse for > 20~40 us (Micros).
3) When DHT detects the start signal, it will pull low the bus 80us as response signal, 
then the DHT pulls up 80us for preparation to send data.
4) When DHT is sending data to MCU, every bit's transmission begin with low-voltage-level that last 50us, 
the following high-voltage-level signal's length decide the bit is "1" or "0".
    0: 26~28 us
    1: 70 us
;----------------------------------------------------------------------------*/

#define MAXdhtData 5	// to complete 40 = 5*8 Bits

int readDHT()
{
    int uSec = 0;

    uint8_t dhtData[MAXdhtData];
    uint8_t byteInx = 0;
    uint8_t bitInx = 7;

    for (int k = 0; k < MAXdhtData; k++) 
        dhtData[k] = 0;

    // == Send start signal to DHT sensor ===========

    gpio_set_direction(DHTgpio, GPIO_MODE_OUTPUT);

    // pull down for 18 ms for a smooth and nice wake up 
    gpio_set_level(DHTgpio, 0);
    vTaskDelay(pdMS_TO_TICKS(18));

    // pull up for 20-40 us for a gentile asking for data
    gpio_set_level(DHTgpio, 1);
    ets_delay_us(30); // Let sensor respond

    gpio_set_direction(DHTgpio, GPIO_MODE_INPUT); // change to input mode

    // == DHT will keep the line low for 80 us and then high for 80us ====
    uSec = getSignalLevel(85, 0);
    if (uSec < 0) return DHT_TIMEOUT_ERROR; 

    uSec = getSignalLevel(85, 1);
    if (uSec < 0) return DHT_TIMEOUT_ERROR;

    // == No errors, read the 40 data bits ================
    for (int k = 0; k < 32; k++) { // DHT11 solo devuelve 4 bytes (32 bits)

        uSec = getSignalLevel(56, 0);
        if (uSec < 0) return DHT_TIMEOUT_ERROR;

        uSec = getSignalLevel(75, 1);
        if (uSec < 0) return DHT_TIMEOUT_ERROR;

        if (uSec > 40) {
            dhtData[byteInx] |= (1 << bitInx);
        }

        if (bitInx == 0) {
            bitInx = 7; 
            ++byteInx; 
        } else {
            bitInx--;
        }
    }

    // == get humidity and temperature from Data ==========================
    humidity = dhtData[0];
    temperature = dhtData[2];

    return DHT_OK;
}
