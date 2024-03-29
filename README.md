# Keybot 🔑

**Keybot** is a research interaction project, positioned to be used in a shared workspace where only a single, shared key is available. 

The project is built around the ESP32 microcontroller utilizing the ESP-IDF framework.

[Research paper available here](https://danielgalis.com/posts/key-bot/)

[(Not all) research resources available here](https://are.na/daniel-galis/key-bot-project)

[(Not all) Datasheets available](https://are.na/daniel-galis/key-bot-project-datasheets)

# How to

## Software
1. Create a Discord bot and make sure to enable [Privileged Gateway Intents](https://github.com/abobija/esp-discord/issues/6#issuecomment-1559844490)
2. Get your bot's token and paste it into the `menuconfig`
3. Invite the bot to your server
4. Get the channel ID of the Discord channel you want to use for communicating with the bot ([guide](https://support.discord.com/hc/en-us/articles/206346498-Where-can-I-find-my-User-Server-Message-ID-)) and paste into the `menuconfig`
5. Setup WIFI SSID and Password in the `menuconfig`
6. Buid, Flash and Monitor using [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html)

## Hardware
### Parts:
- ESP32 board (ESP32-DevKitC V4 with the ESP-WROOM-32E module, others may work)
- M4 30mm screw (acting as a capacitive sensor)
- MOSFET (IRF520N)
- Addressable RGB LED (WS2812B)
- Solenoid (SparkFun ROB-11015)
- Cables
- Micro USB power supply
- Casing (I'm using a 3D printed one from these [files](/casing))
- Screws for the casing

### Schematics:
[WIP](https://sare.na/block/24840316)

## Using Keybot
- When the capacitive sensor is activated for a second straight, the bot sends a message notifying that the key has been hung.
- When the capacitive sensor is not activated for a second straight, the bot sends a message notifying that the key is no longer hung.
- When any user sends a message containing the string `knock` into the bot's Discord channel, the solenoid and LED are activated for a few seconds. If someone forgot to hang the key, this should alert them to do so.

### TODOS
- [ ] visual schematic
- [ ] add photos to readme
- [ ] add all component datasheets to are.na
- [ ] add all theoretical resources to are.na
