#ifndef TELEBOT_H
#define TELEBOT_H

/**
 *
 * @brief Request updates
 *
 * IMPORTANT! This will modify 'buffer->buffer' allocating resources,
 *  it's the caller responsability to disallocate them via free.
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t poll_updates(http_buffer_t *buffer, void* callback);

/**
 *
 * @brief Parse the response obtained and extract the command requested from the user
 *
 * IMPORTANT! It modifies a global variable 'update_id' to keep track of which
 *  messages has been elaborated.
 *
 *
 * @return
 *  - See command.h for command types.
 */
parse_t parse(char *response);

/**
 *
 * @brief Calls a function based on 'command'
 *
 * Important!
 *  This function may call:
 *      wakeOnLan, localApiCall, send_message, telegram_answer_callback.
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t elaborate (reply_struct_t command);

/**
 *
 * @brief Wake on Lan to power on the server
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wake_on_lan (void);

/**
 *
 * @brief Start ping to get the status of the server
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t status(void);

/**
 *
 * @brief Callback that manages the message based on the status recived in 'ping_status'
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t ping_callback();

/**
 *
 * @brief Callbacks for my_ping
 *
 * IMPORTANT! This will modify 'ping_status' with 0 if ping success, 1 if timout.
 *
 */
void test_on_ping_success(void* args, void* cb_args);
void test_on_ping_timeout(void* args, void* cb_args);

/**
 *
 * @brief Calls the APIs of the server.
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t send_command(const char *body);

/**
 *
 * @brief Send a message with a given 'body'.
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t send_message(const char *body);

/**
 *
 * @brief Edit a message via a given 'body'.
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t edit_message(const char *body);

/**
 *
 * @brief Respond to the callback query with a given 'body'.
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t answer_callback(const char *body);

/**
 *
 * @brief SNTP Time Synchronization
 *
 *
 * @return
 *  - true if synced, false otherwise.
 */
bool my_sync(void);

/**
 *
 * @brief Manage the errors of the functions calling telegram bot APIs.
 *
 *
 * Note: It MUST be called after a telegram API call.
 *
 * IMPORTANT! This function may 'abort()' if the error is critical.
 */
bool manage_error_telegram_api(esp_err_t err);

/**
 *
 * @brief Manage the errors of the functions calling the server APIs.
 *
 *
 * Note: It MUST be called after a server API call.
 *
 * IMPORTANT! This function may 'abort()' if the error is critical.
 */
bool manage_error_server_api(esp_err_t err);

/**
 *
 * @brief Called when sending an api call (both to telegram and server).
 *
 * This reduces repeated code.
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t api_call_helper(esp_http_client_handle_t *client, bool(*error_manager)(esp_err_t), const char* body);

/**
 *
 * @brief Check if current timeout has reached maximum allowed, in that case resets the http client before sending the request.
 *
 * Called at the start of api call (both to telegram and server).
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t auto_clean(esp_http_client_handle_t *client);

#endif
