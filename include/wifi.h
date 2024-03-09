#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"

/* This module uses WiFi configuration that you can set via project configuration menu */
#define WORKSHOP_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define WORKSHOP_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define WORKSHOP_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

void initialise_wifi(void);
