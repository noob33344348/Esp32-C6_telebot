#ifndef COMMAND_H
#define COMMAND_H

typedef enum
{
    START,
    HOME,
    STATUS,
    WAKE,
    POWEROFF_MENU,
    POWEROFF_NOW,
    POWEROFF_30,
    POWEROFF_60,
    POWEROFF_120,
    REBOOT_MENU,
    REBOOT_NOW,
    REBOOT_30,
    REBOOT_60,
    REBOOT_120,
    ERROR,
    NO_COMMAND
} command_t;


typedef struct
{
    command_t command;
    int64_t message_id;
} reply_struct_t;

typedef struct
{
    reply_struct_t* reply;
    uint32_t count;
} parse_t;

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} http_buffer_t;

#endif
