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
esp_err_t pool_updates(http_buffer_t *buffer, void* callback);

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
 *  - void
 */
void wakeOnLan (void);

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
 * @brief Callback containg the status of the server
 *
 */
void ping_callback(bool isRunning);

/**
 *
 * @brief Callbacks for ping
 *
 */
void test_on_ping_success(void);
void test_on_ping_timeout(void);

/**
 *
 * @brief Calls the APIs of the server
 *
 *
 * @return
 *  - See HTTP codes.
 */
int localApiCall (void);

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
 * @brief Edit last sent message with a given 'body'.
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t edit_message(const char *body);

/**
 *
 * @brief ???
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t telegram_answer_callback(const char *callback_query_id);

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
 * @brief Check WiFi connection, if disconnected it tries to reconnect
 *
 *
 * @return
 *  - true if connected/reconnected, false otherwise.
 */
bool check_connection(void);

#endif
