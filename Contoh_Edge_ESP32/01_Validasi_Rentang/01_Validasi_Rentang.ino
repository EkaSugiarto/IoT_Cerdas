// =====================================================================
//  01_Validasi_Rentang.ino  -  Validasi rentang ukur sensor
//  Nilai di luar rentang datasheet diberi flag F_RANGE, gagal baca F_SENSOR.
//
//  Board : ESP32 Dev Module, core arduino-esp32 v1.0.6
//  Sensor: simulasi (lihat readSensors). Ganti dengan sensor asli bila ada.
//
//  Perintah Serial Monitor (115200, Newline):
//    INJ <co2|pm25|temp|rh> <nilai>  -> paksa nilai pada slot berikutnya
// =====================================================================

#define INTERVAL_S 5                        // demo: sampel tiap 5 s

#define NPAR 4                              // 0=CO2 1=PM2.5 2=Suhu 3=RH
const char *PNAME[NPAR] = {"CO2", "PM2.5", "Suhu", "RH"};

#define F_RANGE   0x01
#define F_OUTLIER 0x02
#define F_NOTIME  0x04
#define F_SENSOR  0x08

struct Spec { float minV, maxV; };
const Spec SPEC[NPAR] = {{0, 5000}, {0, 1000}, {-40, 125}, {0, 100}};

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
//  3. PERINTAH SERIAL
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
//  4. PROGRAM UTAMA
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(esp_random());
  Serial.println("Format: nilai(flag). Flag 1 = F_RANGE, 8 = F_SENSOR");
}

void loop() {
  handleSerial();
  static unsigned long last = 0;
  if (millis() - last < INTERVAL_S * 1000UL) return;     // jalan sekali per interval
  last = millis();

  float v[NPAR];  uint8_t fl[NPAR];
  readSensors(v, fl);
  for (int i = 0; i < NPAR; i++)
    if (!(fl[i] & F_SENSOR)) fl[i] |= checkRange(v[i], SPEC[i]);
  Serial.printf("CO2=%.0f(%u) PM2.5=%.1f(%u) T=%.2f(%u) RH=%.1f(%u)\n",
                v[0], fl[0], v[1], fl[1], v[2], fl[2], v[3], fl[3]);
}

