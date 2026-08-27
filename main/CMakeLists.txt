idf_component_register(SRCS "myTelebot.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES wifi_sta my_ping esp_http_client lwip esp_netif mbedtls)
target_add_binary_data(${COMPONENT_LIB} "telegram_root.pem" TEXT)
