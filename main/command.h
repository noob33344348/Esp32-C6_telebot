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

#endif
