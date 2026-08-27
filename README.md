# Esp32-C6_telebot
A simple telegram bot for Esp32-C6.

## Features:
- Uses pings to get the server status (Running / Sleepy).
- Uses Wake On Lan to turn on the server.
- Uses local server APIs to reboot or poweroff the server (or virtually run any command).

## Quality of Life:
- Increase/Decrease the polling rate based on user recent interactions.
- The menus use inline keyboard to achieve a better user experience.
- The menus are easily customizable in 'bot_settings.h'.


## Code Logic

### Main cycle
1. **Health check**: Monitor Wi-Fi connection and synchronization status, restore either if offline.
2. **Fetch**: Poll for pending updates.
3. **Parse**: Parse the important data from the HTTP response to a <a href="https://github.com/noob33344348/Esp32-C6_telebot/blob/main/main/my_types.h">parse_t</a> structure.
4. **Elaborate**: Decide which action to undertake based on the 'command' field of parse_t. 
5. **Free parse_t**: Free the callback_id fields of the parse_t returned from parse.
6. **Poll speed**: Increase/Decrease the polling rate if necessary.
7. **Ping reply**: Check if any ping requested is completed and handle results.
8. **Cleanup**: Release resources.

### API call logic
1. **Client setup**: Initialize the HTTP client handler if it is NULL.
2. **Body**: Add a body to the request if necessary.
3. **Execute**: Perform the request.
4. **Status check**: Check that both the return value and the HTTP status code are successful.
5. **Manage errors**: If the request failed a count is incremented. If it reaches the maximum number of failures admited the program aborts, otherwise it will reset the HTTP client handler to NULL.
<p>Note:</p>
<p>The count is nullified if a single request is successful.</p>
<p>Telegram API call errors and Server API call errors are managed separately.</p>
<p>When the count for Server API call errors reaches the maximum allowed a ping will be triggered:</p>
<ul>
  <li>Server running -> abort the program.</li>
  <li>Server sleepy -> notify the user.</li>
</ul>

### Important macros and constants:
#### <a href="https://github.com/noob33344348/Esp32-C6_telebot/blob/main/main/environment.h">environment.h</a> 
- BOT_TOKEN : the token received from Bot Father to authenticate the requests to Telegram APIs. 
- SERVER_API_URL: The local URL where the server hosts its APIs.
- API_TOKEN: the token generated from the server to authenticate the requests to its APIs.
- MAC_ADDRESS: the MAC address of the server; used to build the Wake On Lan packets.

#### <a href="https://github.com/noob33344348/Esp32-C6_telebot/blob/main/main/bot_settings.h">bot_settings.h</a>
- AUTHORIZED_CHAT_ID_STR/INT: the chat_id of the user allowed to use the bot (can be converted in an array for multiple users but the authorization code has to be slightly modified).
- MAX/MIN_POLL: polling interval can span between these values (in seconds).
- MAX_*_TRIES: the maximum number of failed requests to trigger an 'abort()', freshly restarting the whole program.

#### <a href="https://github.com/noob33344348/Esp32-C6_telebot/blob/main/main/myTelebot.c">myTelebot.c</a>
- DEBUG: enables debug if set to true.
- TAG: the tag used in the debug.

### Examples
#### Wake On Lan
![Demo](https://github.com/noob33344348/Esp32-C6_telebot/blob/main/main/Examples/wol_example.gif)

#### Server Command
![Demo](https://github.com/noob33344348/Esp32-C6_telebot/blob/main/main/Examples/server_command_example.gif)

#### Failure
![Demo](https://github.com/noob33344348/Esp32-C6_telebot/blob/main/main/Examples/failure_example.gif)
