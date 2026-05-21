# AGENTS.md

## Resumen del Proyecto

Firmware para ESP32 que:
- Lee sensor **DHT22** (temperatura/humedad)
- Publica lecturas via **MQTT** con TLS mutual auth
- Controla **dimmer AC** con detección zero-cross
- Stack: ESP-IDF + AWS IoT Device SDK + cJSON

## Comandos de compilación

```bash
# Compilar proyecto
pio run

# Subir al dispositivo (ajustar /dev/ttyUSB0 según sea necesario)
pio run --target upload --upload-port /dev/ttyUSB0

# Monitorear salida serial
pio device monitor -b 115200
```

## Archivos principales

- **Punto de entrada**: `src/main.c:396` (`app_main()`)
- **Lógica MQTT**: `src/mqtt_demo_mutual_auth.c`
- **Lógica WiFi**: `src/wifi.c`
- **Configuración de plataforma**: `platformio.ini`

## Configuración de hardware

- **Sensor DHT22**: GPIO_NUM_21
- **Zero-cross del dimmer AC**: GPIO_NUM_23
- **Canal 1 del dimmer AC**: GPIO_NUM_22

## Tópico MQTT

```
clients/<CLIENT_IDENTIFIER>/sensor/dth11
```

El CLIENT_IDENTIFIER se define en Kconfig (`src/Kconfig.projbuild`), por defecto: `testClient`

## Configuración

Los ajustes de WiFi y MQTT están en `src/Kconfig.projbuild`:
- `ESP_WIFI_SSID` / `ESP_WIFI_PASSWORD`
- `MQTT_CLIENT_IDENTIFIER`
- `MQTT_BROKER_ENDPOINT` (por defecto: `test.mosquitto.org`)
- `MQTT_BROKER_PORT` (por defecto: 8883)

## Certificados

Los certificados TLS se incrustan via `platformio.ini`:
```
board_build.embed_txtfiles = 
    src/certs/client.crt
    src/certs/client.key
    src/certs/root_cert_auth.pem
```

## Funciones principales

- `DHT_reader_task()` - Lee el sensor DHT22, retorna JSON (src/main.c:250)
- `dimmer_init()` - Inicializa el dimmer AC con detección zero-cross (src/main.c:216)
- `set_dimmer_level(canal, nivel)` - Ajusta dimmer 0-100% (src/main.c:195)
- `aws_iot_demo()` - Bucle principal de publicación MQTT (src/main.c:326)

## Dependencias

- Framework ESP-IDF (espressif32@5.3.0)
- AWS IoT Device SDK for Embedded C en `components/`
- Biblioteca cJSON para serialización JSON