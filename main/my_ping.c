// Reference: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/icmp_echo.html
#include "ping/ping_sock.h"

#include "my_ping.h"

// Include for 'ping_callback'
#include "myTelebot.h"

void test_on_ping_success(void)
{
    ping_callback(true);
}

void test_on_ping_timeout(void)
{
    ping_callback(false);
}

esp_err_t ping_go(void)
{
    esp_err_t ret;

    // IP address
    ip_addr_t ip_addr;
    IP4_ADDR(&ip_addr, SERVER_IP_0, SERVER_IP_1, SERVER_IP_2, SERVER_IP_3);


    // Default config
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.ip_addr = ip_addr;
    ping_config.count = 1;

    // Set callback functions
    esp_ping_callbacks_t cbs;
    cbs.on_ping_success = test_on_ping_success;
    cbs.on_ping_timeout = test_on_ping_timeout;

    // Initialize ping session
    esp_ping_handle_t ping;
    ret = esp_ping_new_session(&ping_config, &cbs, &ping);

    if(ret != ESP_OK)
    {
        esp_ping_delete_session(ping);
        return ret;
    }

    // Start ping session
    ret = esp_ping_start(ping);
    if(ret != ESP_OK)
    {
        esp_ping_stop(ping);
        esp_ping_delete_session(ping);
        return ret;
    }

    return ret;

}

void ping_stop(void)
{
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
}
