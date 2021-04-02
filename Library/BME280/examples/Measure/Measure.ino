#include <BME280.h>

Bme280 bme280;

void setup() {
  Serial.begin(115200);
  // The default is (SDA, SCL) == (D2, D1) == (4, 5)
  // For ESP-01 use (0, 2)
  if (!bme280.begin(SDA, SCL)) {
    Serial.printf("BME280 init failed.\n");
  }
}

void loop() {
  float temp, hum, pres;
  if (bme280.measure(temp, pres, hum)) {
    Serial.printf("Temperature = %0.2f C, Pressure = %0.2f Pa, Humidity = %0.2f%%\n", temp, pres, hum);
  } else {
    Serial.printf("Measurement failed.\n");
  }

  if (bme280.measureT(temp)) {
    Serial.printf("Temperature = %0.2f C  ", temp);
  }
  if (bme280.measureP(pres)) {
    Serial.printf("Pressure = %0.2f Pa  ", pres);
  }
  if (bme280.measureRH(hum)) {
    Serial.printf("Humidity = %0.2f%%", hum);
  }
  Serial.printf("\n");

  delay(1000);
}