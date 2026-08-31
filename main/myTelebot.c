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
#include "env/environment.h"

// Replies texts and authorized chat ids
#include "env/bot_settings.h"

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

// Implementations organized by category
#include "helpers/ping_helper.c"
#include "helpers/api_calls.c"
#include "helpers/generic_helper.c"


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

    // Set min ps mode
    set_my_wifi_ps(1);

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
                    // Remove power saving mode
                    if(poll_timeout == MAX_POLL)
                        set_my_wifi_ps(0);

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
                {
                    poll_timeout = MAX_POLL;
                    set_my_wifi_ps(1);
                }
                #if DEBUG
                ESP_LOGI(TAG, "Increased poll_timeout: %us", poll_timeout);
                #endif
            }

        }

        // Free http_buffer
        if(http_buffer.buffer != NULL)
            free(http_buffer.buffer);

        #if DEBUG
        ESP_LOGI(TAG, "Heap end: %d", esp_get_free_heap_size());
        #endif
    }
}

// IMPLEMENTATIONS
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
            (void)ping_go(SERVER_IP_STRING, test_on_ping_success, test_on_ping_timeout);
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Pinging...\"}", reply.callback_id);
            ret = answer_callback(body);
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
