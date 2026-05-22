#include "ac_dimmer.h"
#include "driver/timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "AC_DIMMER";

// Variables globales para el control del dimmer
volatile uint8_t dimming_level = 0;  // 0-100%
volatile bool zero_cross_detected = false;
esp_timer_handle_t dimmer_timer;

// Rutina de interrupción para la detección de cruce por cero
static void IRAM_ATTR zero_cross_isr(void* arg) {
    zero_cross_detected = true;
    
    // Calcular el tiempo de retraso para el disparo del triac
    // Un valor de dimming de 0 significa potencia máxima (disparo inmediato)
    // Un valor de dimming de 100 significa potencia mínima (disparo tardío o ninguno)
    int32_t delay_us = (HALF_PERIOD_US * dimming_level) / 100;
    
    // Si el nivel de dimming es muy alto, simplemente no disparamos
    if (dimming_level >= 99) {
        return;
    }
    
    // Programar el timer para disparar el triac
    esp_timer_start_once(dimmer_timer, delay_us);
}

// Función de callback para el timer
static void IRAM_ATTR dimmer_timer_callback(void* arg) {
    // Activar el pin del dimmer
    gpio_set_level(DIMMER_PIN, 1);
    
    // Mantener el pulso por un corto tiempo (50µs)
    esp_rom_delay_us(50);
    
    // Desactivar el pin del dimmer
    gpio_set_level(DIMMER_PIN, 0);
}

// Inicialización del controlador del dimmer AC
void ac_dimmer_init(void) {
    ESP_LOGI(TAG, "Initializing AC dimmer with zero crossing detection");
    
    // Configurar el pin de cruce por cero como entrada
    gpio_config_t io_conf_zc = {
        .intr_type = GPIO_INTR_POSEDGE,  // Interrupción en flanco positivo
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << ZERO_CROSS_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf_zc);
    
    // Configurar el pin del dimmer como salida
    gpio_config_t io_conf_dimmer = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << DIMMER_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf_dimmer);
    
    // Inicialmente apagar el dimmer
    gpio_set_level(DIMMER_PIN, 0);
    
    // Crear el timer para el control del disparo
    esp_timer_create_args_t timer_args = {
        .callback = &dimmer_timer_callback,
        .name = "dimmer_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &dimmer_timer));
    
    // Instalar el handler de la interrupción de cruce por cero
    gpio_install_isr_service(0);
    gpio_isr_handler_add(ZERO_CROSS_PIN, zero_cross_isr, NULL);
    
    ESP_LOGI(TAG, "AC dimmer initialized successfully");
}

// Establecer valor del dimmer (0-100%)
void ac_dimmer_set_value(uint8_t value) {
    if (value > 100) {
        value = 100;
    }
    
    dimming_level = value;
    ESP_LOGI(TAG, "Setting dimmer level to: %d%%", dimming_level);
}

// Inicializar el controlador PID
void pid_init(pid_controller_t *pid, double kp, double ki, double kd, double setpoint, double output_min, double output_max, double max_safe_temp) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->setpoint = setpoint;
    pid->output_min = output_min;
    pid->output_max = output_max;
    pid->max_safe_temp = max_safe_temp;
    pid->last_error = 0;
    pid->integral = 0;
    pid->derivative = 0;
    pid->last_input = 0;
    pid->output = 0;
    pid->last_time = esp_timer_get_time() / 1000; // Convertir a milisegundos
    
    ESP_LOGI(TAG, "PID controller initialized with Kp=%.2f, Ki=%.2f, Kd=%.2f, Setpoint=%.2f", 
             pid->kp, pid->ki, pid->kd, pid->setpoint);
}

// Calcular salida del controlador PID
double pid_compute(pid_controller_t *pid, double input) {
    // Verificar si la temperatura está por encima del límite seguro
    if (input > pid->max_safe_temp) {
        ESP_LOGW(TAG, "Temperature above safety limit! Turning off heater.");
        return 100; // Apagar la calefacción (100% dimming = mínima potencia)
    }
    
    // Obtener el tiempo actual en milisegundos
    uint32_t now = esp_timer_get_time() / 1000;
    double time_change = (now - pid->last_time);
    
    // Si ha pasado muy poco tiempo, no recalcular
    if (time_change < 100) { // Menos de 100ms
        return pid->output;
    }
    
    // Calcular error
    double error = pid->setpoint - input;
    ESP_LOGD(TAG, "PID Error: %.2f (Setpoint: %.2f, Input: %.2f)", error, pid->setpoint, input);
    
    // Calcular término integral (con anti-windup)
    pid->integral += (pid->ki * error * time_change / 1000.0);
    if (pid->integral > pid->output_max) pid->integral = pid->output_max;
    else if (pid->integral < pid->output_min) pid->integral = pid->output_min;
    
    // Calcular término derivativo (sobre el cambio de la medida, no del error)
    pid->derivative = (input - pid->last_input) / (time_change / 1000.0);
    
    // Calcular salida PID
    pid->output = (pid->kp * error) + pid->integral - (pid->kd * pid->derivative);
    
    // Limitar la salida
    if (pid->output > pid->output_max) pid->output = pid->output_max;
    else if (pid->output < pid->output_min) pid->output = pid->output_min;
    
    // Actualizar variables para la próxima iteración
    pid->last_input = input;
    pid->last_time = now;
    
    ESP_LOGD(TAG, "PID Output: %.2f (P=%.2f, I=%.2f, D=%.2f)", 
             pid->output, (pid->kp * error), pid->integral, (-pid->kd * pid->derivative));
    
    return pid->output;
}