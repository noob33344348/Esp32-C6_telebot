#ifndef BOT_SETTINGS_H
#define BOT_SETTINGS_H

#define AUTHORIZED_CHAT_ID_STR "SECRET"
#define AUTHORIZED_CHAT_ID_INT SECRET

#define MAX_POLL 30
#define MIN_POLL 5
#define MAX_SYNC_TRIES 5
#define MAX_WIFI_TRIES 10
#define MAX_TELEGRAM_API_FAILURES 10
#define MAX_SERVER_API_FAILURES 2
#define GENERAL_HTTP_TIMEOUT 3000
#define SERVER_HTTP_TIMEOUT 3000

#define MENU_START \
"{" \
    "\"chat_id\":" AUTHORIZED_CHAT_ID_STR "," \

#define MENU_TEXT \
    "\"text\":\"Commands:\"," \

#define MENU_KEYBOARD \
    "\"reply_markup\":{" \
        "\"inline_keyboard\":[[" \
            "{\"text\":\"Wake\",\"callback_data\":\"wake\"}," \
            "{\"text\":\"Status\",\"callback_data\":\"status\"}" \
        "],["\
            "{\"text\":\"Poweroff\",\"callback_data\":\"poweroff_menu\"}," \
            "{\"text\":\"Reboot\",\"callback_data\":\"reboot_menu\"}" \
        "],["\
            "{\"text\":\"Poff Now\",\"callback_data\":\"poweroff_now\"}," \
            "{\"text\":\"Rebo Now\",\"callback_data\":\"reboot_now\"}" \
        "],["\
            "{\"text\":\"Poff 60\",\"callback_data\":\"poweroff_60\"}," \
            "{\"text\":\"Rebo 60\",\"callback_data\":\"reboot_60\"}" \
        "]]" \
    "}" \

#define MENU_END \
"}" \

#define POWEROFF_MENU_BODY \
    "\"chat_id\":" AUTHORIZED_CHAT_ID_STR "," \
    "\"text\":\"Poweroff:\"," \
    "\"reply_markup\":{" \
        "\"inline_keyboard\":[[" \
            "{\"text\":\"Now\",\"callback_data\":\"poweroff_now\"}," \
            "{\"text\":\"30\",\"callback_data\":\"poweroff_30\"}" \
        "],["\
            "{\"text\":\"60\",\"callback_data\":\"poweroff_60\"}," \
            "{\"text\":\"120\",\"callback_data\":\"poweroff_120\"}" \
        "],["\
            "{\"text\":\"Home\",\"callback_data\":\"home\"}" \
        "]]" \
    "}" \
"}" \

#define REBOOT_MENU_BODY \
    "\"chat_id\":" AUTHORIZED_CHAT_ID_STR "," \
    "\"text\":\"Reboot:\"," \
    "\"reply_markup\":{" \
        "\"inline_keyboard\":[[" \
            "{\"text\":\"Now\",\"callback_data\":\"reboot_now\"}," \
            "{\"text\":\"30\",\"callback_data\":\"reboot_30\"}" \
        "],["\
            "{\"text\":\"60\",\"callback_data\":\"reboot_60\"}," \
            "{\"text\":\"120\",\"callback_data\":\"reboot_120\"}" \
        "],["\
            "{\"text\":\"Home\",\"callback_data\":\"home\"}" \
        "]]" \
    "}" \
"}" \



#endif
