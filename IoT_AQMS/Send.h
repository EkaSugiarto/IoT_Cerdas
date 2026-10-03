// =====================================================================
//  Send.h  -  Penyusun JSON + HTTPS POST ke server
//  Butuh dideklarasikan lebih dulu di .ino:
//  device_name, api_rndbirulangit, raw_2m, client_birulangit
// =====================================================================
String jsonString;

void addJson(String key, String value) {
  if (jsonString.length() > 0) jsonString += ",";
  jsonString += "\"" + key + "\":\"" + value + "\"";
}

String getJson() {
  return "{" + jsonString + "}";
}

// Kirim isi jsonString ke url, maksimal 5 percobaan
bool postJson(const String &tag, const String &url) {
  bool success = false;
  HTTPClient https;
  if (https.begin(*client_birulangit, url)) {
    https.setUserAgent(device_name);
    https.addHeader("Content-Type", "application/json");
    String body = getJson();
    Serial.println("[" + tag + "] " + body);
    for (int attempt = 1; attempt <= 5 && !success; attempt++) {
      int code = https.POST(body);
      Serial.printf("[%s] percobaan %d -> kode %d\n", tag.c_str(), attempt, code);
      success = (code == 200 || code == 201);
      if (!success && attempt < 5) delay(1000);
    }
    https.end();
  }
  jsonString = "";          // kosongkan untuk pengiriman berikutnya
  return success;
}

bool Send_rndbirulangit() {
  addJson("device_1",      device_name);
  addJson("RSSI_1",        raw_2m.RSSI);
  addJson("created_at_1",  raw_2m.created_at);
  addJson("Uptime_1",      raw_2m.uptime);
  addJson("SD_status_1",   raw_2m.SD_status);
  addJson("PM25_status_1", raw_2m.PM25_status);
  addJson("SHT_status_1",  raw_2m.SHT_status);
  addJson("CO2v_1",        raw_2m.CO2v);
  addJson("PM25v_1",       raw_2m.PM25v);
  addJson("Tempinv_1",     raw_2m.Tempinv);
  addJson("Humidinv_1",    raw_2m.Humidinv);
  return postJson("RnD BiruLangit", api_rndbirulangit);
}
