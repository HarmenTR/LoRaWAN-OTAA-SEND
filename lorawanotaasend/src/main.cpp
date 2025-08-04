#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>  // SHT31 sıcaklık-nem sensörü

Adafruit_SHT31 sht31 = Adafruit_SHT31();

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// LMIC Pinleri (TTGO LoRa32 V1.3.1 için)
const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 14,
    .dio = {26, 33, 32}
};

// Cihaz bilgileri (LSB sıralı)
static const u1_t DEVEUI[8] = {
  0x10, 0x00, 0x06, 0x25, 0x86, 0x44, 0x82, 0x26
};
static const u1_t APPEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static const u1_t APPKEY[16] = {
  0xFA, 0x28, 0x08, 0x93,
  0xDA, 0x26, 0x82, 0x42,
  0x65, 0x2C, 0xB0, 0x23,
  0xAE, 0x11, 0x10, 0x98
};

void os_getDevEui(u1_t* buf) { memcpy(buf, DEVEUI, 8); }
void os_getArtEui(u1_t* buf) { memcpy(buf, APPEUI, 8); }
void os_getDevKey(u1_t* buf) { memcpy(buf, APPKEY, 16); }

static osjob_t sendjob;
uint16_t measureCount = 0;  // Sayaç

// Ortalama hesaplamak için değişkenler
float tempSum = 0.0;
float humiSum = 0.0;
uint16_t sampleCount = 0;

// Son ölçülen ortalama
float lastAvgTemp = 0.0;
float lastAvgHumi = 0.0;

// Sürekli OLED güncellemesi için zaman takibi
unsigned long lastSampleTime = 0;
unsigned long lastOLEDUpdate = 0;

// Gönderim durumunu takip etmek için flag
static bool sending = false;

void saveSession() {
  Serial.println("Session kaydedildi (örnek fonksiyon)");
}

// Gönderim fonksiyonu
void do_send(osjob_t* j) {
  // LMIC modülünün önceki bir gönderimle meşgul olup olmadığını kontrol et
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("LMIC meşgul, gönderim bekleniyor, atlandi."));
    // LMIC meşgulken yeni bir gönderim planlamayız, mevcut işlemi bitirmesini bekleriz.
    // İşlem bittiğinde (EV_TXCOMPLETE) yeni gönderim zaten zamanlanacaktır.
    return;
  }

  // Yeterli örnek toplanıp toplanmadığını kontrol et
  // Eğer hiç örnek yoksa, payload oluşturmaya veya göndermeye çalışma.
  if (sampleCount == 0) {
    Serial.println("Veri yok, gönderim atlandi.");
    // Veri toplanana kadar boş paket göndermeyi denememek için
    // bir sonraki gönderimi daha kısa bir süre sonra tekrar dene.
    // Örneğin, 60 saniye sonra tekrar kontrol edebilirsin.
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(60), do_send);
    return;
  }

  // 'sending' bayrağı, birden fazla do_send çağrısının üst üste gelmesini engeller.
  // Normalde LMIC.opmode kontrolü bunu yapar, ama ek güvenlik sağlar.
  if (sending) {
    Serial.println(F("Önceki gönderim zaten devam ediyor, atlandi."));
    return;
  }

  // Tüm kontrollerden geçtiysek, gönderim işlemine başla ve 'sending' bayrağını ayarla.
  sending = true;

  // Sıcaklık ve nem ortalamalarını hesapla
  // Bu kısma dokunmuyorum, mevcut mantığınızı koruyorum.
  float avgTemp100 = tempSum / sampleCount; // (t + 60) * 100 olarak toplandığı varsayılıyor
  float avgHumi = humiSum / sampleCount;

  // Payload'ı hazırla
  // Bu kısma da dokunmuyorum, mevcut mantığınızı koruyorum.
  int16_t t = (int16_t)(avgTemp100 / 10.0); // Önceki kodunuzdaki dönüşüme göre
  int16_t h = (int16_t)(avgHumi * 10);     // Önceki kodunuzdaki dönüşüme göre

  // Ekranda gösterilecek ortalamaları güncelle (payload gönderiminden sonra)
  // Bu kısım payload ile ilgili değil ama yine de güncelleniyor.
  lastAvgTemp = ((avgTemp100 / 100.0));
  lastAvgHumi = avgHumi;

  uint8_t payload[4];
  payload[0] = (t >> 8) & 0xFF;
  payload[1] = t & 0xFF;
  payload[2] = (h >> 8) & 0xFF;
  payload[3] = h & 0xFF;

  // Veri gönderme komutunu tetikle
  LMIC_setTxData2(1, payload, sizeof(payload), 1);
  Serial.print("Gönderilen değer: T=");
  Serial.print(t); // Payload olarak gönderilen ham değeri basıyoruz
  Serial.print(" H=");
  Serial.println(h); // Payload olarak gönderilen ham değeri basıyoruz

  // Gönderim için kullanılan sayaçları sıfırla
  measureCount++; // Bu genel bir sayaç, ihtiyacınıza göre kalsın.
  tempSum = 0;
  humiSum = 0;
  sampleCount = 0;
}

void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
    case EV_JOINING:
      Serial.println(F("Ağa katiliyor..."));
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Aga katiliyor...");
      display.display();
      break;

    case EV_JOINED:
      Serial.println(F("JOINED"));
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("JOINED");
      display.setCursor(0, 10);
      display.println("15dk sonra gonder");
      display.display();
      LMIC_setLinkCheckMode(0);
      saveSession();

      // 15 dakika sonra gönderim işini başlat
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(900), do_send);
      break;

case EV_TXCOMPLETE:
  Serial.println(F("Gönderim tamamlandi."));
  if (LMIC.txrxFlags & TXRX_ACK) {
    Serial.println(F("ACK alindi."));
    Serial.print("LMIC.seqnoUp = ");
    Serial.println(LMIC.seqnoUp);
  } else {
    Serial.println(F("ACK alinmadi!"));
  }

  sending = false;  // Gönderim bitti, flag'i sıfırla

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Ortalama gonderildi");
  display.setCursor(0, 10);
  display.println("Yeni tur 15dk");
  display.display();

  // Eğer bu ilk veri gönderimiyse (seqnoUp == 1), 10 saniye sonra tekrar gönderimi başlat
  if (LMIC.seqnoUp == 1) {
    Serial.println("İlk gönderimden sonra 10 saniyelik bekleme uygulanıyor...");
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(10), do_send);
  } else {
    // Diğer durumlarda 15 dakika sonra veri gönder
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(900), do_send);
  }
  break;

    case EV_JOIN_FAILED:
      Serial.println(F("Katilim basarisiz!"));
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Katilim Hatasi");
      display.display();
      break;

    default:
      Serial.print(F("Olay: "));
      Serial.println(ev);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED bulunamadi"));
    while (true);
  }

  if (!sht31.begin(0x44)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SHT31 bulunamadi!");
    display.display();
    while (true);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LoRa Baslatiliyor");
  display.display();

  os_init();
  LMIC_reset();
  LMIC_startJoining();
}

void loop() {
  os_runloop_once();

  // Her 5 saniyede bir ölçüm al (örnekleme hızı)
  if (millis() - lastSampleTime >= 5000) {
    lastSampleTime = millis();

    float t = sht31.readTemperature();
    float h = sht31.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      tempSum += (t + 60)*100;
      humiSum += h ;
      sampleCount++;

      Serial.print("Ölçüm alındı - Sicaklik: ");
      Serial.print(t);
      Serial.print(" C, Nem: ");
      Serial.print(h);
      Serial.println("      %");
    } else {
      Serial.println("SHT31 okunamadi!");
    }
  }

  // OLED ekranı 2 saniyede bir güncelle
  if (millis() - lastOLEDUpdate >= 2000) {
    lastOLEDUpdate = millis();

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println("60 sn ortalamasi:");

    display.setCursor(0, 10);
    display.print("Sicaklik: ");
    display.print(lastAvgTemp - 60.0, 2);
    display.println(" C");

    display.setCursor(0, 20);
    display.print("Nem : ");
    display.print(lastAvgHumi , 2);
    display.println(" %");

    display.setCursor(0, 35);
    display.print("Ornek Sayisi: ");
    display.println(sampleCount);

    display.display();
  }
}
