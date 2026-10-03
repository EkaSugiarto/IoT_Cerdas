// =====================================================================
//  04_Penyimpanan_SPIFFS.ino  -  Penyimpanan data di SPIFFS
//  Record per slot ke /m<YYYYMMDD>.csv, rotasi file terlama saat > 75%.
//
//  Board : ESP32 Dev Module, core arduino-esp32 v1.0.6
//  Sensor: simulasi (lihat readSensors). Ganti dengan sensor asli bila ada.
//
//  Perintah Serial Monitor (115200, Newline):
//    LS                             -> daftar file SPIFFS
//    DUMP <nama file>               -> cetak isi file
// =====================================================================

#include <WiFi.h>
#include "time.h"
#include "SPIFFS.h"

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

#define NPAR 4                              // 0=CO2 1=PM2.5 2=Suhu 3=RH
const char *PNAME[NPAR] = {"CO2", "PM2.5", "Suhu", "RH"};

#define F_RANGE   0x01
#define F_OUTLIER 0x02
#define F_NOTIME  0x04
#define F_SENSOR  0x08

struct Spec { float minV, maxV; };
const Spec SPEC[NPAR] = {{0, 5000}, {0, 1000}, {-40, 125}, {0, 100}};

struct TimeIdx {
  uint32_t dayKey;    // YYYYMMDD
  uint8_t  wday;      // 0=Minggu .. 6=Sabtu
  uint8_t  hour;      // 0..23
  uint8_t  minute;    // 0..59
  uint16_t minOfDay;  // 0..1439
};

const float QUOTA_MAX = 0.75;               // batas pemakaian SPIFFS

// =====================================================================
//  1. SENSOR (SIMULASI)
// =====================================================================
float injVal[NPAR];
bool  injOn[NPAR] = {false, false, false, false};

float rnd(float a, float b) { return a + (b - a) * (random(10001) / 10000.0); }

// Mengisi v[] dengan nilai sensor dan fl[] dengan F_SENSOR bila gagal.
// Ganti isi fungsi ini dengan pembacaan MH-Z19B, PMS5003, dan SHT31.
void readSensors(float v[NPAR], uint8_t fl[NPAR]) {
  float t = millis() / 60000.0;                          // menit sejak boot
  v[0] = 450 + 40 * sin(t / 30.0) + rnd(-8, 8);          // ppm
  v[1] = 25  +  8 * sin(t / 45.0) + rnd(-2, 2);          // ug/m3
  v[2] = 28  +  2 * sin(t / 60.0) + rnd(-0.1, 0.1);      // degC
  v[3] = 70  -  6 * sin(t / 60.0) + rnd(-0.5, 0.5);      // %RH

  const float spike[NPAR]  = {800, 80, 8, 20};           // lonjakan outlier
  const float outVal[NPAR] = {6000, 1500, 130, 120};     // di luar rentang
  for (int i = 0; i < NPAR; i++) {
    fl[i] = 0;
    if (random(100) < 3)  { v[i] += spike[i]; Serial.printf("[SIM] OUTLIER %s\n", PNAME[i]); }
    if (random(1000) < 5) { v[i] = outVal[i]; Serial.printf("[SIM] RANGE %s\n", PNAME[i]); }
    if (injOn[i])         { v[i] = injVal[i]; injOn[i] = false; Serial.printf("[INJ] %s = %.2f\n", PNAME[i], v[i]); }
  }
}

// =====================================================================
//  2. VALIDASI RENTANG
// =====================================================================
uint8_t checkRange(float v, const Spec &s) {
  if (isnan(v)) return F_SENSOR;
  if (v < s.minV || v > s.maxV) return F_RANGE;
  return 0;
}

// =====================================================================
//  3. WAKTU & INDEKS
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
//  4. SPIFFS
// =====================================================================
bool fsOK = false;

void initFS() {
  fsOK = SPIFFS.begin(true);                             // format bila gagal mount
  Serial.printf("[FS] SPIFFS %s, terpakai %u / %u byte\n", fsOK ? "OK" : "GAGAL",
                (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
}

void appendLine(const char *path, const char *header, const char *line) {
  if (!fsOK) return;
  bool isNew = !SPIFFS.exists(path);
  File f = SPIFFS.open(path, FILE_APPEND);
  if (!f) return;
  if (isNew) f.println(header);
  f.print(line);
  f.close();
}

void appendRecord(const TimeIdx &x, time_t t, const float v[NPAR], const uint8_t fl[NPAR]) {
  char path[24], line[96];
  snprintf(path, sizeof(path), "/m%lu.csv", (unsigned long)x.dayKey);
  snprintf(line, sizeof(line), "%ld,%.0f,%.1f,%.2f,%.1f,%u,%u,%u,%u\n",
           (long)t, v[0], v[1], v[2], v[3], fl[0], fl[1], fl[2], fl[3]);
  appendLine(path, "epoch,co2,pm25,temp,rh,fCO2,fPM,fT,fRH", line);
}

// Hapus file terlama (berdasarkan tanggal di nama) bila pemakaian > QUOTA_MAX
void enforceQuota() {
  while (fsOK && (float)SPIFFS.usedBytes() / SPIFFS.totalBytes() > QUOTA_MAX) {
    File root = SPIFFS.open("/");
    String oldest = "";
    File f = root.openNextFile();
    while (f) {
      String n = f.name();                               // core 1.0.6: "/m20261003.csv"
      if ((n.startsWith("/m") || n.startsWith("/h")) &&
          (oldest == "" || n.substring(2) < oldest.substring(2))) oldest = n;
      f = root.openNextFile();
    }
    if (oldest == "") break;
    Serial.println("[FS] hapus " + oldest);
    SPIFFS.remove(oldest);
  }
}

// =====================================================================
//  5. PERINTAH SERIAL
// =====================================================================
void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd == "LS") {
    File root = SPIFFS.open("/");
    File f = root.openNextFile();
    while (f) { Serial.printf("%s  %u byte\n", f.name(), (unsigned)f.size()); f = root.openNextFile(); }
    Serial.printf("Terpakai %u / %u byte\n", (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
  } else if (cmd.startsWith("DUMP ")) {
    File f = SPIFFS.open(cmd.substring(5), FILE_READ);
    if (!f) { Serial.println("File tidak ada"); return; }
    while (f.available()) Serial.write(f.read());
    f.close();
  }
}

// =====================================================================
//  6. PROGRAM UTAMA
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(esp_random());
  initFS();
  initTime();
}

void loop() {
  handleSerial();
  time_t now = time(nullptr);
  static long lastSlot = -1;
  long slot = now / INTERVAL_S;
  if (slot == lastSlot) { delay(20); return; }
  lastSlot = slot;
  if (!timeValid()) { Serial.println("Menunggu NTP..."); return; }

  TimeIdx x = makeIdx(now);
  float v[NPAR];  uint8_t fl[NPAR];
  readSensors(v, fl);
  for (int i = 0; i < NPAR; i++)
    if (!(fl[i] & F_SENSOR)) fl[i] |= checkRange(v[i], SPEC[i]);

  appendRecord(x, now, v, fl);
  Serial.printf("[TULIS] /m%lu.csv  CO2=%.0f PM2.5=%.1f T=%.2f RH=%.1f\n",
                (unsigned long)x.dayKey, v[0], v[1], v[2], v[3]);
  if (x.minute == 0 && now % 60 < INTERVAL_S) enforceQuota();   // cek kuota tiap jam
}

