// =====================================================================
//  Date_Time.h  -  RTC internal ESP32 + sinkronisasi waktu dari server
//  Library: ESP32Time (fbiego) -> install lewat Library Manager
//  Butuh dideklarasikan lebih dulu di .ino: device_name, WiFi, HTTPClient
// =====================================================================
#include <ESP32Time.h>

ESP32Time rtc(0);   // offset 0: server sudah mengirim waktu WIB

const String api_time = "https://rndbirulangit.pythonanywhere.com/api/get-time";

// 0 = "YYYY-MM-DD HH:MM:SS", 1 = "YYYY-MM-DD", 2 = "HH:MM:SS"
String get_date_time(int choose) {
  char buf[20];
  if (choose == 1)
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             rtc.getYear(), rtc.getMonth() + 1, rtc.getDay());   // getMonth(): 0-11
  else if (choose == 2)
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  else
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             rtc.getYear(), rtc.getMonth() + 1, rtc.getDay(),
             rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  return String(buf);
}

// Waktu sekarang, tetapi detiknya dipaksa = seconds
// get_precission_second(0) -> "2026-10-03 14:36:00" (awal slot)
String get_precission_second(int seconds) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           rtc.getYear(), rtc.getMonth() + 1, rtc.getDay(),
           rtc.getHour(true), rtc.getMinute(), seconds);
  return String(buf);
}

// Lama perangkat menyala sejak boot, format "HH:MM:SS"
String get_uptime() {
  unsigned long totalSeconds = millis() / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           totalSeconds / 3600, (totalSeconds % 3600) / 60, totalSeconds % 60);
  return String(buf);
}

// p = {"datetime": "2026-10-03 14:35:12", ...}
bool setRtcFromJson(const String &p) {
  int k = p.indexOf("\"datetime\":");
  if (k == -1) return false;
  int a = p.indexOf("\"", k + 11) + 1;     // awal nilai datetime
  String d = p.substring(a, a + 19);       // 19 karakter
  if (d.length() != 19) return false;
  rtc.setTime(d.substring(17, 19).toInt(),    // detik
              d.substring(14, 16).toInt(),    // menit
              d.substring(11, 13).toInt(),    // jam
              d.substring(8, 10).toInt(),     // tanggal
              d.substring(5, 7).toInt(),      // bulan 1-12
              d.substring(0, 4).toInt());     // tahun
  return true;
}

bool Sync_RTC() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (!https.begin(client, api_time)) return false;
  https.setUserAgent(device_name);

  unsigned long t0 = millis();
  int code = https.GET();
  unsigned long rtt = millis() - t0;

  bool ok = (code >= 200 && code <= 299)
            && setRtcFromJson(https.getString());
  https.end();
  Serial.printf("[RTC] %s kode=%d RTT=%lu ms\n",
                get_date_time(0).c_str(), code, rtt);
  return ok;
}
