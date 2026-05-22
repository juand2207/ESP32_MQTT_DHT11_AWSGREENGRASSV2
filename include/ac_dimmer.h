#ifndef AC_DIMMER_H
#define AC_DIMMER_H

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

// Definiciones de pines para el módulo dimmer AC
#define ZERO_CROSS_PIN GPIO_NUM_5   // Pin Z-C (ajustar según tu conexión)
#define DIMMER_PIN GPIO_NUM_4       // Pin D1 (ajustar según tu conexión)

// Constantes para el control del dimmer
#define MAX_DIMMING_LEVEL 100      // Nivel máximo de dimming (%)
#define AC_FREQ 60                 // Frecuencia AC (Hz) - ajustar a 50Hz si es necesario
#define HALF_PERIOD_US (1000000 / (AC_FREQ * 2))  // Duración de medio ciclo en microsegundos

// Estructura para controlador PID
typedef struct {
    double kp;             // Constante proporcional
    double ki;             // Constante integral
    double kd;             // Constante derivativa
    double setpoint;       // Temperatura objetivo
    double input;          // Temperatura actual
    double output;         // Salida (0-100)
    double last_error;     // Último error calculado
    double integral;       // Acumulador integral
    double derivative;     // Término derivativo
    double last_input;     // Último valor de entrada
    double output_min;     // Límite mínimo de salida
    double output_max;     // Límite máximo de salida
    double max_safe_temp;  // Temperatura máxima segura
    uint32_t last_time;    // Último tiempo de cálculo
} pid_controller_t;

// Funciones para el dimmer AC con cruce por cero
void ac_dimmer_init(void);
void ac_dimmer_set_value(uint8_t value);

// Funciones para el controlador PID
void pid_init(pid_controller_t *pid, double kp, double ki, double kd, double setpoint, double output_min, double output_max, double max_safe_temp);
double pid_compute(pid_controller_t *pid, double input);

#endif /* AC_DIMMER_H */