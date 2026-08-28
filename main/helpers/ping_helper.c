esp_err_t ping_callback()
{
    #if DEBUG
    ESP_LOGI(TAG, "Ping callback");
    #endif
    ping_stop();
    char body[800];

    // Check if abort is required
    if(ping_abort && ping_status == 0)
    {
        #if DEBUG
        ESP_LOGE(TAG, "CRITICAL! Can't reach server, but it is running; aborting...");
        #endif // DEBUG
        snprintf(body, sizeof(body),
             "{\"chat_id\":%s,\"message_id\":%lld,\"text\":\"Server can't be reached.\nBut server running.\nABORTING...\"}", AUTHORIZED_CHAT_ID_STR, edit_id);
        edit_message(body);
        abort();
    }
    ping_abort = false;

    if(ping_abort && ping_status == 1)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Can't reach server because it is NOT running; NOT aborting...");
        #endif // DEBUG

        snprintf(body, sizeof(body),
             "{\"chat_id\":%s,\"message_id\":%lld,\"text\":\"Server can't be reached.\nServer not running...\", %s}", AUTHORIZED_CHAT_ID_STR, edit_id, MENU_KEYBOARD);

    }
    else
    {
        snprintf(body, sizeof(body),
             "{\"chat_id\":%s,\"message_id\":%lld,\"text\":\"%s\", %s}", AUTHORIZED_CHAT_ID_STR, edit_id, (ping_status == 0 ? "Running" : "Sleepy"), MENU_KEYBOARD);

    }

    return edit_message(body);
}

void test_on_ping_success(void* args, void* cb_args)
{
    #if DEBUG
    ESP_LOGI(TAG, "Ping success");
    #endif
    ping_status = 0;
}

void test_on_ping_timeout(void* args, void* cb_args)
{
    #if DEBUG
    ESP_LOGI(TAG, "Ping timeout");
    #endif
    ping_status = 1;
}
