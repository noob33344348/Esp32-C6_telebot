#ifndef TELEBOT_ENV
#define TELEBOT_ENV

// Telegram

#define BOT_TOKEN "SECRET"
#define BASE_URL "https://api.telegram.org/bot" BOT_TOKEN

extern const uint8_t telegram_root_pem_start[]
asm("_binary_telegram_root_pem_start");

extern const uint8_t telegram_root_pem_end[]
asm("_binary_telegram_root_pem_end");


// Server

#define SERVER_IP_STRING "SECRET"
#define SERVER_API_URL "http://" SERVER_IP_STRING ":SECRET/SECRET"
#define API_TOKEN "SECRET"

static const uint8_t MAC_ADDRESS[] =  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // SECRET

#endif
