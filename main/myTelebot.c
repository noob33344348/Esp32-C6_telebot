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
// Protectes each http client
static int8_t ping_status = -1;

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
    while(ret != ESP_OK)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
        #endif
        ret = my_wifi_init();
        for(volatile uint32_t i=0; i<100000; i++);

    }

    ret = my_wifi_start();
    while(ret != ESP_OK)
    {

        #if DEBUG
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
        #endif
        ret = my_wifi_start();
        for(volatile uint32_t i=0; i<100000; i++);

    }

    // Main loop
    while (1)
    {
        #if DEBUG
        ESP_LOGI(TAG, "Entered main task");
        ESP_LOGI(TAG, "Heap start: %d", esp_get_free_heap_size());
        #endif

        // Check if connected and synced
        if(!check_connection())
            break;

        if(!my_sync())
            break;

        // Elaborate ping
        if(ping_status > -1)
        {
            ping_callback();
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
        ret = pool_updates(&http_buffer, http_event_handler);

        #if DEBUG
        ESP_LOGI(TAG, "Response length: %d", http_buffer.length);
        #endif


        if(ret == ESP_OK) // Elaborate response
        {
            #if DEBUG
            ESP_LOGI(TAG, "Parsing");
            #endif
            parse_t parse_out = parse(http_buffer.buffer);

            for(uint32_t i=0; i<parse_out.count; i++)
            {
                elaborate(parse_out.reply[i]);

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
            }
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

        }
        #if DEBUG
        else
            ESP_LOGW(TAG, "Failed to retrive messages: %d", esp_err_to_name(ret));
        #endif
        free(http_buffer.buffer);

        // Wait
        #if DEBUG
        ESP_LOGI(TAG, "Heap end: %d", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Stop");
        #endif
        for(volatile uint32_t i=0; i<1000000; i++);
    }
}

// IMPLEMENTATIONS
bool check_connection(void)
{
    // Try to reconnect until it works
    esp_err_t err;
    uint32_t limit;
    for(limit = 100000; limit>0 && my_wifi_status() == false; limit--)
    {
        err = my_wifi_reconnect();
        while(err != ESP_OK)
        {
            #if DEBUG
            ESP_LOGI(TAG, "Reconnecting...");
            #endif
            for(volatile uint32_t i=0; i<1000000; i++);
            err = my_wifi_reconnect();
        }
    }
    #if DEBUG
    if(limit == 0)
        ESP_LOGE(TAG, "Failed to connect - %s", esp_err_to_name(err));
    #endif
    return my_wifi_status();
}

bool my_sync(void)
{
    static bool synced = false;
    if(!synced)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Trying to sync");
        #endif

        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
        esp_netif_sntp_init(&config);
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
            #if DEBUG
            ESP_LOGE(TAG, "CRITICAL! Failed to update system time within 10s timeout");
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

esp_err_t pool_updates(http_buffer_t *buffer, void *callback)
{
    // Setup https connection
    char url_temp[512];

    snprintf(url_temp, sizeof(url_temp),
             BASE_URL "/getUpdates?offset=%lld&timeout=30",
             (long long)update_id);

    esp_http_client_config_t pool_config = {
        .url = url_temp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 35000,
        .event_handler = callback,
        .user_data = buffer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&pool_config);

    // Perform request
    esp_err_t ret = esp_http_client_perform(client);

    #if DEBUG
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
    else
        ESP_LOGI(TAG, "Success - Status: %d",
             esp_http_client_get_status_code(client));
    #endif

    // Close connection
    esp_http_client_cleanup(client);
    return ret;
}

parse_t parse(char *response)
{
    cJSON *root = cJSON_Parse(response);

    // Check for errors in response
    parse_t ret = {
        .reply = NULL,
        .count = 0
    };

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
        ESP_LOGI(TAG, "New update id: %d", update_id);
        #endif

        if(chat_id_int != AUTHORIZED_CHAT_ID_INT) // Unauthorized user
        {
            #if DEBUG
            ESP_LOGI(TAG, "Non authorized chat id");
            #endif
        }
        else // Authorized user
        {
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
        }
        ret.count++;
    }

    cJSON_Delete(root);
    return ret;
}

esp_err_t elaborate (reply_struct_t reply)
{
    esp_err_t ret = ESP_OK;
    char body[500];
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
            if(edit_id == -1)
            {
                #if DEBUG
                ESP_LOGE(TAG, "Main message not found");
                #endif
                return ESP_FAIL;
            }
            snprintf(body, sizeof(body),
                     MENU_START "\"message_id\":%lld," MENU_TEXT MENU_KEYBOARD MENU_END, edit_id);
            edit_message(body);
            break;
        }
        case STATUS:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - STATUS");
            #endif
            if(edit_id == -1)
            {
                #if DEBUG
                ESP_LOGE(TAG, "Main message not found");
                #endif
                return ESP_FAIL;
            }

            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Pinging...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = ping_go(SERVER_IP_STRING, test_on_ping_success, test_on_ping_timeout);

            #if DEBUG
            ESP_LOGI(TAG, "Ping go: %s", esp_err_to_name(ret));
            if(ret != ESP_OK)
                ESP_LOGE(TAG, "Failed - Couldn't start a new ping session");
            #endif
            break;
        case WAKE:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - WAKE");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Waking...\"}", reply.callback_id);
            ret = answer_callback(body);
            wakeOnLan();
            #if DEBUG
            ESP_LOGI(TAG, "Body: %s", body);
            #endif
            break;
        case POWEROFF_MENU:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF MENU");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Thinking hard...\"}", reply.callback_id);
            ret = answer_callback(body);
            snprintf(body, sizeof(body), "{\"message_id\":%lld," POWEROFF_MENU_BODY, edit_id);
            #if DEBUG
            ESP_LOGI(TAG, "Body: %s", body);
            #endif
            edit_message(body);
            break;
        case POWEROFF_NOW:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF NOW");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = send_command("{\"action\":\"poweroff\",\"minutes\":\"0\"}");
            #if DEBUG
            ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));
            #endif
            break;
        case POWEROFF_30:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF 30");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = send_command("{\"action\":\"poweroff\",\"minutes\":\"30\"}");
            #if DEBUG
            ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));
            #endif
            break;
        case POWEROFF_60:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF 60");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = send_command("{\"action\":\"poweroff\",\"minutes\":\"60\"}");
            #if DEBUG
            ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));

            #endif
            break;
        case POWEROFF_120:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - POWEROFF 120");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = send_command("{\"action\":\"poweroff\",\"minutes\":\"120\"}");
            #if DEBUG
            ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));
            #endif
            break;
        case REBOOT_MENU:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT MENU");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Thinking hard...\"}", reply.callback_id);
            ret = answer_callback(body);
            snprintf(body, sizeof(body), "{\"message_id\":%lld," REBOOT_MENU_BODY, edit_id);
            #if DEBUG
            ESP_LOGI(TAG, "Body: %s", body);
            #endif
            edit_message(body);
            break;
        case REBOOT_NOW:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT NOW");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = send_command("{\"action\":\"reboot\",\"minutes\":\"0\"}");
            #if DEBUG
            ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));
            #endif
            break;
        case REBOOT_30:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT 30");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = send_command("{\"action\":\"reboot\",\"minutes\":\"30\"}");
            #if DEBUG
            ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));
            #endif
            break;
        case REBOOT_60:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT 60");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = send_command("{\"action\":\"reboot\",\"minutes\":\"60\"}");
            #if DEBUG
            ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));
            #endif
            break;
        case REBOOT_120:
            #if DEBUG
            ESP_LOGI(TAG, "Elaborating - REBOOT 120");
            #endif
            snprintf(body, sizeof(body),
                     "{\"callback_query_id\":%s,\"text\":\"Asking the server...\"}", reply.callback_id);
            ret = answer_callback(body);
            ret = send_command("{\"action\":\"reboot\",\"minutes\":\"120\"}");
            #if DEBUG
            ESP_LOGI(TAG, "Command status: %s", esp_err_to_name(ret));
            #endif
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

    // Setup https connection
    const esp_http_client_config_t send_config = {
        .url = BASE_URL "/sendMessage",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000
    };

    esp_http_client_handle_t client = esp_http_client_init(&send_config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_set_post_field(client, body, strlen(body));
    #if DEBUG
    ESP_LOGI(TAG, "Sending body: %s", body);
    #endif
    // Send message
    esp_err_t ret = esp_http_client_perform(client);

    #if DEBUG
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
    else if (esp_http_client_get_status_code(client) != 200)
        ESP_LOGW(TAG, "Failed to send message - Status: %d", esp_http_client_get_status_code(client));
    else
        ESP_LOGI(TAG, "Message sent!");
    #endif

    // Close connection
    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t answer_callback(const char *body)
{
    // Setup https connection
    #if DEBUG
    ESP_LOGI(TAG, "Answering callback...");
    #endif
    const esp_http_client_config_t send_config = {
        .url = BASE_URL "/answerCallbackQuery",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&send_config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_set_post_field(client, body, strlen(body));

    // Send message
    esp_err_t ret = esp_http_client_perform(client);

    #if DEBUG
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));


    if (esp_http_client_get_status_code(client) != 200)
        ESP_LOGW(TAG, "Failed to send message - Status: %d", esp_http_client_get_status_code(client));
    else
        ESP_LOGI(TAG, "Answer sent!");
    #endif

    // Close connection
    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t edit_message(const char *body)
{

    #if DEBUG
    ESP_LOGI(TAG, "Heap before: %d", esp_get_free_heap_size());
    #endif

    // Setup https connection
    #if DEBUG
    ESP_LOGI(TAG, "Editing message...");
    #endif
    const esp_http_client_config_t send_config = {
        .url = BASE_URL "/editMessageText",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&send_config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_set_post_field(client, body, strlen(body));

    // Send message
    esp_err_t ret = esp_http_client_perform(client);

    #if DEBUG
    ESP_LOGI(TAG, "Heap after: %d", esp_get_free_heap_size());
    #endif
    #if DEBUG
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));


    if (esp_http_client_get_status_code(client) != 200)
        ESP_LOGW(TAG, "Failed to send message - Status: %d", esp_http_client_get_status_code(client));
    else
        ESP_LOGI(TAG, "Message edited!");
    #endif

    // Close connection
    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t send_command(const char *body)
{
    #if DEBUG
    ESP_LOGI(TAG, "Sending command...");
    #endif

    // Setup https connection
    const esp_http_client_config_t send_config = {
        .url = SERVER_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000
    };

    esp_http_client_handle_t client = esp_http_client_init(&send_config);
    esp_http_client_set_header(client, "Authorization", "Bearer " API_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_set_post_field(client, body, strlen(body));
    #if DEBUG
    ESP_LOGI(TAG, "Sending body: %s", body);
    #endif
    // Send message
    esp_err_t ret = esp_http_client_perform(client);

    #if DEBUG
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
    else if (esp_http_client_get_status_code(client) != 200)
        ESP_LOGW(TAG, "Failed to send command - Status: %d", esp_http_client_get_status_code(client));
    else
        ESP_LOGI(TAG, "Command sent!");
    #endif

    // Close connection
    esp_http_client_cleanup(client);
    return ret;
}

void wakeOnLan(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed - Coudn't setup socket");
        #endif // DEBUG
        return;
    }

    // Allow broadcast
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(7);
    dest_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    // Build WoL magic packet (6 x 0xFF + 16 x MAC address repeated)
    uint8_t wol_packet[102];
    memset(wol_packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(&wol_packet[6 + i * 6], MAC_ADDRESS, 6);
    }

    sendto(sock, wol_packet, sizeof(wol_packet), 0,
           (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    close(sock);
    #if DEBUG
    ESP_LOGI(TAG, "Magic packet sent");
    #endif
}

void ping_callback() //TODO Manage errors
{
    #if DEBUG
    ESP_LOGI(TAG, "Ping callback");
    #endif
    ping_stop();

    char body[800];
    snprintf(body, sizeof(body),
             "{\"chat_id\":%s,\"message_id\":%lld,\"text\":\"%s\", %s}", AUTHORIZED_CHAT_ID_STR, edit_id, (ping_status == 0 ? "Running" : "Sleepy"), MENU_KEYBOARD);
    #if DEBUG
    ESP_LOGI(TAG, "Body: %s", body);
    #endif
    edit_message(body);
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
