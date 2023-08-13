# key-bot

**key-bot** is a research interaction project, positioned to be used in a shared workspace where only a single, shared key is available. 

The project is built around the ESP32 microcontroller utilizing the ESP-IDF framework.

[Research paper available here](https://danielgalis.com/posts/key-bot/)

## Software

### Setup
1. Create a Discord bot and make sure to enable [Privileged Gateway Intents](https://github.com/abobija/esp-discord/issues/6#issuecomment-1559844490))
2. Get your bot's token and paste it into the menuconfig
3. Invite the bot to your server
4. Get the channel ID of the Discord channel you want to use for communicating with the bot ([guide](https://support.discord.com/hc/en-us/articles/206346498-Where-can-I-find-my-User-Server-Message-ID-)) and paste into the menuconfig
5. Test

## Hardware

- ESP32 board (ESP32-DevKitC V4 with the ESP-WROOM-32E module, others may work)
- M4 30mm screw (acting as a capacitive sensor)
- MOSFET (IRF520N)
- Addressable RGB LED (WS2812B)
- Solenoid (SparkFun ROB-11015)
- Cables
- Micro USB power supply
- Casing (I'm using a 3D printed one from [file])
- Screws for the casing
