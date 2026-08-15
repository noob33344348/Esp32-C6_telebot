#ifndef TELEBOT_H
#define TELEBOT_H

/**
 *
 * @brief Request updates
 *
 * Important!
 *  This function modifies the 'response' parameter.
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t pool_updates(char* response);

/**
 *
 * @brief Parse the response obtained and extract the command requested from the user
 *
 *
 * @return
 *  - See command.h for command types.
 */
command_t *parse(char* response);

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
esp_err_t elaborate (command_t command);

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
 * @brief Get the status of the server
 *
 *
 * @return
 *  - false: server is down
 *  - true: server is up
 */
bool status(void);

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
 * @brief Send a message with a given 'body'
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t send_message(bool hasBody, const char *body);

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

#endif
