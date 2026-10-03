// =====================================================================
//  IoT_AQMS.ino  -  Akuisisi CO2, PM2.5, suhu, kelembapan setiap 2 menit
//  lalu kirim data mentah ke server lewat WiFi (HTTPS POST JSON)
//  Board: ESP32 Dev Module, core arduino-esp32 v1.0.6
// =====================================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ===== 1. Konfigurasi perangkat & jaringan =====
const String device_name = "TMM_JKT-";
const String ssid        = "NAMA_WIFI";
const String pass        = "PASSWORD_WIFI";

// ===== 2. Endpoint server RnD BiruLangit =====
String api_rndbirulangit = "https://rndbirulangit.pythonanywhere.com/api/post/telkom_group";

// ===== 3. Variabel global =====
float CO2v = NAN, PM25v = NAN, Tempinv = NAN, Humidinv = NAN;   // diisi kode sensor

struct RawData {
  String created_at, uptime, RSSI, SD_status, PM25_status, SHT_status;
  String CO2v, PM25v, Tempinv, Humidinv;
};
RawData raw_2m;

WiFiClientSecure *client_birulangit;

// ===== 4. File pendukung (HARUS setelah deklarasi di atas) =====
#include "Date_Time.h"
#include "Send.h"

// ===== 5. Akuisisi sensor (isi dengan kode sensor Anda) =====
void initSensors() {
  // TODO: Serial1.begin(9600, SERIAL_8N1, RX, TX);  // MH-Z19B
  // TODO: Serial2.begin(9600);                      // PMS5003
  // TODO: Wire.begin(21, 22);                       // SHT31
}

void readSensors() {
  // TODO: baca MH-Z19B -> CO2v      (ppm)
  // TODO: baca PMS5003 -> PM25v     (ug/m3)
  // TODO: baca SHT31   -> Tempinv   (degC), Humidinv (%RH)
  // Bila pembacaan gagal, biarkan bernilai NAN
}

// ===== 6. WiFi =====
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("[WiFi] Menyambung ke " + ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println(" OK, IP " + WiFi.localIP().toString());
  else
    Serial.println(" GAGAL");
}

// ===== 7. Isi paket data satu slot =====
void fillRaw() {
  raw_2m.created_at  = get_precission_second(0);   // dibulatkan ke HH:MM:00
  raw_2m.uptime      = get_uptime();
  raw_2m.RSSI        = String(WiFi.RSSI());
  raw_2m.SD_status   = "0";                         // praktikum tanpa SD card
  raw_2m.PM25_status = isnan(PM25v)   ? "0" : "1";  // sesuaikan konvensi server
  raw_2m.SHT_status  = isnan(Tempinv) ? "0" : "1";
  raw_2m.CO2v        = String(CO2v, 0);
  raw_2m.PM25v       = String(PM25v, 1);
  raw_2m.Tempinv     = String(Tempinv, 2);
  raw_2m.Humidinv    = String(Humidinv, 2);
}

// ===== 8. Menjaga waktu tetap benar =====
int lastSyncDay = -1;
unsigned long lastTry = 0;

void maintainTime() {
  if (millis() - lastTry < 30000) return;           // coba paling cepat tiap 30 s
  bool valid = rtc.getYear() >= 2025;
  bool daily = valid && rtc.getHour(true) == 0 && rtc.getMinute() == 1
               && rtc.getDay() != lastSyncDay;      // resync harian 00:01
  if (!valid || daily) {
    lastTry = millis();
    connectWiFi();
    if (Sync_RTC()) lastSyncDay = rtc.getDay();
  }
}

// ===== 9. Program utama =====
bool send_2m = false;

void setup() {
  Serial.begin(115200);
  initSensors();
  client_birulangit = new WiFiClientSecure;
  client_birulangit->setInsecure();                 // praktikum: tanpa verifikasi sertifikat
  connectWiFi();
  if (Sync_RTC()) lastSyncDay = rtc.getDay();
}

void loop() {
  maintainTime();
  int m = rtc.getMinute(), s = rtc.getSecond();

  if (rtc.getYear() >= 2025 && m % 2 == 0 && s == 0 && !send_2m) {
    send_2m = true;                                  // kunci: sekali per slot
    readSensors();
    fillRaw();
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      Send_rndbirulangit();
    }
  }
  if (s != 0) send_2m = false;                       // buka kunci untuk slot berikut
  delay(50);
}
