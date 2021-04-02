#pragma once

#include <Arduino.h>
#include <Wire.h>

class Bme280 {
private:
  /*
    Use predetermined struct size to decouple bme280_driver.h from Arduino
    project.
  */
  uint8_t bme280[60];

  struct {
    uint8_t temp, pres, hum, filter;
  } sampling;

public:
  bool begin(uint8_t sda, uint8_t scl, uint8_t addr = 0x76);
  bool measure(float &temp, float &pres);
  bool measure(float &temp, float &pres, float &hum);
  bool measureT(float &result);
  bool measureP(float &result);
  bool measureRH(float &result);
  void setSampling(uint8_t temp, uint8_t pres, uint8_t hum, uint8_t filter);

protected:
  bool setup(uint8_t temp, uint8_t pres, uint8_t hum, uint8_t filter);
  uint32_t calcResponseTime(uint8_t temp, uint8_t pres, uint8_t hum,
                            uint8_t filter);

  static void delay_msec(uint32_t value);
  static int8_t bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data,
                         uint8_t cnt);
  static int8_t bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data,
                          uint8_t cnt);
};
