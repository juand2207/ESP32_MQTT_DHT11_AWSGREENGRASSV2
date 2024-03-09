/*
* AWS IoT Device SDK for Embedded C 202103.00
* Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of
* this software and associated documentation files (the "Software"), to deal in
* the Software without restriction, including without limitation the rights to
* use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
* the Software, and to permit persons to whom the Software is furnished to do so,
* subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
* FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
* COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
* IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef MQTT_DEMO_MUTUAL_AUTH_H_
#define MQTT_DEMO_MUTUAL_AUTH_H_

/**************************************************/
/******* DO NOT CHANGE the following order ********/
/**************************************************/

/* Logging related header files are required to be included in the following order:
* 1. Include the header file "logging_levels.h".
* 2. Define LIBRARY_LOG_NAME and  LIBRARY_LOG_LEVEL.
* 3. Include the header file "logging_stack.h".
*/

/* Include header that defines log levels. */
#include "logging_levels.h"

/* Logging configuration for the Demo. */
#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME     "MQTT_DEMO"
#endif
#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif

#include "logging_stack.h"

/************ End of logging configuration ****************/


/**
* @brief Details of the MQTT broker to connect to.
*
* @note Your AWS IoT Core endpoint can be found in the AWS IoT console under
* Settings/Custom Endpoint, or using the describe-endpoint API.
*
*/
#ifndef AWS_IOT_ENDPOINT
    #define AWS_IOT_ENDPOINT    CONFIG_MQTT_BROKER_ENDPOINT
#endif

/**
* @brief AWS IoT MQTT broker port number.
*
* In general, port 8883 is for secured MQTT connections.
*
* @note Port 443 requires use of the ALPN TLS extension with the ALPN protocol
* name. When using port 8883, ALPN is not required.
*/
#ifndef AWS_MQTT_PORT
    #define AWS_MQTT_PORT    ( CONFIG_MQTT_BROKER_PORT )
#endif

/**
* @brief The username value for authenticating client to MQTT broker when
* username/password based client authentication is used.
*
* Refer to the AWS IoT documentation below for details regarding client
* authentication with a username and password.
* https://docs.aws.amazon.com/iot/latest/developerguide/custom-authentication.html
* As mentioned in the link above, an authorizer setup needs to be done to use
* username/password based client authentication.
*
* @note AWS IoT message broker requires either a set of client certificate/private key
* or username/password to authenticate the client. If this config is defined,
* the username and password will be used instead of the client certificate and
* private key for client authentication.
*
* #define CLIENT_USERNAME    "...insert here..."
*/

/**
* @brief The password value for authenticating client to MQTT broker when
* username/password based client authentication is used.
*
* Refer to the AWS IoT documentation below for details regarding client
* authentication with a username and password.
* https://docs.aws.amazon.com/iot/latest/developerguide/custom-authentication.html
* As mentioned in the link above, an authorizer setup needs to be done to use
* username/password based client authentication.
*
* @note AWS IoT message broker requires either a set of client certificate/private key
* or username/password to authenticate the client.
*
* #define CLIENT_PASSWORD    "...insert here..."
*/

/**
* @brief MQTT client identifier.
*
* No two clients may use the same client identifier simultaneously.
*/
#ifndef CLIENT_IDENTIFIER
    #define CLIENT_IDENTIFIER    CONFIG_MQTT_CLIENT_IDENTIFIER
#endif

/**
* @brief Size of the network buffer for MQTT packets.
*/
#define NETWORK_BUFFER_SIZE       ( CONFIG_MQTT_NETWORK_BUFFER_SIZE )

/**
* @brief The name of the operating system that the application is running on.
* The current value is given as an example. Please update for your specific
* operating system.
*/
#define OS_NAME                   "FreeRTOS"

/**
* @brief The version of the operating system that the application is running
* on. The current value is given as an example. Please update for your specific
* operating system version.
*/
#define OS_VERSION                tskKERNEL_VERSION_NUMBER

/**
* @brief The name of the hardware platform the application is running on. The
* current value is given as an example. Please update for your specific
* hardware platform.
*/
#define HARDWARE_PLATFORM_NAME    CONFIG_HARDWARE_PLATFORM_NAME

/**
* @brief The name of the MQTT library used and its version, following an "@"
* symbol.
*/
#include "core_mqtt.h"
#define MQTT_LIB    "core-mqtt@" MQTT_LIBRARY_VERSION

/*-----------------------------------------------------------*/

/* OpenSSL sockets transport implementation. */
#include "network_transport.h"

static const char * globalMqttTopic = "clients/" CLIENT_IDENTIFIER "/hello/world";
static uint16_t globalMqttTopicLength = ( uint16_t ) ( sizeof( globalMqttTopic ) - 1 );

/**
* @brief Delay in seconds between two iterations of subscribePublishLoop().
*/
#define MQTT_SUBPUB_LOOP_DELAY_SECONDS      ( 5U )

/**
* @brief Initializes the MQTT library.
*
* @param[in] pMqttContext MQTT context pointer.
* @param[in] pNetworkContext The network context pointer.
*
* @return EXIT_SUCCESS if the MQTT library is initialized;
* EXIT_FAILURE otherwise.
*/
int initializeMqtt( MQTTContext_t * pMqttContext,
                        NetworkContext_t * pNetworkContext );

/**
* @brief Connect to MQTT broker with reconnection retries.
*
* If connection fails, retry is attempted after a timeout.
* Timeout value will exponentially increase until maximum
* timeout value is reached or the number of attempts are exhausted.
*
* @param[out] pNetworkContext The output parameter to return the created network context.
*
* @return EXIT_FAILURE on failure; EXIT_SUCCESS on successful connection.
*/
int connectToServerWithBackoffRetries( NetworkContext_t * pNetworkContext );

/**
* @brief A function that connects to MQTT broker,
* subscribes a topic, publishes to the same
* topic MQTT_PUBLISH_COUNT_PER_LOOP number of times, and verifies if it
* receives the Publish message back.
*
* @param[in] pMqttContext MQTT context pointer.
* @param[in,out] pClientSessionPresent Pointer to flag indicating if an
* MQTT session is present in the client.
*
* @return EXIT_FAILURE on failure; EXIT_SUCCESS on success.
*/
int subscribePublishLoop( MQTTContext_t * pMqttContext,
                        bool * pClientSessionPresent,
                        const char * pcTopicFilter,
                        uint16_t usTopicFilterLength,
                        const char * pcPayload,
                        uint16_t payloadLength );

#endif /* ifndef MQTT_DEMO_MUTUAL_AUTH_H_ */
