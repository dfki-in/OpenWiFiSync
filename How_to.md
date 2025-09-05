# Wi-Fi Beacon Frame Synchronization

In this project Wi-Fi beacon frames from an off-the-shelf router are captured to synchronize time between two separate ESP32-microcontrollers, one declared as a master (*syncR_M*) and one as a slave (*syncR_S*). These use the gptimer-peripheral to synchronize to the MAC-timestamps from the beacon-frame of the router. For collection of the synchronization-offsets an observer-project (*syncR_obs*) is included, which is built to listen to the broadcasts sent by master and slave. The timestamps are then logged over UART and can be analyzed (e.g. with a Python-script).

## Requirements
The Hardware thats used:
- Wi-Fi Router
- 3x ESP32-S3

To compile the code, you will need to install [ESP-IDF](https://github.com/espressif/esp-idf), preferrably in version 5.4 in which this code has been tested.

## Steps for Setting Up
The only thing to be configured is the macro WIFI_SSID in the file main.h in both syncR_M and syncR_S to match the SSID of your router. After that, compile the 3 projects and flash them on the ESPs. Synchronization offsets are then directly being logged by the observer.
