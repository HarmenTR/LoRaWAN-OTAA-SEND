A compact LoRaWAN environmental monitoring node based on the TTGO LoRa32 V1.3 (ESP32 + SX1276).
It samples temperature and humidity every 5 seconds using an SHT31 sensor, displays the running averages on a 0.96" I2C SSD1306 OLED, and transmits the 60-second average values to the network via LoRaWAN OTAA (MCCI LMIC) every 15 minutes.

Core features:

LoRaWAN uplink using MCCI LMIC (OTAA activation)

SHT31 temperature & humidity measurements

Local OLED status display with measurement feedback

Averaging and periodic sending algorithm

Designed for TTGO LoRa32 V1.3 pinout

Ideal for environmental IoT, remote monitoring and low-power data logging projects.
