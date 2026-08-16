// Premade libraries
#include <stdio.h>
#include "esp_http_client.h"
#include "cJSON.h"

// Documentation of IMPLEMENTATIONS
#include "myTelebot.h"

// Custom wifi driver (station mode only)
#include "wifi_sta.h"

// Environment variables of telegram and server
#include "environment.h"

// Replies texts and authorized chat ids
#include "bot_settings.h"

// Define the enum for the commands used
#include "command_t.h"

// Simple ping functions implementations
#include my_ping.h

// Debugging
#define DEBUG true
#define TAG "Telebot"

// Tracks which update has been recived
static int64_t update_id = 0;
static int64_t latest_status_message_id = -1;

// MAIN
void app_main(void)
{
    esp_err_t ret;

    // Wifi connection
    ret = my_wifi_init();
    if(ret != ESP_OK)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
        #endif
        return;
    }

    ret = my_wifi_start();
    if(ret != ESP_OK)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
        #endif
        return;
    }

    // Main loop
    while (1)
    {
        // Try to reconnect until it works
        while(my_wifi_status() == false)
        {
            ret = my_wifi_reconnect();
            if(ret != ESP_OK)
            {
                #if DEBUG
                ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
                #endif
                return;
            }
        }

        // Look for updates
        char *response;
        ret = pool_updates(response);
        if(ret == ESP_OK)
        {
            // Elaborate response
            reply_struct_t_t *commands = parse(response);
            uint32_t num_of_commands = sizeof(commands)/sizeof(reply_struct_t_t);

            for(uint32_t i=0; i<num_of_commands; i++)
                elaborate(commands[i]);

        }
        #if DEBUG
        else
            ESP_LOGW(TAG, "Failed to retrive messages: %d", esp_err_to_name(err));
        #endif

        // Wait
        for(volatile uint32_t i=0; i<100000; i++);
    }
}

// IMPLEMENTATIONS

esp_err_t pool_updates(char* response)
{

    // Setup https connection
    esp_http_client_config_t pool_config = {
        .url = (BASE_URL "/getUpdates?offset=%lld&timeout=30", update_id);
        .cert_pem = TELEGRAM_ROOT_CERT,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 35000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&pool_config);
    esp_err_t err = esp_http_client_perform(client);

    #if DEBUG
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "Success - Status: %d",
             esp_http_client_get_status_code(client));
    #endif


    // Read response
    int content_len = esp_http_client_get_content_length(client);
    char buffer[content_len + 1];
    int read_len = esp_http_client_read(client, buffer, content_len);
    buffer[read_len] = '\0';

    response = buffer;

    // Close connection
    esp_http_client_cleanup(client);
    return ret;
}

reply_struct_t *parse(char* response)
{
    cJSON *root = cJSON_Parse(response);

    // Check for errors in response
    if (root == nullptr)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed - HTTP response - null");
        #endif
        return ERROR;
    }

    cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (!cJSON_IsTrue(ok))
    {
        #if DEBUG
        ESP_LOGE(TAG, "Failed - HTTP response - ok: false");
        #endif
        cJSON_Delete(root);
        return ERROR;
    }

    // Parse
    cJSON *result = cJSON_GetObjectItem(root, "result");
    reply_struct_t ret[cJSON_GetArraySize(result)];
    uint32_t counter = 0;
    cJSON_ArrayForEach(update, result)
    {
        // Update managed actions
        update_id = cJSON_GetObjectItem(update, "update_id")->valueint;

        // Non authorized users
        cJSON chat = cJSON_GetObjectItem(update, "chat");
        if(cJSON_GetObjectItem(chat, "chat_id")->valueint != AUTHORIZED_CHAT_ID)
        {
            #if DEBUG
            ESP_LOGI(TAG, "Non authorized chat id");
            #endif
            ret[counter].command = NO_COMMAND;
            ret[counter].message_id = -1;
        }

        // Authorized users
        else
        {
            // Return requested action
            char* text = cJSON_GetObjectItem(update, "text")->valuestring;

            // Data (in case of a callback query)
            cJSON* id_data = cJSON_GetObjectItem(update, "data");
            char* data = id_data ? id_data->valuestring : nullptr;

            // Message id (in case of a callback query)
            cJSON* id_item = cJSON_GetObjectItem(update, "message_id");
            int id = id_item ? id_item->valueint : -1;
            ret[counter].message_id = id;

            // Message data
            if(text == "/start")
            {
                ret[counter].command = START;
            }
            else
            {
                switch (data)
                {
                    case "home":
                        ret[counter].command = HOME;
                        break;
                    case "status":
                        ret[counter].command = STATUS;
                        break;
                    case "wake":
                        ret[counter].command = WAKE;
                        break;
                    case "poweroff_menu":
                        ret[counter].command = POWEROFF_MENU;
                        break;
                    case "poweroff_now":
                        ret[counter].command = POWEROFF_NOW;
                        break;
                    case "poweroff_30":
                        ret[counter].command = POWEROFF_30;
                        break;
                    case "poweroff_60":
                        ret[counter].command = POWEROFF_60;
                        break;
                    case "poweroff_120":
                        ret[counter].command = POWEROFF_120;
                        break;
                    case "reboot_menu":
                        ret[counter].command = REBOOT_MENU;
                        break;
                    case "reboot_now":
                        ret[counter].command = REBOOT_NOW;
                        break;
                    case "reboot_30":
                        ret[counter].command = REBOOT_30;
                        break;
                    case "reboot_60":
                        ret[counter] = REBOOT_60;
                        break;
                    case "reboot_120":
                        ret[counter].command = REBOOT_120;
                        break;
                    default:
                        ret[counter].command = NO_COMMAND;
                        break;
                }
            }
        }
        counter++;
    }

    cJSON_Delete(root);
    return ret;
}

void elaborate (reply_struct_t reply)
{
    switch(reply.command)
    {
        case START:
            send_message(MENU_START MENU_TEXT MENU_KEYBOARD MENU_END);
            break;
        case HOME:
            edit_message(MENU_START MENU_TEXT ("\"message_id\":\"%d:\",", reply.message_id) MENU_KEYBOARD MENU_END);
            break;
        case STATUS:
            latest_status_message_id = reply.message_id;
            esp_err_t err = ping_go();
            #if DEBUG
            if(err != ESP_OK)
                ESP_LOGE(TAG, "Failed - Couldn't start a new ping session");
            #endif
            break;
        case WAKE:
            break;
        case POWEROFF_MENU:
            break;
        case POWEROFF_NOW:
            break;
        case POWEROFF_30:
            break;
        case POWEROFF_60:
            break;
        case POWEROFF_120:
            break;
        case POWEROFF_MENU:
            break;
        case REBOOT_NOW:
            break;
        case REBOOT_30:
            break;
        case REBOOT_60:
            break;
        case REBOOT_120:
            break;
        case ERROR:
            break;
        case NO_COMMAND:
            break;
    }
}

esp_err_t send_message(const char *body)
{
    // Setup https connection
    const esp_http_client_config_t send_config = {
        .url = BASE_URL BOT_TOKEN "/sendMessage",
        .cert_pem = TELEGRAM_ROOT_CERT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&send_config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_set_post_field(client, body, strlen(body));

    // Send message
    esp_err_t err = esp_http_client_perform(client);

    #if DEBUG
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(err));


    if (esp_http_client_get_status_code(client) != 200)
        ESP_LOGW(TAG, "Failed to send message - Status: %d", esp_http_client_get_status_code(client));
    else
        ESP_LOGI(TAG, "POST - Success");
    #endif

    // Close connection
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t edit_message(const char *body)
{
    // Setup https connection
    const esp_http_client_config_t send_config = {
        .url = BASE_URL BOT_TOKEN "/editMessageText",
        .cert_pem = TELEGRAM_ROOT_CERT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&send_config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_set_post_field(client, body, strlen(body));

    // Send message
    esp_err_t err = esp_http_client_perform(client);

    #if DEBUG
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(err));


    if (esp_http_client_get_status_code(client) != 200)
        ESP_LOGW(TAG, "Failed to send message - Status: %d", esp_http_client_get_status_code(client));
    else
        ESP_LOGI(TAG, "POST - Success");
    #endif

    // Close connection
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t telegram_answer_callback(const char *callback_query_id) {
    // TODO Recheck the code
    char post_data[256];

    esp_http_client_config_t config = {
        .url = BASE_URL BOT_TOKEN "/answerCallbackQuery?callback_query_id=" callback_query_id "&text=OK",
        .cert_pem = TELEGRAM_ROOT_CERT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);


    esp_http_client_cleanup(client);
    return err;
}

void wakeOnLan(void)
{
    // Assemble 16 magic packets
    byte magicPacket[102];
    for (int i = 0; i < 6; i++)
        magicPacket[i] = 0xFF;

    for (int i = 0; i < 16; i++)
        memcpy(&magicPacket[6 + i * 6], MAC_ADDRESS, 6);


    // TODO Send packets

    #if DEBUG
    ESP_LOGI(TAG, "Magic packet sent");
    #endif
}

void ping_callback(bool isRunning)
{
    ping_stop();

    if(latest_status_message_id == -1)
    {
        #if DEBUG
        ESP_LOGE(TAG, "Inconsistent call to ping_callback with latest_status_message_id: -1. - Couldn't edit_the message");
        #endif
        return;
    }

    // Go back to home message
    reply_struct_t home;
    home.command = HOME;
    home.message_id = latest_status_message_id;
    elaborate(home);

    // But edit the text to show the server status
    const char* body = "{\"chat_id\": \"AUTHORIZED_CHAT_ID\", \"message_id\": %d, \"text\": \"Status:\" " (isRunning ? "Running":"Sleepy") "}";
    edit_message(body);

}
