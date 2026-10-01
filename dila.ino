/*
  ==========================================================
  SISTEM MONITORING DAN PENGENDALIAN OTOMATIS
  BIBIT KELAPA SAWIT BERBASIS INTERNET OF THINGS
  (Blynk + Thinger.io + ESP-NOW + Google Sheets + MQTT + Serial Manual)
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
#include <PubSubClient.h> 

// ==========================================================
// KONFIGURASI WI-FI & URL
// ==========================================================
char ssid[] = "lida";
char pass[] = "nanasmuda";

String GOOGLE_SCRIPT_URL_D3 = "https://script.google.com/macros/s/AKfycbxIHvZ4xddd_aN-op7BwsFq3fzGBYlvb7ob5Ap6JJ7udYeA6k14m_WSZtoTCBPnI5qi/exec";

// ==========================================================
// KONFIGURASI MQTT 
// ==========================================================
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char* topic_ph = "sawit/monitor/ph";
const char* topic_rh_udara = "sawit/monitor/kelembapan_udara";
const char* topic_rh_tanah = "sawit/monitor/kelembapan_tanah";
const char* topic_suhu = "sawit/monitor/suhu_udara";

WiFiClient espClientMQTT;
PubSubClient mqttClient(espClientMQTT);

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
const unsigned long INTERVAL_BLYNK_SEND = 1000UL;      
const unsigned long INTERVAL_MQTT_SEND = 5000UL; 

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
bool modeSensorManual = false; // TRUE = Data dari Serial Monitor, FALSE = Data dari Sensor

String modePompaBlynk = "OFF"; 
bool statusManualThinger = false;
int tahapanKirimBlynk = 0;

unsigned long waktuDHTSebelumnya = 0, waktuTanahSebelumnya = 0;
unsigned long waktuPHSebelumnya = 0, waktuLCDSebelumnya = 0, waktuCloudD3Sebelumnya = 0;
unsigned long waktuCobaWiFiSebelumnya = 0, waktuCobaBlynkSebelumnya = 0;
unsigned long waktuCobaMQTTSebelumnya = 0, waktuMQTTSebelumnya = 0;

enum StatusPembacaanPH { PH_DIAM, PH_STABILISASI, PH_MENGAMBIL_SAMPEL };
StatusPembacaanPH statusPembacaanPH = PH_DIAM;
unsigned long waktuMulaiStabilisasiPH = 0, waktuSampelPHTerakhir = 0;
uint32_t totalADCPh = 0;
int adcPHMinimum = 1023, adcPHMaksimum = 0, jumlahSampelPH = 0;

// ==========================================================
// STRUKTUR ESP-NOW
// ==========================================================
typedef struct DataSensorLokal {
  int id_node; float vCharge; float iCharge; float pCharge; 
  float vLoad; float iLoad; float pLoad; float iNet; 
  float socCC; float suhu_b; 
} DataSensorLokal;

DataSensorLokal dataElsevier;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&dataElsevier, incomingData, sizeof(dataElsevier));
}

float batasiPersen(float nilai) { return constrain(nilai, 0.0f, 100.0f); }

void tulisRelay(uint8_t pinRelay, bool menyala) {
  digitalWrite(pinRelay, (RELAY_ACTIVE_LOW) ? (menyala ? LOW : HIGH) : (menyala ? HIGH : LOW));
}

void aturPompa(bool menyala) { statusPompa = menyala; tulisRelay(RELAY_POMPA_PIN, statusPompa); }
void aturKipasMasuk(bool menyala) { statusKipasMasuk = menyala; tulisRelay(RELAY_KIPAS_MASUK_PIN, statusKipasMasuk); }
void aturKipasBuang(bool menyala) { statusKipasBuang = menyala; tulisRelay(RELAY_KIPAS_BUANG_PIN, statusKipasBuang); }

float hitungPHTanah(float nilaiADC) { return constrain((PH_KEMIRINGAN * nilaiADC) + PH_KONSTANTA, 0.0f, 14.0f); }
float hitungKelembapanTanah(float nilaiADC) {
  float rentang = (float)ADC_TANAH_BASAH - (float)ADC_TANAH_KERING;
  if (fabsf(rentang) < 1.0f) return NAN;
  return batasiPersen(((nilaiADC - ADC_TANAH_KERING) / rentang) * 100.0f);
}

void prosesPompa() {
  if (modePompaBlynk == "ON" || statusManualThinger == true) {
    if (statusPompa == false) aturPompa(true);
  } else {
    if (statusPompa == true) aturPompa(false);
  }
}

void evaluasiUdara() {
  if (!isnan(suhu)) {
    if (suhu >= SUHU_KIPAS_MASUK_ON) permintaanKipasMasuk = true;
    else if (suhu <= SUHU_KIPAS_MASUK_OFF) permintaanKipasMasuk = false;
  } else permintaanKipasMasuk = false;
  
  if (statusKipasMasuk != permintaanKipasMasuk) aturKipasMasuk(permintaanKipasMasuk);

  if (!isnan(kelembapanUdara)) {
    if (kelembapanUdara >= RH_KIPAS_BUANG_ON) permintaanKipasBuang = true;
    else if (kelembapanUdara <= RH_KIPAS_BUANG_OFF) permintaanKipasBuang = false;
  } else permintaanKipasBuang = false;
  
  if (statusKipasBuang != permintaanKipasBuang) aturKipasBuang(permintaanKipasBuang);
}

// ==========================================================
// PEMBACAAN INPUT SERIAL MONITOR (UNTUK TESTING MANUAL)
// ==========================================================
void bacaSerialMonitor() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    String inputUpper = input;
    inputUpper.toUpperCase();

    if (inputUpper == "MANUAL") {
      modeSensorManual = true;
      Serial.println(">> MODE MANUAL AKTIF: Sensor fisik diabaikan.");
    }
    else if (inputUpper == "SENSOR") {
      modeSensorManual = false;
      Serial.println(">> MODE SENSOR AKTIF: Membaca data dari sensor fisik.");
    }
    else if (inputUpper.startsWith("SUHU=")) {
      suhu = input.substring(5).toFloat();
      modeSensorManual = true;
      Serial.println(">> (Manual) Suhu disetel ke: " + String(suhu));
    }
    else if (inputUpper.startsWith("RHU=")) {
      kelembapanUdara = input.substring(4).toFloat();
      modeSensorManual = true;
      Serial.println(">> (Manual) Kelembapan Udara disetel ke: " + String(kelembapanUdara));
    }
    else if (inputUpper.startsWith("RHT=")) {
      kelembapanTanah = input.substring(4).toFloat();
      modeSensorManual = true;
      Serial.println(">> (Manual) Kelembapan Tanah disetel ke: " + String(kelembapanTanah));
    }
    else if (inputUpper.startsWith("PH=")) {
      nilaiPH = input.substring(3).toFloat();
      modeSensorManual = true;
      Serial.println(">> (Manual) pH disetel ke: " + String(nilaiPH));
    }
  }
}

// ==========================================================
// RUTINITAS SENSOR FISIK
// ==========================================================
void prosesDHT(unsigned long waktuSekarang) {
  if (modeSensorManual) return; // Jika mode manual, lewati baca sensor
  if (waktuSekarang - waktuDHTSebelumnya < INTERVAL_DHT) return;
  waktuDHTSebelumnya = waktuSekarang;
  float s = dht.readTemperature(), h = dht.readHumidity();
  suhu = isnan(s) ? NAN : s + OFFSET_SUHU;
  kelembapanUdara = isnan(h) ? NAN : batasiPersen(h + OFFSET_KELEMBAPAN_UDARA);
}

void prosesKelembapanTanah(unsigned long waktuSekarang) {
  if (modeSensorManual) return; // Jika mode manual, lewati baca sensor
  if (waktuSekarang - waktuTanahSebelumnya < INTERVAL_TANAH) return;
  waktuTanahSebelumnya = waktuSekarang;
  uint32_t totalADC = 0; int adcMin = 1023, adcMax = 0;
  for (int i = 0; i < JUMLAH_SAMPEL_TANAH; i++) {
    int adc = analogRead(SOIL_MOISTURE_ADC_PIN);
    totalADC += adc;
    if (adc < adcMin) adcMin = adc;
    if (adc > adcMax) adcMax = adc;
    delayMicroseconds(250);
  }
  adcKelembapanTanah = (float)(totalADC - adcMin - adcMax) / (JUMLAH_SAMPEL_TANAH - 2);
  kelembapanTanah = hitungKelembapanTanah(adcKelembapanTanah);
}

void prosesPHTanah(unsigned long waktuSekarang) {
  if (modeSensorManual) return; // Jika mode manual, lewati baca sensor
  if (statusPembacaanPH == PH_DIAM && waktuSekarang - waktuPHSebelumnya >= INTERVAL_PH) {
    waktuPHSebelumnya = waktuSekarang; digitalWrite(DMS_CONTROL_PIN, HIGH);
    waktuMulaiStabilisasiPH = waktuSekarang;
    totalADCPh = 0; adcPHMinimum = 1023; adcPHMaksimum = 0; jumlahSampelPH = 0;
    statusPembacaanPH = PH_STABILISASI;
  }
  if (statusPembacaanPH == PH_STABILISASI && waktuSekarang - waktuMulaiStabilisasiPH >= WAKTU_STABILISASI_DMS) {
    statusPembacaanPH = PH_MENGAMBIL_SAMPEL; waktuSampelPHTerakhir = waktuSekarang - INTERVAL_SAMPEL_PH;
  }
  if (statusPembacaanPH != PH_MENGAMBIL_SAMPEL || waktuSekarang - waktuSampelPHTerakhir < INTERVAL_SAMPEL_PH) return;
  
  waktuSampelPHTerakhir = waktuSekarang;
  int adc = analogRead(PH_ADC_PIN);
  totalADCPh += adc;
  if (adc < adcPHMinimum) adcPHMinimum = adc;
  if (adc > adcPHMaksimum) adcPHMaksimum = adc;
  jumlahSampelPH++;

  if (jumlahSampelPH >= JUMLAH_SAMPEL_PH) {
    digitalWrite(DMS_CONTROL_PIN, LOW);
    adcPHTanah = (float)(totalADCPh - adcPHMinimum - adcPHMaksimum) / (JUMLAH_SAMPEL_PH - 2);
    nilaiPH = hitungPHTanah(adcPHTanah);
    statusPembacaanPH = PH_DIAM;
  }
}

// ==========================================================
// TAMPILAN LCD & SERIAL
// ==========================================================
void prosesLCD(unsigned long waktuSekarang) {
  if (waktuSekarang - waktuLCDSebelumnya < INTERVAL_LCD) return;
  waktuLCDSebelumnya = waktuSekarang;

  String s_s = isnan(suhu) ? "--" : String((int)suhu);
  String s_u = isnan(kelembapanUdara) ? "--" : String((int)kelembapanUdara);
  String s_t = isnan(kelembapanTanah) ? "--" : String((int)kelembapanTanah);
  String s_ph = isnan(nilaiPH) ? "-.-" : String(nilaiPH, 1);

  String km = statusKipasMasuk ? "ON " : "OFF";
  String kb = statusKipasBuang ? "ON " : "OFF";
  String pa = statusPompa ? "ON " : "OFF";

  char vp[6], ip[6], vb[6], ib[6];
  dtostrf(dataElsevier.vCharge, 4, 1, vp);
  dtostrf(dataElsevier.iCharge, 4, 2, ip);
  dtostrf(dataElsevier.vLoad, 4, 1, vb);
  dtostrf(dataElsevier.iLoad, 4, 2, ib);

  char b0[22], b1[22], b2[22], b3[22];
  snprintf(b0, sizeof(b0), "S:%-2sC KM:%-3s VP:%s", s_s.c_str(), km.c_str(), vp);
  snprintf(b1, sizeof(b1), "U:%-2s%% KB:%-3s IP:%s", s_u.c_str(), kb.c_str(), ip);
  snprintf(b2, sizeof(b2), "T:%-2s%% PA:%-3s VB:%s", s_t.c_str(), pa.c_str(), vb);
  snprintf(b3, sizeof(b3), "PH:%-4s  IB:%s", s_ph.c_str(), ib);

  static String sBaris[4];
  String baris[4] = {String(b0), String(b1), String(b2), String(b3)};

  for(int i=0; i<4; i++) {
    while(baris[i].length() < 20) { baris[i] += " "; }
    if (baris[i] != sBaris[i]) {
      lcd.setCursor(0, i); 
      lcd.print(baris[i]);
      sBaris[i] = baris[i];
    }
  }
}

void tampilkanSemuaData(unsigned long waktuSekarang) {
  static unsigned long waktuTampilSebelumnya = 0;
  if (waktuSekarang - waktuTampilSebelumnya >= 5000UL) {
    waktuTampilSebelumnya = waktuSekarang;
    Serial.println("\n=== GABUNGAN DATA ===");
    Serial.printf("Mode Data  : %s\n", modeSensorManual ? "MANUAL (SERIAL)" : "SENSOR FISIK");
    Serial.printf("Suhu Ruang : %.1f C | RH Ruang: %.1f %%\n", suhu, kelembapanUdara);
    Serial.printf("RH Tanah   : %.1f %% | pH Tanah: %.1f\n", kelembapanTanah, nilaiPH);
    Serial.println("=====================\n");
  }
}

// ==========================================================
// KONEKSI & CLOUD (Blynk, Thinger, G-Sheets, MQTT)
// ==========================================================
BLYNK_WRITE(V12) {
  String perintah = param.asString();
  perintah.toUpperCase(); 
  if (perintah == "1" || perintah == "ON") modePompaBlynk = "ON";
  else modePompaBlynk = "OFF";
}

void kirimDataBlynk() {
  if (!Blynk.connected()) return;
  switch (tahapanKirimBlynk) {
    case 0:
      if (!isnan(nilaiPH)) Blynk.virtualWrite(V0, nilaiPH); 
      if (!isnan(suhu)) Blynk.virtualWrite(V1, suhu);       
      if (!isnan(kelembapanUdara)) Blynk.virtualWrite(V2, (int)round(kelembapanUdara)); 
      break;
    case 1:
      Blynk.virtualWrite(V3, statusPompa ? "ON" : "OFF");      
      Blynk.virtualWrite(V4, statusKipasMasuk ? "ON" : "OFF"); 
      Blynk.virtualWrite(V5, statusKipasBuang ? "ON" : "OFF"); 
      break;
    case 2:
      if (!isnan(kelembapanTanah)) Blynk.virtualWrite(V6, (int)round(kelembapanTanah)); 
      break;
    case 3:
      Blynk.virtualWrite(V7, dataElsevier.vCharge);  
      Blynk.virtualWrite(V8, dataElsevier.iCharge);  
      break;
    case 4:
      Blynk.virtualWrite(V9, dataElsevier.vLoad);                  
      Blynk.virtualWrite(V10, dataElsevier.iLoad);                
      Blynk.virtualWrite(V11, (int)round(dataElsevier.pLoad));    
      break;
  }
  tahapanKirimBlynk++;
  if (tahapanKirimBlynk > 4) tahapanKirimBlynk = 0;
}

void prosesKoneksiWiFiBlynk(unsigned long waktuSekarang) {
  if (WiFi.status() != WL_CONNECTED) {
    if (waktuSekarang - waktuCobaWiFiSebelumnya >= 10000UL) {
      waktuCobaWiFiSebelumnya = waktuSekarang;
      WiFi.disconnect(); WiFi.begin(ssid, pass);
    }
    return;
  }
  if (!Blynk.connected()) {
    if (waktuCobaBlynkSebelumnya == 0 || waktuSekarang - waktuCobaBlynkSebelumnya >= 5000UL) {
      waktuCobaBlynkSebelumnya = waktuSekarang;
      Blynk.connect(1000);
    }
  }
  if (Blynk.connected()) Blynk.run();
}

void prosesKoneksiMQTT(unsigned long waktuSekarang) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqttClient.connected()) {
    if (waktuSekarang - waktuCobaMQTTSebelumnya >= 5000UL) {
      waktuCobaMQTTSebelumnya = waktuSekarang;
      String clientId = "SawitESP32-";
      clientId += String(random(0xffff), HEX);
      if (mqttClient.connect(clientId.c_str())) Serial.println("[MQTT] Terhubung ke Broker!");
    }
  } else {
    mqttClient.loop();
    if (waktuSekarang - waktuMQTTSebelumnya >= INTERVAL_MQTT_SEND) {
      waktuMQTTSebelumnya = waktuSekarang;
      if (!isnan(suhu)) mqttClient.publish(topic_suhu, String(suhu, 1).c_str());
      if (!isnan(kelembapanUdara)) mqttClient.publish(topic_rh_udara, String(kelembapanUdara, 1).c_str());
      if (!isnan(kelembapanTanah)) mqttClient.publish(topic_rh_tanah, String(kelembapanTanah, 1).c_str());
      if (!isnan(nilaiPH)) mqttClient.publish(topic_ph, String(nilaiPH, 2).c_str());
    }
  }
}

void kirimKeGoogleSheets(unsigned long waktuSekarang) {
  if (waktuSekarang - waktuCloudD3Sebelumnya >= INTERVAL_CLOUD_D3) {
    waktuCloudD3Sebelumnya = waktuSekarang;
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure client; client.setInsecure(); HTTPClient http;
      
      String url = GOOGLE_SCRIPT_URL_D3;
      url += "?suhu_d3=" + String(isnan(suhu) ? 0 : suhu, 1);
      url += "&rh_d3=" + String(isnan(kelembapanUdara) ? 0 : kelembapanUdara, 1);
      url += "&soil_d3=" + String(isnan(kelembapanTanah) ? 0 : kelembapanTanah, 1);
      url += "&ph_d3=" + String(isnan(nilaiPH) ? 0 : nilaiPH, 2);
      
      http.begin(client, url); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      int httpCode = http.GET();
      if(httpCode > 0) Serial.println("[CLOUD] Data Sheet terkirim!");
      http.end();
    }
  }
}

// ==========================================================
// SETUP & INISIALISASI
// ==========================================================
void setup() {
  Serial.begin(115200);
  analogReadResolution(10);

  pinMode(PH_ADC_PIN, INPUT); analogSetPinAttenuation(PH_ADC_PIN, ADC_11db);
  pinMode(DMS_CONTROL_PIN, OUTPUT); digitalWrite(DMS_CONTROL_PIN, LOW);
  pinMode(SOIL_MOISTURE_ADC_PIN, INPUT); analogSetPinAttenuation(SOIL_MOISTURE_ADC_PIN, ADC_11db);
  pinMode(RELAY_POMPA_PIN, OUTPUT); pinMode(RELAY_KIPAS_MASUK_PIN, OUTPUT); pinMode(RELAY_KIPAS_BUANG_PIN, OUTPUT);

  aturPompa(false); aturKipasMasuk(false); aturKipasBuang(false);

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN, 100000);
  lcd.init(); lcd.backlight(); lcd.clear(); dht.begin();

  lcd.print("Sistem Bibit"); lcd.setCursor(0, 1); lcd.print("Memulai...");

  WiFi.mode(WIFI_STA); WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);
  thing.add_wifi(ssid, pass); 
  
  thing["DataSawit"] >> [](pson& out){
    out["pH_Tanah"] = isnan(nilaiPH) ? 0.0 : nilaiPH;
    out["Suhu_Ruangan"] = isnan(suhu) ? 0.0 : suhu;
    out["Kelembapan_Udara"] = isnan(kelembapanUdara) ? 0.0 : kelembapanUdara;
    out["Kelembapan_Tanah"] = isnan(kelembapanTanah) ? 0.0 : kelembapanTanah;
    out["Udara_Masuk"] = statusKipasMasuk;
    out["Udara_Keluar"] = statusKipasBuang;
    out["Status_Pompa"] = statusPompa;
  };

  thing["kontrolPompa"] << [](pson& in){
    if(in.is_empty()){
      in = statusManualThinger;
    } else {
      statusManualThinger = in;
    }
  };

  mqttClient.setServer(mqtt_server, mqtt_port);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
  }

  timerBlynk.setInterval(INTERVAL_BLYNK_SEND, kirimDataBlynk);
  
  delay(1500); lcd.clear();
}

// ==========================================================
// LOOP (Program Utama)
// ==========================================================
void loop() {
  unsigned long waktuSekarang = millis();

  // 1. Baca Perintah Manual dari Serial Monitor
  bacaSerialMonitor();

  // 2. Proses Jaringan IoT (WiFi, Blynk, Thinger, MQTT)
  prosesKoneksiWiFiBlynk(waktuSekarang); 
  thing.handle();                        
  prosesKoneksiMQTT(waktuSekarang); 
  
  // 3. Proses Pembacaan Sensor (Akan dilewati jika Mode Manual aktif)
  prosesDHT(waktuSekarang);              
  prosesKelembapanTanah(waktuSekarang);  
  prosesPHTanah(waktuSekarang);          
  
  // 4. Evaluasi Kondisi Kipas & Pompa Berdasarkan Data (Manual/Sensor)
  evaluasiUdara(); // <-- Posisinya dipindah agar Kipas merespon input Serial Manual
  prosesPompa();            
  
  // 5. Tampilkan ke Layar LCD & Serial
  prosesLCD(waktuSekarang);              
  tampilkanSemuaData(waktuSekarang);     
  
  // 6. Pengiriman ke Cloud secara berkala
  timerBlynk.run();                      
  kirimKeGoogleSheets(waktuSekarang);    
}
