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
        char time_buff[64];
        current_time(time_buff, sizeof(time_buff));

        ESP_LOGI(TAG, "Current time: %s", time_buff);
        #endif
        synced = true;
        return true;
    }
    else
        return true;
}

void current_time(char *time_buff, uint8_t buff_size)
{
    time_t now;
    time(&now);

    // CET in winter, CEST in summer
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0", 1);
    tzset();

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    strftime(time_buff, buff_size, "%H:%M:%S", &timeinfo);
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
