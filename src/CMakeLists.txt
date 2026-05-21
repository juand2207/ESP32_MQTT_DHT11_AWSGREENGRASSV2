# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources})

# embed cert
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html?highlight=target_add_binary_data#embedding-binary-data
target_add_binary_data(${COMPONENT_TARGET} "certs/client.crt" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/client.key" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/root_cert_auth.pem" TEXT)

