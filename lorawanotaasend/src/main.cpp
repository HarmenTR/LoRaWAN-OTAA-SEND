#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>  // SHT31 temperature-humidity sensor

Adafruit_SHT31 sht31 = Adafruit_SHT31();

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// LMIC Pins (for TTGO LoRa32 V1.3.1)
const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 14,
    .dio = {26, 33, 32}
};

// Device information (LSB ordered)
static const u1_t DEVEUI[8] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const u1_t APPEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static const u1_t APPKEY[16] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

void os_getDevEui(u1_t* buf) { memcpy(buf, DEVEUI, 8); }
void os_getArtEui(u1_t* buf) { memcpy(buf, APPEUI, 8); }
void os_getDevKey(u1_t* buf) { memcpy(buf, APPKEY, 16); }

static osjob_t sendjob;
uint16_t measureCount = 0;  // Counter

// Variables for calculating average
float tempSum = 0.0;
float humiSum = 0.0;
uint16_t sampleCount = 0;

// Last measured average
float lastAvgTemp = 0.0;
float lastAvgHumi = 0.0;

// Time tracking for continuous OLED update
unsigned long lastSampleTime = 0;
unsigned long lastOLEDUpdate = 0;

// Flag to track sending status
static bool sending = false;

void saveSession() {
  Serial.println("Session saved (example function)");
}

// Sending function
void do_send(osjob_t* j) {
  // Check if LMIC is busy with a previous transmission
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("LMIC busy, waiting for previous send, skipped."));
    return;
  }

  // Check if enough samples have been collected
  if (sampleCount == 0) {
    Serial.println("No data, send skipped.");
    // Try again after 60 seconds
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(60), do_send);
    return;
  }

  // Prevent multiple consecutive do_send calls
  if (sending) {
    Serial.println(F("Previous send already in progress, skipped."));
    return;
  }

  // Start sending
  sending = true;

  // Calculate averages
  float avgTemp100 = tempSum / sampleCount; // Assuming (t + 60) * 100 accumulated
  float avgHumi = humiSum / sampleCount;

  // Prepare payload
  int16_t t = (int16_t)(avgTemp100 / 10.0);
  int16_t h = (int16_t)(avgHumi * 10);

  // Update averages shown on screen (after payload sent)
  lastAvgTemp = ((avgTemp100 / 100.0));
  lastAvgHumi = avgHumi;

  uint8_t payload[4];
  payload[0] = (t >> 8) & 0xFF;
  payload[1] = t & 0xFF;
  payload[2] = (h >> 8) & 0xFF;
  payload[3] = h & 0xFF;

  LMIC_setTxData2(1, payload, sizeof(payload), 1);
  Serial.print("Sent values: T=");
  Serial.print(t);
  Serial.print(" H=");
  Serial.println(h);

  // Reset counters used for sending
  measureCount++;
  tempSum = 0;
  humiSum = 0;
  sampleCount = 0;
}

void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
    case EV_JOINING:
      Serial.println(F("Joining network..."));
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Joining...");
      display.display();
      break;

    case EV_JOINED:
      Serial.println(F("JOINED"));
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("JOINED");
      display.setCursor(0, 10);
      display.println("Sending in 15 min");
      display.display();
      LMIC_setLinkCheckMode(0);
      saveSession();

      // Start first send after 15 minutes
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(900), do_send);
      break;

    case EV_TXCOMPLETE:
      Serial.println(F("Transmission complete."));
      if (LMIC.txrxFlags & TXRX_ACK) {
        Serial.println(F("ACK received."));
        Serial.print("LMIC.seqnoUp = ");
        Serial.println(LMIC.seqnoUp);
      } else {
        Serial.println(F("ACK not received!"));
      }

      sending = false;  // Clear sending flag

      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Avg sent");
      display.setCursor(0, 10);
      display.println("Next in 15 min");
      display.display();

      // If this was the first transmission
      if (LMIC.seqnoUp == 1) {
        Serial.println("Waiting 10 s after first send...");
        os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(10), do_send);
      } else {
        os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(900), do_send);
      }
      break;

    case EV_JOIN_FAILED:
      Serial.println(F("Join failed!"));
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Join Error");
      display.display();
      break;

    default:
      Serial.print(F("Event: "));
      Serial.println(ev);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED not found"));
    while (true);
  }

  if (!sht31.begin(0x44)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SHT31 not found!");
    display.display();
    while (true);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Starting LoRa");
  display.display();

  os_init();
  LMIC_reset();
  LMIC_startJoining();
}

void loop() {
  os_runloop_once();

  // Take a measurement every 5 seconds (sampling rate)
  if (millis() - lastSampleTime >= 5000) {
    lastSampleTime = millis();

    float t = sht31.readTemperature();
    float h = sht31.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      tempSum += (t + 60)*100;
      humiSum += h ;
      sampleCount++;

      Serial.print("Sample taken - Temp: ");
      Serial.print(t);
      Serial.print(" C, Humidity: ");
      Serial.print(h);
      Serial.println(" %");
    } else {
      Serial.println("SHT31 read failed!");
    }
  }

  // Update OLED screen every 2 seconds
  if (millis() - lastOLEDUpdate >= 2000) {
    lastOLEDUpdate = millis();

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println("60 sec average:");

    display.setCursor(0, 10);
    display.print("Temp: ");
    display.print(lastAvgTemp - 60.0, 2);
    display.println(" C");

    display.setCursor(0, 20);
    display.print("Hum : ");
    display.print(lastAvgHumi , 2);
    display.println(" %");

    display.setCursor(0, 35);
    display.print("Samples: ");
    display.println(sampleCount);

    display.display();
  }
}
