#ifndef PING_H
#define PING_H

/**
 *
 * @brief Calls 'ping_callback' of myTelebot signaling successful ping
 *
 */
void test_on_ping_success(void);


/**
 *
 * @brief Calls 'ping_callback' of myTelebot signaling failed ping
 *
 */
void test_on_ping_timeout(void);

/**
 *
 * @brief Initialize and start the ping session
 *
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t ping_go(void);

/**
 *
 * @brief Stops and deletes the ping session
 *
 */
void ping_stop(void);

#endif
