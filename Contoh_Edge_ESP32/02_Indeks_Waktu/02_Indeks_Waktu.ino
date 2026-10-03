// =====================================================================
//  02_Indeks_Waktu.ino  -  Jam internal ESP32 sebagai indeks data
//  Sinkron NTP, lalu sampling selaras slot waktu dan indeks hari-jam-menit.
//
//  Board : ESP32 Dev Module, core arduino-esp32 v1.0.6
//  Sensor: simulasi (lihat readSensors). Ganti dengan sensor asli bila ada.
//
//  Perintah Serial Monitor (115200, Newline):
//    (tidak ada)
// =====================================================================
#include <WiFi.h>
#include "time.h"

const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const long  GMT_OFFSET = 7 * 3600;          // WIB

// DEMO_SPEED 1: sampel tiap 5 s, cocok untuk demo di kelas.
// DEMO_SPEED 0: kondisi sebenarnya (sampel tiap 2 menit).
#define DEMO_SPEED 1
#if DEMO_SPEED
  #define INTERVAL_S 5
  #define BUCKET_S   60
#else
  #define INTERVAL_S 120
  #define BUCKET_S   3600
#endif

#define F_NOTIME 0x04

struct TimeIdx {
  uint32_t dayKey;    // YYYYMMDD
  uint8_t  wday;      // 0=Minggu .. 6=Sabtu
  uint8_t  hour;      // 0..23
  uint8_t  minute;    // 0..59
  uint16_t minOfDay;  // 0..1439
};

// =====================================================================
//  1. WAKTU & INDEKS
// =====================================================================
TimeIdx makeIdx(time_t t) {
  struct tm ti;  localtime_r(&t, &ti);
  TimeIdx x;
  x.dayKey   = (ti.tm_year + 1900) * 10000UL + (ti.tm_mon + 1) * 100 + ti.tm_mday;
  x.wday     = ti.tm_wday;
  x.hour     = ti.tm_hour;
  x.minute   = ti.tm_min;
  x.minOfDay = ti.tm_hour * 60 + ti.tm_min;
  return x;
}

bool timeValid() { return time(nullptr) > 1700000000; }   // > Nov 2023

void initTime() {
  Serial.print("[WiFi] Menyambung");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(500); Serial.print("."); }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " GAGAL");
  configTime(GMT_OFFSET, 0, "id.pool.ntp.org", "pool.ntp.org");
  struct tm ti;
  char buf[24];
  if (getLocalTime(&ti, 10000)) {
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
    Serial.printf("[NTP] %s\n", buf);
  } else {
    Serial.println("[NTP] belum sinkron, dicoba ulang otomatis");
  }
}

// =====================================================================
//  2. PROGRAM UTAMA
// =====================================================================
const char *HARI[7] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};

void setup() {
  Serial.begin(115200);
  delay(500);
  initTime();
}

void loop() {
  time_t now = time(nullptr);
  static long lastSlot = -1;
  long slot = now / INTERVAL_S;                          // slot selaras jam
  if (slot == lastSlot) { delay(20); return; }
  lastSlot = slot;

  if (!timeValid()) {                                    // jam belum benar
    Serial.printf("[F_NOTIME] epoch=%ld, menunggu NTP...\n", (long)now);
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    return;
  }
  TimeIdx x = makeIdx(now);
  Serial.printf("%s  dayKey=%lu  hour=%u  minute=%u  minOfDay=%u  slot=%ld\n",
                HARI[x.wday], (unsigned long)x.dayKey, x.hour, x.minute, x.minOfDay, slot);
}

