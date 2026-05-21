#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "rom/ets_sys.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "DHT22.h"
#include "wifi.h"
#include "mqtt_demo_mutual_auth.h"

static const char *TAG = "MQTT_EXAMPLE";
// AC Dimmer configuration
#define DIMMER_ZERO_CROSS_PIN   GPIO_NUM_23    // Zero crossing detection pin
#define DIMMER_CH1_PIN          GPIO_NUM_22    // Channel 1 control pin
#define DIMMER_CH2_PIN          GPIO_NUM_19    // Channel 2 control pin (if needed)
#define DIMMER_FREQ             60             // AC frequency in Hz (50 or 60)
#define DIMMER_TIMER_GROUP      TIMER_GROUP_0
#define DIMMER_TIMER_IDX        TIMER_0
#define DIMMER_TIMER_DIVIDER    80             // Timer clock divider
#define DIMMER_RESOLUTION       100            // Dimming resolution (0-100%)

// Global variables for dimmer control
static uint8_t dimmer_level_ch1 = 50;           // Initial dimmer level for channel 1 (50%)
static uint8_t dimmer_level_ch2 = 0;            // Current dimmer level for channel 2 (0-100%)
static bool dimmer_enabled = true;              // Flag to enable/disable dimmer
static QueueHandle_t zero_cross_evt_queue = NULL;

// DEBUG flags
static volatile uint32_t debug_zero_cross_count = 0;  // Counter for zero crossing events
static volatile uint32_t debug_triac_triggers = 0;    // Counter for TRIAC triggers

// Variables para prevenir watchdog y reinicios
static portMUX_TYPE dimmer_spinlock = portMUX_INITIALIZER_UNLOCKED;
static bool in_critical_section = false;

// Forward declarations
static void dimmer_timer_init(void);
static void dimmer_gpio_init(void);
static void IRAM_ATTR dimmer_zero_cross_isr(void* arg);
static void IRAM_ATTR dimmer_timer_isr(void* arg);
static void dimmer_control_task(void* pvParameters);
void set_dimmer_level(uint8_t channel, uint8_t level);

// Initialize dimmer timer
static void dimmer_timer_init(void) {
    // Timer configuration for dimmer control
    timer_config_t config = {
        .divider = DIMMER_TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = false,  // Cambiado a false para un solo disparo
    };
    timer_init(DIMMER_TIMER_GROUP, DIMMER_TIMER_IDX, &config);
    
    // Set timer counting to microseconds
    timer_set_counter_value(DIMMER_TIMER_GROUP, DIMMER_TIMER_IDX, 0);
    
    // Register timer interrupt handler
    timer_isr_register(DIMMER_TIMER_GROUP, DIMMER_TIMER_IDX, dimmer_timer_isr, NULL, ESP_INTR_FLAG_IRAM, NULL);
}

// Initialize GPIO pins for dimmer control
static void dimmer_gpio_init(void) {
    // Configure zero crossing detection pin as input with interrupt
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,      // Falling edge trigger
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << DIMMER_ZERO_CROSS_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    
    // Configure dimmer control pins as outputs
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ((1ULL << DIMMER_CH1_PIN) | (1ULL << DIMMER_CH2_PIN));
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    
    // Set initial state of dimmer pins to OFF
    gpio_set_level(DIMMER_CH1_PIN, 0);
    gpio_set_level(DIMMER_CH2_PIN, 0);
    
    // Install GPIO ISR service and add ISR handler for zero crossing pin
    gpio_install_isr_service(0);
    gpio_isr_handler_add(DIMMER_ZERO_CROSS_PIN, dimmer_zero_cross_isr, NULL);
}

// Zero crossing interrupt handler - MODIFICADO PARA ESTABILIDAD
static void IRAM_ATTR dimmer_zero_cross_isr(void* arg) {
    // Usamos una bandera para evitar anidamiento de interrupciones que puedan causar problemas
    if (in_critical_section) {
        return;  // Si ya estamos en una sección crítica, salimos para evitar problemas
    }
    
    in_critical_section = true;
    
    // Desactivamos interrupciones para este bloque crítico
    portENTER_CRITICAL_ISR(&dimmer_spinlock);
    
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(zero_cross_evt_queue, &gpio_num, NULL);
    
    debug_zero_cross_count++;  // Increment zero crossing counter
    
    // Caso especial: Si el nivel es 100%, activamos TRIAC directamente
    if (dimmer_enabled && dimmer_level_ch1 >= 100) {
        gpio_set_level(DIMMER_CH1_PIN, 1);
        debug_triac_triggers++;
    }
    // Caso especial: Si el nivel es 0%, mantenemos el TRIAC apagado
    else if (dimmer_enabled && dimmer_level_ch1 <= 0) {
        gpio_set_level(DIMMER_CH1_PIN, 0);
    }
    // Para otros valores (1-99%), configuramos un timer
    else if (dimmer_enabled) {
        // Mantenemos el TRIAC apagado inicialmente
        gpio_set_level(DIMMER_CH1_PIN, 0);
        
        // Calculate firing delay based on dimmer levels (0-100%)
        uint32_t half_cycle_us = (1000000 / (2 * DIMMER_FREQ)); // Half cycle time in microseconds
        uint32_t delay_us = (100 - dimmer_level_ch1) * half_cycle_us / 100;
        
        // Safety: prevent extremely short delays that could cause issues
        if (delay_us < 500) delay_us = 500;  // Mínimo 500us para evitar problemas
        
        // Set timer for triac firing
        timer_set_counter_value(DIMMER_TIMER_GROUP, DIMMER_TIMER_IDX, 0);
        timer_set_alarm_value(DIMMER_TIMER_GROUP, DIMMER_TIMER_IDX, delay_us);
        timer_set_alarm(DIMMER_TIMER_GROUP, DIMMER_TIMER_IDX, 1);
        timer_start(DIMMER_TIMER_GROUP, DIMMER_TIMER_IDX);
    }
    else {
        // Dimmer disabled - keep TRIAC off for safety
        gpio_set_level(DIMMER_CH1_PIN, 0);
    }
    
    // Channel 2 is always OFF in this application
    gpio_set_level(DIMMER_CH2_PIN, 0);
    
    // Reactivamos interrupciones
    portEXIT_CRITICAL_ISR(&dimmer_spinlock);
    in_critical_section = false;
}

// Timer interrupt handler for triac firing - SIMPLIFICADO
static void IRAM_ATTR dimmer_timer_isr(void* arg) {
    // Clear interrupt flag
    timer_group_clr_intr_status_in_isr(DIMMER_TIMER_GROUP, DIMMER_TIMER_IDX);
    
    if (dimmer_enabled && dimmer_level_ch1 > 0 && dimmer_level_ch1 < 100) {
        // Activate TRIAC
        gpio_set_level(DIMMER_CH1_PIN, 1);
        debug_triac_triggers++;
        
        // We'll turn off the TRIAC at the next zero crossing
        // No need for a short pulse that might cause issues
    }
}

// Task for dimmer control
static void dimmer_control_task(void* pvParameters) {
    uint32_t gpio_num;
    uint32_t last_reported_time = 0;
    
    while (1) {
        if (xQueueReceive(zero_cross_evt_queue, &gpio_num, portMAX_DELAY)) {
            // Process zero crossing event if needed
            // Most work is done in the ISR
            
            // Debug info each second
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (current_time - last_reported_time > 5000) {  // Report every 5 seconds
                ESP_LOGI(TAG, "Zero crossings: %u, TRIAC triggers: %u, Dimmer level: %u%%", 
                    debug_zero_cross_count, debug_triac_triggers, dimmer_level_ch1);
                last_reported_time = current_time;
            }
        }
    }
}

// Función para modificar el nivel del dimmer
void set_dimmer_level(uint8_t channel, uint8_t level) {
    // Ensure level is within range
    if (level > 100) level = 100;
    
    // Para prevenir problemas, deshabilitamos interrupciones mientras cambiamos el valor
    portENTER_CRITICAL(&dimmer_spinlock);
    
    // Update the appropriate channel
    if (channel == 1) {
        dimmer_level_ch1 = level;
    } else if (channel == 2) {
        dimmer_level_ch2 = level;
    }
    
    portEXIT_CRITICAL(&dimmer_spinlock);
    
    // Log the change
    ESP_LOGI(TAG, "Dimmer channel %d set to %d%%", channel, level);
}

// Initialize the dimmer module - MODIFICADA PARA ESTABILIDAD
void dimmer_init(void) {
    // Create queue for zero crossing events
    zero_cross_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    
    // Initialize GPIO pins
    dimmer_gpio_init();
    
    // Initialize timer
    dimmer_timer_init();
    
    // Inicialmente desactivamos el dimmer para estabilidad
    dimmer_enabled = false;
    
    // Esperar un segundo para que el sistema se estabilice
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Create dimmer control task
    xTaskCreate(dimmer_control_task, "dimmer_control_task", 2048, NULL, 10, NULL);
    
    // Esperar otro segundo
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Ahora habilitamos el dimmer
    dimmer_enabled = true;
    
    // Establecer nivel de potencia inicial (100% para estabilidad)
    dimmer_level_ch1 = 100;
    
    ESP_LOGI(TAG, "AC Dimmer initialized at %d%% brightness", dimmer_level_ch1);
    ESP_LOGI(TAG, "Dimmer pins - ZC: %d, CH1: %d", DIMMER_ZERO_CROSS_PIN, DIMMER_CH1_PIN);
}


// Modified DHT reader task to include dimmer status
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
    cJSON *dimmer = cJSON_CreateObject();
    
    cJSON_AddItemToObject(root, "device", device);
    cJSON_AddItemToObject(root, "sensors", sensors);
    cJSON_AddItemToObject(root, "dimmer", dimmer);
    
    cJSON_AddNumberToObject(device, "uptime", xTaskGetTickCount() * portTICK_RATE_MS);
    cJSON_AddStringToObject(device, "hardware", mac_str);
    cJSON_AddStringToObject(device, "firmware", esp_get_idf_version());  
    
    cJSON_AddStringToObject(sensors, "temperature", temperatureStr);
    cJSON_AddStringToObject(sensors, "humidity", humidityStr);
    
    // Para prevenir problemas de concurrencia, obtenemos los valores en una sección crítica
    portENTER_CRITICAL(&dimmer_spinlock);
    uint8_t ch1_level = dimmer_level_ch1;
    uint8_t ch2_level = dimmer_level_ch2;
    bool is_enabled = dimmer_enabled;
    portEXIT_CRITICAL(&dimmer_spinlock);
    
    cJSON_AddNumberToObject(dimmer, "channel1", ch1_level);
    cJSON_AddNumberToObject(dimmer, "channel2", ch2_level);
    cJSON_AddBoolToObject(dimmer, "enabled", is_enabled);
    
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

    /* Initialize the dimmer module */
    dimmer_init();
    
    // Iniciar con WiFi y conexión MQTT
    initialise_wifi();

    /* Set MQTT Topic name to the global variable. */ 
    globalMqttTopic = "clients/" CLIENT_IDENTIFIER "/sensor/dth11";
    globalMqttTopicLength = ( uint16_t ) strlen( globalMqttTopic );

    xTaskCreate(&aws_iot_demo, "aws_iot_demo", 4096, NULL, 5, NULL );
    
    // Después de iniciar todo, esperamos a que se establezcan las conexiones
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // 30 segundos
    
    // Una vez que todo esté estable, podemos intentar cambiar el nivel del dimmer
    ESP_LOGI(TAG, "Setting dimmer to 80%% after system stabilization");
    set_dimmer_level(1, 35);
}