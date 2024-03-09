#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "rom/ets_sys.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include <string.h>
#include <math.h>

#include "esp_log.h"

#include "DHT11.h"
#include "wifi.h"
#include "mqtt_demo_mutual_auth.h"

static const char *TAG = "MQTT_EXAMPLE";

char* DHT_reader_task(void)
{
    setDHTgpio(GPIO_NUM_21);

    printf("DHT Sensor Readings\n" );
    int ret = readDHT();
    
    errorHandler(ret);

    char humidityStr[10];
    char temperatureStr[10];
    sprintf(humidityStr, "%.2f", getHumidity());
    sprintf(temperatureStr, "%.2f", getTemperature());
    
    uint8_t mac[6];
    char mac_str[18];
    esp_efuse_mac_get_default(mac);    
    sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON *root = cJSON_CreateObject();
    cJSON *device = cJSON_CreateObject();
    cJSON *sensors = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "device", device);
    cJSON_AddItemToObject(root, "sensors", sensors);
    cJSON_AddNumberToObject(device, "uptime", xTaskGetTickCount() * portTICK_RATE_MS);
    cJSON_AddStringToObject(device, "hardware", mac_str);
    // esp_get_idf_version() returns the version of the ESP-IDF framework that the current application was built with.
    cJSON_AddStringToObject(device, "firmware", esp_get_idf_version());  
    cJSON_AddStringToObject(sensors, "temperature", temperatureStr);
    cJSON_AddStringToObject(sensors, "humidity", humidityStr);
    cJSON_AddStringToObject(root, "client", CLIENT_IDENTIFIER);
    cJSON_AddStringToObject(root, "status", "online");

    char *json_string = cJSON_Print(root);
    printf("%s\n", json_string);
    cJSON_Delete(root);

    // -- wait at least 2 sec before reading again ------------
    // The interval of whole process must be beyond 2 seconds !!
    vTaskDelay(2000 / portTICK_RATE_MS);
    
    return json_string;
}

/*-----------------------------------------------------------*/

/**
* @brief Entry point of demo.
*
* The example shown below uses MQTT APIs to send and receive MQTT packets
* over the TLS connection established using OpenSSL.
*
* The example is single threaded and uses statically allocated memory;
* it uses QOS1 and therefore implements a retransmission mechanism
* for Publish messages. Retransmission of publish messages are attempted
* when a MQTT connection is established with a session that was already
* present. All the outgoing publish messages waiting to receive PUBACK
* are resent in this demo. In order to support retransmission all the outgoing
* publishes are stored until a PUBACK is received.
*/
void aws_iot_demo(void *arg)
{
    int returnStatus = EXIT_SUCCESS;
    MQTTContext_t mqttContext = { 0 };
    NetworkContext_t xNetworkContext = { 0 };
    bool clientSessionPresent = false;
    struct timespec tp;

    /* Seed pseudo random number generator (provided by ISO C standard library) for
    * use by retry utils library when retrying failed network operations. */

    /* Get current time to seed pseudo random number generator. */
    ( void ) clock_gettime( CLOCK_REALTIME, &tp );
    /* Seed pseudo random number generator with nanoseconds. */
    srand( tp.tv_nsec );

    /* Initialize MQTT library. Initialization of the MQTT library needs to be
    * done only once in this demo. */
    returnStatus = initializeMqtt( &mqttContext, &xNetworkContext );

    if( returnStatus == EXIT_SUCCESS )
    {
        for( ; ; )
        {
            /* Generate payload from DHT sensor. */
            char* pcPayload = DHT_reader_task();
            uint16_t payloadLength = ( uint16_t ) strlen(pcPayload);

            /* Attempt to connect to the MQTT broker. If connection fails, retry after
            * a timeout. Timeout value will be exponentially increased till the maximum
            * attempts are reached or maximum timeout value is reached. The function
            * returns EXIT_FAILURE if the TCP connection cannot be established to
            * broker after configured number of attempts. */
            returnStatus = connectToServerWithBackoffRetries( &xNetworkContext );
            if( returnStatus == EXIT_FAILURE )
            {
                /* Log error to indicate connection failure after all
                * reconnect attempts are over. */
                LogError( ( "Failed to connect to MQTT broker %.*s.",
                            AWS_IOT_ENDPOINT_LENGTH,
                            AWS_IOT_ENDPOINT ) );
            }
            else
            {
                /* If TLS session is established, execute Subscribe/Publish loop. */
                returnStatus = subscribePublishLoop( &mqttContext,
                                                    &clientSessionPresent,
                                                    globalMqttTopic,
                                                    globalMqttTopicLength,
                                                    pcPayload, 
                                                    payloadLength );
            }

            if( returnStatus == EXIT_SUCCESS )
            {
                /* Log message indicating an iteration completed successfully. */
                LogInfo( ( "Demo completed successfully." ) );
            }

            /* End TLS session, then close TCP connection. */
            ( void ) xTlsDisconnect( &xNetworkContext );

            LogInfo( ( "Short delay before starting the next iteration....\n" ) );
            sleep( MQTT_SUBPUB_LOOP_DELAY_SECONDS );
        }
    }

    LogInfo( ( "returnStatus: %d", returnStatus ) );
}

void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %"PRIu32" bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);

    /* Initialize NVS. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    initialise_wifi();

    /* Set MQTT Topic name to the global variable. */ 
    globalMqttTopic = "clients/" CLIENT_IDENTIFIER "/hello/world";
    globalMqttTopicLength = ( uint16_t ) strlen( globalMqttTopic );

    xTaskCreate(&aws_iot_demo, "aws_iot_demo", 4096, NULL, 5, NULL );
}
