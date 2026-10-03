#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"

Adafruit_SHT31 sht_in = Adafruit_SHT31();

float Tempinv_1;
int Humidinv_1;

bool SHT_status;

float readTempoutv(int maxRetry = 5) {
  float t;
  for (int i = 0; i < maxRetry; i++) {
    t = sht_out.readTemperature();
    if (!isnan(t)) return t;
  }
  return -1;
}

float readHumidoutv(int maxRetry = 5) {
  float h;
  for (int i = 0; i < maxRetry; i++) {
    h = sht_out.readHumidity();
    if (!isnan(h)) return h;
  }
  return -1;
}

void SHTS() {
  if (sht_out.begin(0x44)) {
    SHT_status = 1;
  }
}

void SHTL() {
  Tempinv = readTempoutv(3);
  Humidinv_1 = readHumidoutv(3);
}