/*
  ==========================================================
  SISTEM MONITORING DAN PENGENDALIAN OTOMATIS
  BIBIT KELAPA SAWIT BERBASIS INTERNET OF THINGS
  (Blynk + Thinger.io + ESP-NOW + Google Sheets)
  ==========================================================
*/

// ==========================================================
// KONFIGURASI BLYNK & THINGER.IO
// ==========================================================
#define BLYNK_TEMPLATE_ID "TMPL6uAAjqVzm"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN "Pwt8SQGQenXAEDRxYn0zRnS628MG7kP-"
#define BLYNK_PRINT Serial

#define THINGER_USERNAME "Herlinjufri"
#define THINGER_DEVICE_ID "Sistem_Monitoring_Sawit"
#define THINGER_DEVICE_CREDENTIAL "05l17zMlvXf7i+25"

// ==========================================================
// LIBRARY
// ==========================================================
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ThingerESP32.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ==========================================================
// KONFIGURASI WI-FI & URL
// ==========================================================
char ssid[] = "SoC22";
char pass[] = "soc12345";

String GOOGLE_SCRIPT_URL_D3 = "https://script.google.com/macros/s/AKfycbxIHvZ4xddd_aN-op7BwsFq3fzGBYlvb7ob5Ap6JJ7udYeA6k14m_WSZtoTCBPnI5qi/exec";

// ==========================================================
// KONFIGURASI PIN & OBJEK
// ==========================================================
#define PH_ADC_PIN 34
#define DMS_CONTROL_PIN 13
#define SOIL_MOISTURE_ADC_PIN 35
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define LCD_SDA_PIN 21
#define LCD_SCL_PIN 22
#define RELAY_POMPA_PIN 18
#define RELAY_KIPAS_MASUK_PIN 19
#define RELAY_KIPAS_BUANG_PIN 23

LiquidCrystal_I2C lcd(0x27, 20, 4);
DHT dht(DHT_PIN, DHT_TYPE);
BlynkTimer timerBlynk;
ThingerESP32 thing(THINGER_USERNAME, THINGER_DEVICE_ID, THINGER_DEVICE_CREDENTIAL);

// ==========================================================
// PARAMETER SISTEM & BATAS KONTROL
// ==========================================================
const bool RELAY_ACTIVE_LOW = true;
const float SUHU_KIPAS_MASUK_ON = 35.0;
const float SUHU_KIPAS_MASUK_OFF = 30.0;
const float RH_KIPAS_BUANG_ON = 80.0;
const float RH_KIPAS_BUANG_OFF = 70.0;
const float OFFSET_SUHU = 0.0;
const float OFFSET_KELEMBAPAN_UDARA = 0.0;
const float PH_KEMIRINGAN = -0.0233;
const float PH_KONSTANTA = 17.248;
const int ADC_TANAH_KERING = 800;
const int ADC_TANAH_BASAH = 350;

// INTERVAL (ms)
const unsigned long INTERVAL_DHT = 2000UL;
const unsigned long INTERVAL_TANAH = 3000UL;
const unsigned long INTERVAL_PH = 5000UL;
const unsigned long INTERVAL_LCD = 500UL;
const unsigned long INTERVAL_CLOUD_D3 = 60000UL; 

// INTERVAL PENGIRIMAN BLYNK (1000ms untuk mencegah Flood Error)
const unsigned long INTERVAL_BLYNK_SEND = 1000UL;      

const unsigned long WAKTU_STABILISASI_DMS = 500UL;
const unsigned long INTERVAL_SAMPEL_PH = 20UL;
const int JUMLAH_SAMPEL_PH = 30;
const int JUMLAH_SAMPEL_TANAH = 20;

// ==========================================================
// VARIABEL GLOBAL & KONTROL MANUAL
// ==========================================================
float suhu = NAN, kelembapanUdara = NAN;
float adcPHTanah = NAN, teganganPH = NAN, nilaiPH = NAN;
float adcKelembapanTanah = NAN, kelembapanTanah = NAN;

bool statusPompa = false, statusKipasMasuk = false, statusKipasBuang = false;
bool permintaanKipasMasuk = false, permintaanKipasBuang = false;

// Variabel Kontrol Manual Pompa
String modePompaBlynk = "OFF"; 
bool statusManualThinger = false;

// Variabel Pencegah Flood Error Blynk
int tahapanKirimBlynk = 0;

unsigned long waktuDHTSebelumnya = 0, waktuTanahSebelumnya = 0;
unsigned long waktuPHSebelumnya = 0, waktuLCDSebelumnya = 0, waktuCloudD3Sebelumnya = 0;
unsigned long waktuCobaWiFiSebelumnya = 0, waktuCobaBlynkSebelumnya = 0;
