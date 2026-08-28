esp_err_t poll_updates(http_buffer_t *buffer, void *callback)
{
    // Setup https connection
    char url_temp[512];

    snprintf(url_temp, sizeof(url_temp),
             BASE_URL "/getUpdates?offset=%lld&timeout=%d",
             (long long)update_id, poll_timeout);

    static esp_http_client_handle_t client = NULL;
    if(client == NULL)// Setup https connection
    {
        const esp_http_client_config_t config = {
            .url = url_temp,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .method = HTTP_METHOD_GET,
            .timeout_ms = (poll_timeout + 3) * 1000,
            .event_handler = callback,
            .user_data = buffer,
            .keep_alive_enable = true
        };
        client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    // Always change url
    esp_http_client_set_url(client, url_temp);

    return api_call_helper(&client, manage_error_telegram_api, NULL);
}

esp_err_t send_message(const char *body)
{
    #if DEBUG
    ESP_LOGI(TAG, "Sending message...");
    #endif

    static esp_http_client_handle_t client = NULL;
    if(client == NULL)// Setup https connection
    {
        const esp_http_client_config_t config = {
            .url = BASE_URL "/sendMessage",
            .crt_bundle_attach = esp_crt_bundle_attach,
            .method = HTTP_METHOD_POST,
            .timeout_ms = GENERAL_HTTP_TIMEOUT,
            .keep_alive_enable = true
        };
        client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }


    return api_call_helper(&client, manage_error_telegram_api, body);
}

esp_err_t answer_callback(const char *body)
{
    // Setup https connection
    #if DEBUG
    ESP_LOGI(TAG, "Answering callback...");
    #endif

    static esp_http_client_handle_t client = NULL;
    if(client == NULL)// Setup https connection
    {
        const esp_http_client_config_t config = {
            .url = BASE_URL "/answerCallbackQuery",
            .crt_bundle_attach = esp_crt_bundle_attach,
            .method = HTTP_METHOD_POST,
            .timeout_ms = GENERAL_HTTP_TIMEOUT,
            .keep_alive_enable = true
        };
        client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }


    return api_call_helper(&client, manage_error_telegram_api, body);
}

esp_err_t edit_message(const char *body)
{
    // Setup https connection
    #if DEBUG
    ESP_LOGI(TAG, "Editing message...");
    #endif
    if(edit_id == -1)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Main message not found");
        #endif
        manage_error_telegram_api(ESP_FAIL);
        return ESP_FAIL;
    }

    static esp_http_client_handle_t client = NULL;
    if(client == NULL)// Setup https connection
    {
        const esp_http_client_config_t config = {
            .url = BASE_URL "/editMessageText",
            .crt_bundle_attach = esp_crt_bundle_attach,
            .method = HTTP_METHOD_POST,
            .timeout_ms = GENERAL_HTTP_TIMEOUT,
            .keep_alive_enable = true
        };
        client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    return api_call_helper(&client, manage_error_telegram_api, body);
}

esp_err_t send_command(const char *body)
{
    #if DEBUG
    ESP_LOGI(TAG, "Sending command...");
    #endif

    static esp_http_client_handle_t client = NULL;
    if(client == NULL) // Setup https connection
    {
        const esp_http_client_config_t config = {
            .url = SERVER_API_URL,
            .method = HTTP_METHOD_POST,
            .timeout_ms = SERVER_HTTP_TIMEOUT,
            .keep_alive_enable = true
        };
        client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Authorization", "Bearer " API_TOKEN);
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    return api_call_helper(&client, manage_error_server_api, body);
}

esp_err_t api_call_helper(esp_http_client_handle_t *client, void(*error_manager)(esp_err_t), const char* body)
{
    if(body != NULL)
    {
        #if DEBUG
        ESP_LOGI(TAG, "Adding body: %s", body);
        #endif // DEBUG
        esp_http_client_set_post_field(*client, body, strlen(body));
    }

    // Send message
    esp_err_t ret = esp_http_client_perform(*client);

    // Manage errors
    #if DEBUG
    ESP_LOGI(TAG, "Ret: %s\nHttp status: %d", esp_err_to_name(ret), esp_http_client_get_status_code(*client));
    #endif // DEBUG
    if(ret != ESP_OK || esp_http_client_get_status_code(*client) != 200)
    {
        ret = ESP_FAIL;
        esp_http_client_cleanup(*client);
        *client = NULL;
    }
    error_manager(ret);

    return ret;
}

void manage_error_telegram_api(esp_err_t err)
{
    static uint8_t failed_telegram_api = 0;
    if(err == ESP_OK)
    {
        #if DEBUG
        ESP_LOGI(TAG, "Success - Telegram API call");
        #endif // DEBUG
        failed_telegram_api = 0;
        return;
    }

    #if DEBUG
    ESP_LOGE(TAG, "Failure: %s", esp_err_to_name(err));
    #endif // DEBUG

    failed_telegram_api++;

    if(failed_telegram_api == MAX_TELEGRAM_API_FAILURES)
    {
        #if DEBUG
        ESP_LOGE(TAG, "CRITICAL! Too many telegram API failures, aborting...");
        #endif // DEBUG
        abort();
    }
}

void manage_error_server_api(esp_err_t err)
{
    static uint8_t failed_server_api = 0;
    if(err == ESP_OK)
    {
        #if DEBUG
        ESP_LOGI(TAG, "Success - server API call");
        #endif // DEBUG
        failed_server_api = 0;
        return;
    }

    #if DEBUG
    ESP_LOGE(TAG, "Failure: %s", esp_err_to_name(err));
    #endif // DEBUG
    char body[500];
    failed_server_api++;

    if(failed_server_api == MAX_SERVER_API_FAILURES)
    {
        #if DEBUG
        ESP_LOGE(TAG, "CRITICAL! Too many failures, checking server status...");
        #endif // DEBUG

        snprintf(body, sizeof(body),
                 "{\"message_id\":%lld, \"text\": \"Coudn't contact server. \nTrying to ping...\", \"chat_id\": %s}", edit_id, AUTHORIZED_CHAT_ID_STR);
        edit_message(body);
        (void)ping_go(SERVER_IP_STRING, test_on_ping_success, test_on_ping_timeout);
        ping_abort = true;
        failed_server_api = 0;
    }
    else
    {
        snprintf(body, sizeof(body),
                 MENU_START "\"message_id\":%lld, \"text\":\"Command failed.\", " MENU_KEYBOARD MENU_END, edit_id);
        edit_message(body);
    }


}
