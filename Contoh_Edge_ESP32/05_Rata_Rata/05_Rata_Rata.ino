// =====================================================================
//  05_Rata_Rata.ino  -  Rata-rata per jam dan per 24 jam
//  Hanya data berflag 0, aturan kelengkapan 75%. DEMO_SPEED 1: "jam" = 1 menit.
//
//  Board : ESP32 Dev Module, core arduino-esp32 v1.0.6
//  Sensor: simulasi (lihat readSensors). Ganti dengan sensor asli bila ada.
//
//  Perintah Serial Monitor (115200, Newline):
//    INJ <co2|pm25|temp|rh> <nilai>  -> paksa nilai pada slot berikutnya
// =====================================================================

#include <WiFi.h>
#include "time.h"

const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const long  GMT_OFFSET = 7 * 3600;          // WIB

// DEMO_SPEED 1: sampel tiap 5 s dan "jam" = 1 menit (12 sampel), cocok untuk demo di kelas.
// DEMO_SPEED 0: kondisi sebenarnya (sampel tiap 2 menit, 30 sampel per jam).
#define DEMO_SPEED 1
#if DEMO_SPEED
  #define INTERVAL_S 5
  #define BUCKET_S   60
#else
  #define INTERVAL_S 120
  #define BUCKET_S   3600
#endif
#define SAMPLES_PER_BUCKET (BUCKET_S / INTERVAL_S)               // 30 (demo: 12)
#define MIN_VALID_BUCKET   ((SAMPLES_PER_BUCKET * 3 + 3) / 4)    // 75% -> 23 (demo: 9)

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
struct Acc { float sum = 0; uint16_t n = 0; };

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
//  4. RATA-RATA PER JAM & 24 JAM
// =====================================================================
Acc accH[NPAR];

float   hAvg[NPAR][24];      // slot ring 24 jam
uint8_t hMask[24];           // bit i = parameter i sah
time_t  hStamp[24];          // awal jam slot tsb

void addSample(const float v[NPAR], const uint8_t fl[NPAR]) {
  for (int i = 0; i < NPAR; i++)
    if (fl[i] == 0) { accH[i].sum += v[i]; accH[i].n++; }
}

uint8_t finalizeHour(float out[NPAR]) {
  uint8_t mask = 0;
  for (int i = 0; i < NPAR; i++) {
    if (accH[i].n >= MIN_VALID_BUCKET) { out[i] = accH[i].sum / accH[i].n; mask |= (1 << i); }
    else out[i] = NAN;
    accH[i] = Acc();
  }
  return mask;
}

void storeHour(uint8_t slot, time_t start, const float out[NPAR], uint8_t mask) {
  for (int i = 0; i < NPAR; i++) hAvg[i][slot] = out[i];
  hMask[slot] = mask;  hStamp[slot] = start;
}

float avg24(int p, time_t now) {
  float s = 0;  int n = 0;
  for (int h = 0; h < 24; h++) {
    bool fresh = (now - hStamp[h]) < 24L * BUCKET_S;
    if (fresh && (hMask[h] & (1 << p))) { s += hAvg[p][h]; n++; }
  }
  return (n >= 18) ? s / n : NAN;                        // >= 75% dari 24
}

long curBucket = -1;

void bucketRollover(long newBucket, time_t now) {
  if (curBucket >= 0) {
    float out[NPAR];
    uint8_t mask  = finalizeHour(out);
    time_t  start = (time_t)curBucket * BUCKET_S;
    storeHour(curBucket % 24, start, out, mask);
    float a24[NPAR];
    for (int p = 0; p < NPAR; p++) a24[p] = avg24(p, now);

    TimeIdx s = makeIdx(start);
    Serial.printf("[JAM %02u:%02u] CO2=%.0f PM2.5=%.1f T=%.2f RH=%.1f mask=0x%X\n",
                  s.hour, s.minute, out[0], out[1], out[2], out[3], mask);
    Serial.printf("[24J]      CO2=%.0f PM2.5=%.1f T=%.2f RH=%.1f\n", a24[0], a24[1], a24[2], a24[3]);

  }
  curBucket = newBucket;
}

// =====================================================================
//  5. PERINTAH SERIAL
// =====================================================================
void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.startsWith("INJ ")) {
    int sp = cmd.indexOf(' ', 4);
    String p = cmd.substring(4, sp);  float val = cmd.substring(sp + 1).toFloat();
    int idx = p == "co2" ? 0 : p == "pm25" ? 1 : p == "temp" ? 2 : p == "rh" ? 3 : -1;
    if (idx >= 0 && sp > 0) { injVal[idx] = val; injOn[idx] = true; Serial.println("[INJ] dijadwalkan"); }
    else Serial.println("Format: INJ <co2|pm25|temp|rh> <nilai>");
  }
}

// =====================================================================
//  6. PROGRAM UTAMA
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(esp_random());
  initTime();
  Serial.printf("Sampel per jam = %d, sah bila >= %d\n", SAMPLES_PER_BUCKET, MIN_VALID_BUCKET);
}

void loop() {
  handleSerial();
  time_t now = time(nullptr);
  static long lastSlot = -1;
  long slot = now / INTERVAL_S;
  if (slot == lastSlot) { delay(20); return; }
  lastSlot = slot;
  if (!timeValid()) { Serial.println("Menunggu NTP..."); return; }

  float v[NPAR];  uint8_t fl[NPAR];
  readSensors(v, fl);
  for (int i = 0; i < NPAR; i++)
    if (!(fl[i] & F_SENSOR)) fl[i] |= checkRange(v[i], SPEC[i]);

  long bucket = now / BUCKET_S;
  if (bucket != curBucket) bucketRollover(bucket, now);  // tutup jam lama dulu
  addSample(v, fl);                                      // baru tambah sampel baru
  Serial.printf("CO2=%.0f(%u) PM2.5=%.1f(%u) T=%.2f(%u) RH=%.1f(%u)\n",
                v[0], fl[0], v[1], fl[1], v[2], fl[2], v[3], fl[3]);
}

