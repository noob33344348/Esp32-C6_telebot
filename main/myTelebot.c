// Premade libraries
#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "esp_crt_bundle.h"
#include <lwip/sockets.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Custom wifi driver (station mode only)
#include "wifi_sta.h"

// Environment variables of telegram and server
#include "environment.h"

// Replies texts and authorized chat ids
#include "bot_settings.h"

// Define the enum for the commands used
#include "my_types.h"

// Simple ping functions implementations
#include "my_ping.h"

// Documentation of IMPLEMENTATIONS
#include "myTelebot.h"

// Debugging
#define DEBUG true
#define TAG "Telebot"

// STATICS

// Callback for reading when poolling for updates
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt == NULL) {
        return ESP_FAIL;
    }

    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }

    http_buffer_t *http_buffer = (http_buffer_t *)evt->user_data;

    // Check if buffer is big enough
    size_t required = http_buffer->length + evt->data_len + 1;
    if(http_buffer->capacity < required) // Expand buffer
    {
        // Calculate new capacity
        size_t new_capacity = http_buffer->capacity == 0 ? 512 : http_buffer->capacity * 2;
        while(new_capacity < required)
            new_capacity *= 2;

        // Realloc
        char *new_buffer = realloc(http_buffer->buffer, new_capacity);

        if(new_buffer == NULL)
        {
            #if DEBUG
            ESP_LOGE(TAG, "Out of memory while reading http response");
            #endif
            return ESP_ERR_NO_MEM;
        }
        http_buffer->buffer = new_buffer;
        http_buffer->capacity = new_capacity;
    }

    // Read http response
    memcpy(http_buffer->buffer + http_buffer->length, evt->data, evt->data_len);

    http_buffer->length += evt->data_len;
    http_buffer->buffer[http_buffer->length] = '\0';

    #if DEBUG
    ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA: %d bytes", evt->data_len);
    ESP_LOGI(TAG, "Data: %.*s",
                evt->data_len,
                (char *)evt->data);
    #endif
    return ESP_OK;
}


// Tracks which update has been recived
static int64_t update_id = 0;
static int64_t edit_id = -1;
static int8_t ping_status = -1; // -1: no ping, 0: server on, 1: server off
// If enabled abort program if ping failes
static volatile bool ping_abort = false;
// How much time to spend polling (s)
static uint8_t poll_timeout = MAX_POLL;

// MAIN
void app_main(void)
{
    #if DEBUG
    ESP_LOGI(TAG, "Telebot started");

    size_t cert_len =
    telegram_root_pem_end - telegram_root_pem_start;
    ESP_LOGI("TLS", "CA certificate size: %zu", cert_len);
    ESP_LOGI("TLS", "CA: %s", telegram_root_pem_start);
    #endif

    // Return value
    esp_err_t ret;

    // Wifi connection
    ret = my_wifi_init();
    for(uint8_t i = 0; i < MAX_WIFI_TRIES && ret != ESP_OK; i++)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
        #endif

        if (i+1 == MAX_WIFI_TRIES)
        {
            #if DEBUG
            ESP_LOGE(TAG, "CRITICAL! Failed to wifi_init");
            #endif // DEBUG
            abort();
        }

        ret = my_wifi_init();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ret = my_wifi_start();
    for(uint8_t i = 0; i < MAX_WIFI_TRIES && ret != ESP_OK; i++)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
        #endif
        if (i+1 == MAX_WIFI_TRIES)
        {
            #if DEBUG
            ESP_LOGE(TAG, "CRITICAL! Failed to wifi_start");
            #endif // DEBUG
            abort();
        }

        ret = my_wifi_start();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // Main loop
    while (1)
    {
        #if DEBUG
        ESP_LOGI(TAG, "Entered main task");
        ESP_LOGI(TAG, "Heap start: %d", esp_get_free_heap_size());
        #endif

        // Check if connected
        #if DEBUG
        ESP_LOGI(TAG, "Checking if connected");
        #endif // DEBUG
        for(uint8_t i = 0; i < MAX_WIFI_TRIES && !my_wifi_status(); i++)
        {
            if (i+1 == MAX_WIFI_TRIES)
            {
                #if DEBUG
                ESP_LOGE(TAG, "CRITICAL! Failed to connect - %s", esp_err_to_name(ret));
                #endif // DEBUG
                abort();
            }

            ret = my_wifi_reconnect();
            if(ret == ESP_OK)
            {
                #if DEBUG
                ESP_LOGI(TAG, "Reconnecting...");
                #endif
            }
            else
            {
                #if DEBUG
                ESP_LOGE(TAG, "Failed reconnection instance...");
                #endif
            }

            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        // Check if synced
        #if DEBUG
        ESP_LOGI(TAG, "Checking if synced");
        #endif // DEBUG
        for(uint8_t i = 0; i < MAX_SYNC_TRIES && !my_sync(); i++)
        {
            if (i+1 == MAX_SYNC_TRIES)
            {
                #if DEBUG
                ESP_LOGE(TAG, "CRITICAL! Failed to sync");
                #endif // DEBUG
                abort();
            }
        }

        // Look for updates
        #if DEBUG
        ESP_LOGI(TAG, "Looking for updates");
        #endif
        http_buffer_t http_buffer = {
            .buffer = NULL,
            .length = 0,
            .capacity = 0
        };
        ret = poll_updates(&http_buffer, http_event_handler);

        if(ret == ESP_OK) // Elaborate response
        {
            #if DEBUG
            ESP_LOGI(TAG, "Response length: %d", http_buffer.length);
            ESP_LOGI(TAG, "Parsing");
            #endif
            parse_t parse_out = parse(http_buffer.buffer);

            for(uint32_t i=0; i<parse_out.count; i++)
            {
                ret = elaborate(parse_out.reply[i]);
                #if DEBUG
                ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));
                #endif // DEBUG

                // Free callback_id
                if(parse_out.reply[i].callback_id != NULL)
                {
                    #if DEBUG
                    ESP_LOGI(TAG, "Trying to free callback_id");
                    #endif
                    free(parse_out.reply[i].callback_id);
                    #if DEBUG
                    ESP_LOGI(TAG, "Freed successefull!");
                    #endif
                }

                // Reduce poll_timeout
                if(poll_timeout > MIN_POLL)
                {
                    poll_timeout = MIN_POLL;
                    #if DEBUG
                    ESP_LOGI(TAG, "Reduced poll_timeout: %us", poll_timeout);
                    #endif // DEBUG
                }
            }

            // Free reply
            if(parse_out.reply != NULL)
            {
                #if DEBUG
                ESP_LOGI(TAG, "Trying to free parse_out.reply");
                #endif
                free(parse_out.reply);
                #if DEBUG
                ESP_LOGI(TAG, "Freed successefull!");
                #endif
            }

            // Increase poll_timeout if no messages arrive
            if(parse_out.count == 0 && poll_timeout < MAX_POLL)
            {
                poll_timeout *= 2;
                if (poll_timeout > MAX_POLL)
                    poll_timeout = MAX_POLL;
                #if DEBUG
                ESP_LOGI(TAG, "Increased poll_timeout: %us", poll_timeout);
                #endif
            }

        }

        // Free http_buffer
        if(http_buffer.buffer != NULL)
            free(http_buffer.buffer);

        // Elaborate ping
        if(ping_status > -1)
        {
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating ping");
            #endif // DEBUG
            ret = ping_callback();
            #if DEBUG
            ESP_LOGI(TAG, "Ping callback status: %s", esp_err_to_name(ret));
            #endif // DEBUG

            ping_status = -1;
        }

        #if DEBUG
        ESP_LOGI(TAG, "Heap end: %d", esp_get_free_heap_size());
        #endif
    }
}

// IMPLEMENTATIONS
bool my_sync(void)
{
    static bool synced = false;
    static bool init = false;
    if(!synced)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Trying to sync");
        #endif

        if(!init)
        {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
            esp_netif_sntp_init(&config);
            init = true;
        }

        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000)) != ESP_OK) {
            #if DEBUG
            ESP_LOGE(TAG, "Failed to update system time within 20s timeout");
            #endif
            return false;
        }
        #if DEBUG
        time_t now;
        time(&now);

        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf),
                 "%c", &timeinfo);

        ESP_LOGI(TAG, "Current time: %s", strftime_buf);
        #endif
        synced = true;
        return true;
    }
    else
        return true;
}

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

parse_t parse(char *response)
{
    parse_t ret = {
        .reply = NULL,
        .count = 0
    };
    cJSON *root = cJSON_Parse(response);

    // Check for errors in response
    if (root == NULL)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed - HTTP response - null");
        #endif
        return ret;
    }

    const cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (ok == NULL)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed - HTTP response - no 'ok'");
        #endif
        return ret;
    }

    if (!cJSON_IsTrue(ok))
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed - HTTP response - ok: false");
        #endif
        cJSON_Delete(root);
        return ret;
    }

    // Parse
    const cJSON *result = cJSON_GetObjectItem(root, "result");
    if (result == NULL)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed - HTTP response - no 'result'");
        #endif
        return ret;
    }

    ret.reply =malloc(cJSON_GetArraySize(result) * sizeof(reply_struct_t));

    cJSON *update;
    cJSON_ArrayForEach(update, result)
    {
        ret.reply[ret.count].command = NO_COMMAND;
        ret.reply[ret.count].callback_id = NULL;
        ret.reply[ret.count].message_id = -1;

        cJSON *callback = cJSON_GetObjectItem(update, "callback_query");

        // Check user authorization
        int chat_id_int;
        cJSON *message;
        if(callback == NULL) // '/start' message
        {

            message = cJSON_GetObjectItem(update, "message");
            cJSON *chat = cJSON_GetObjectItem(message, "chat");
            cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
            chat_id_int = chat_id->valueint;
        }
        else // Callbacks
        {
            message = cJSON_GetObjectItem(callback, "message");
            cJSON *chat = cJSON_GetObjectItem(message, "chat");
            cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
            chat_id_int = chat_id->valueint;
        }

        // Update managed actions
        update_id = cJSON_GetObjectItem(update, "update_id")->valueint +1;
        cJSON *message_id = cJSON_GetObjectItem(message, "message_id");
        ret.reply[ret.count].message_id = message_id->valueint;
        #if DEBUG
        ESP_LOGI(TAG, "New update id: %lld", update_id);
        #endif

        if(chat_id_int != AUTHORIZED_CHAT_ID_INT) // Unauthorized user
        {
            #if DEBUG
            ESP_LOGI(TAG, "Non authorized chat id");
            #endif
            continue;
        }
        // Authorized user

        // Return requested action
        char* text = cJSON_GetObjectItem(message, "text")->valuestring;


        char *data = NULL;
        char *id = NULL;
        if(callback != NULL)
        {
            // Data (in case of a callback query)
            cJSON *id_data = cJSON_GetObjectItem(callback, "data");
            data = id_data->valuestring;

            // Message id (in case of a callback query)
            cJSON *id_item = cJSON_GetObjectItem(callback, "id");
            id = id_item->valuestring;

            ret.reply[ret.count].callback_id = malloc(strlen(id)+1);
            if(ret.reply[ret.count].callback_id == NULL)
            {
                #if DEBUG
                ESP_LOGE(TAG, "Failed - Couldn't malloc for callback_id");
                ESP_LOGI(TAG, "Heap: %d", esp_get_free_heap_size());
                #endif // DEBUG

                // Skip iteration
                ret.reply[ret.count].callback_id = NULL;
                continue;
            }
            strcpy(ret.reply[ret.count].callback_id, id);
        }

        // Message data
        if(!strcmp(text, "/start"))
        {
            ret.reply[ret.count].command = START;
        }
        else if(callback != NULL)
        {
            if(!strcmp("home",data))
                ret.reply[ret.count].command = HOME;
            else if(!strcmp("status",data))
                ret.reply[ret.count].command = STATUS;
            else if(!strcmp("wake",data))
                ret.reply[ret.count].command = WAKE;
            else if(!strcmp("poweroff_menu",data))
                ret.reply[ret.count].command = POWEROFF_MENU;
            else if(!strcmp("poweroff_now",data))
                ret.reply[ret.count].command = POWEROFF_NOW;
            else if(!strcmp("poweroff_30",data))
                ret.reply[ret.count].command = POWEROFF_30;
            else if(!strcmp("poweroff_60",data))
                ret.reply[ret.count].command = POWEROFF_60;
            else if(!strcmp("poweroff_120",data))
                ret.reply[ret.count].command = POWEROFF_120;
            else if(!strcmp("reboot_menu",data))
                ret.reply[ret.count].command = REBOOT_MENU;
            else if(!strcmp("reboot_now",data))
                ret.reply[ret.count].command = REBOOT_NOW;
            else if(!strcmp("reboot_30",data))
                ret.reply[ret.count].command = REBOOT_30;
            else if(!strcmp("reboot_60",data))
                ret.reply[ret.count].command = REBOOT_60;
            else if(!strcmp("reboot_120",data))
                ret.reply[ret.count].command = REBOOT_120;
            else
                ret.reply[ret.count].command = NO_COMMAND;
        }
        else
        {
            #if DEBUG
            ESP_LOGE(TAG, "Parsing failed - unknown command");
            #endif
        }
        ret.count++;
    }

    cJSON_Delete(root);
    return ret;
}

esp_err_t elaborate (reply_struct_t reply)
{
    esp_err_t ret = ESP_OK;
    char body[800];
    switch(reply.command)
    {
        case START:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - START");
            #endif
            ret = send_message(MENU_START MENU_TEXT MENU_KEYBOARD MENU_END);
            if(ret == ESP_OK)
                edit_id = reply.message_id+1;
            break;
        case HOME:
        {
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - HOME");
            #endif

            //TODO create body
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Going back...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld," MENU_TEXT MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        }
        case STATUS:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - STATUS");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Pinging...\"}", reply.callback_id);
            ret = answer_callback(body);
            (void)ping_go(SERVER_IP_STRING, test_on_ping_success, test_on_ping_timeout);
            break;
        case WAKE:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - WAKE");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Waking...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = wake_on_lan();
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\": \"Waking.\nPlease wait a few minutes...\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case POWEROFF_MENU:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF MENU");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Thinking hard...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body), "{\"message_id\":%lld," POWEROFF_MENU_BODY, edit_id);
            ret = edit_message(body);
            break;
        case POWEROFF_NOW:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF NOW");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = send_command("{\"action\":\"poweroff\",\"minutes\":\"0\"}");
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\":\"Powering off\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case POWEROFF_30:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF 30");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = send_command("{\"action\":\"poweroff\",\"minutes\":\"30\"}");
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\":\"Powering off in 30\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case POWEROFF_60:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF 60");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = send_command("{\"action\":\"poweroff\",\"minutes\":\"60\"}");
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\":\"Powering off in 60\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case POWEROFF_120:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF 120");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = send_command("{\"action\":\"poweroff\",\"minutes\":\"120\"}");
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\":\"Powering off in 120\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case REBOOT_MENU:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT MENU");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Thinking hard...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body), "{\"message_id\":%lld," REBOOT_MENU_BODY, edit_id);
            ret = edit_message(body);
            break;
        case REBOOT_NOW:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT NOW");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = send_command("{\"action\":\"reboot\",\"minutes\":\"0\"}");
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\":\"Rebooting\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case REBOOT_30:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT 30");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = send_command("{\"action\":\"reboot\",\"minutes\":\"30\"}");
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\":\"Rebooting in 30\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case REBOOT_60:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT 60");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = send_command("{\"action\":\"reboot\",\"minutes\":\"60\"}");
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\":\"Rebooting in 60\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case REBOOT_120:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT 120");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            if(ret != ESP_OK)
                return ret;
            ret = send_command("{\"action\":\"reboot\",\"minutes\":\"120\"}");
            if(ret != ESP_OK)
                return ret;
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld, \"text\":\"Rebooting in 120\", " MENU_KEYBOARD MENU_END, edit_id);
            ret = edit_message(body);
            break;
        case ERROR:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - ERROR");
            #endif
            break;
        case NO_COMMAND:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - NO COMMAND");
            #endif
            break;
        #if DEBUG
        default:
            ESP_LOGI(TAG, "Elaborating - UNKNOWN");
            break;
        #endif
    }
    return ret;
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

esp_err_t wake_on_lan(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed - Coudn't setup socket");
        #endif // DEBUG
        char body[800];
        snprintf(body, sizeof(body),
         "{\"chat_id\":%s,\"message_id\":%lld,\"text\":\"Failed to wakeup\", %s}", AUTHORIZED_CHAT_ID_STR, edit_id, MENU_KEYBOARD);
        return edit_message(body);
    }

    // Allow broadcast
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(7);
    dest_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    // Build WoL magic packet
    uint8_t wol_packet[102];
    memset(wol_packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(&wol_packet[6 + i * 6], MAC_ADDRESS, 6);
    }

    sendto(sock, wol_packet, sizeof(wol_packet), 0,
           (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    #if DEBUG
    ESP_LOGI(TAG, "Magic packet sent");
    #endif
    close(sock);
    return ESP_OK;
}

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
    }
    else
    {
        snprintf(body, sizeof(body),
                 MENU_START "\"message_id\":%lld, \"text\":\"Command failed.\", " MENU_KEYBOARD MENU_END, edit_id);
        edit_message(body);
    }


}
