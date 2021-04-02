#include "BME280.h"
#include "bme280_driver.h"

#if DEBUG
#define dprintf(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define dprintf(format, ...)
#endif

bool Bme280::begin(uint8_t sda, uint8_t scl, uint8_t addr) {
  static_assert(sizeof(this->bme280) == sizeof(struct bme280_t),
                "struct bme280_t size changed!");
  Wire.begin(sda, scl);

  sampling = {.temp = BME280_OVERSAMP_4X,
              .pres = BME280_OVERSAMP_2X,
              .hum = BME280_OVERSAMP_2X,
              .filter = BME280_FILTER_COEFF_OFF};

  struct bme280_t &bme280 = *(struct bme280_t *)this->bme280;
  bme280.dev_addr = addr;
  bme280.delay_msec = Bme280::delay_msec;
  bme280.bus_write = Bme280::bus_write;
  bme280.bus_read = Bme280::bus_read;

  if (bme280_init(&bme280)) {
    dprintf("bme280_init failed\n");
    return false;
  }

  return true;
}

/* ************************************************************************** */

bool Bme280::measure(float &temp, float &pres) {
  if (!this->setup(sampling.temp, sampling.pres, BME280_OVERSAMP_SKIPPED,
                   sampling.filter)) {
    dprintf("sampling setup failed\n");
    return false;
  }

  s32 uncomp_temp, uncomp_pres;
  if (bme280_read_uncomp_pressure_temperature(&uncomp_pres, &uncomp_temp)) {
    dprintf("bme280_read_uncomp_pressure_temperature(&uncomp_pres, "
            "&uncomp_temp) failed\n");
    return false;
  }

  temp = (float)bme280_compensate_temperature_double(uncomp_temp);
  pres = (float)bme280_compensate_pressure_double(uncomp_pres);

  return true;
}

bool Bme280::measure(float &temp, float &pres, float &hum) {
  if (!this->setup(sampling.temp, sampling.pres, sampling.hum,
                   sampling.filter)) {
    dprintf("sampling setup failed\n");
    return false;
  }

  s32 uncomp_temp, uncomp_pres, uncomp_hum;
  if (bme280_read_uncomp_pressure_temperature_humidity(
          &uncomp_pres, &uncomp_temp, &uncomp_hum)) {
    dprintf("bme280_read_uncomp_pressure_temperature_humidity(&uncomp_pres, "
            "&uncomp_temp, &uncomp_hum) failed\n");
    return false;
  }

  temp = (float)bme280_compensate_temperature_double(uncomp_temp);
  pres = (float)bme280_compensate_pressure_double(uncomp_pres);
  hum = (float)bme280_compensate_humidity_double(uncomp_hum);

  return true;
}

bool Bme280::measureT(float &result) {
  if (!this->setup(sampling.temp, BME280_OVERSAMP_SKIPPED,
                   BME280_OVERSAMP_SKIPPED, sampling.filter)) {
    dprintf("sampling setup failed\n");
    return false;
  }

  s32 uncomp;
  if (bme280_read_uncomp_temperature(&uncomp)) {
    dprintf("bme280_read_uncomp_temperature(&uncomp) failed\n");
    return false;
  }
  result = (float)bme280_compensate_temperature_double(uncomp);

  return true;
}

bool Bme280::measureP(float &result) {
  if (!this->setup(BME280_OVERSAMP_SKIPPED, sampling.pres,
                   BME280_OVERSAMP_SKIPPED, sampling.filter)) {
    dprintf("sampling setup failed\n");
    return false;
  }

  s32 uncomp;
  if (bme280_read_uncomp_pressure(&uncomp)) {
    dprintf("bme280_read_uncomp_pressure(&uncomp) failed\n");
    return false;
  }
  result = (float)bme280_compensate_pressure_double(uncomp);

  return true;
}

bool Bme280::measureRH(float &result) {
  if (!this->setup(BME280_OVERSAMP_SKIPPED, BME280_OVERSAMP_SKIPPED,
                   sampling.hum, sampling.filter)) {
    dprintf("sampling setup failed\n");
    return false;
  }

  s32 uncomp;
  if (bme280_read_uncomp_humidity(&uncomp)) {
    dprintf("bme280_read_uncomp_humidity(&uncomp) failed\n");
    return false;
  }
  result = (float)bme280_compensate_humidity_double(uncomp);

  return true;
}

/* ************************************************************************** */

void Bme280::setSampling(uint8_t temp, uint8_t pres, uint8_t hum,
                         uint8_t filter) {
  sampling = {.temp = temp, .pres = pres, .hum = hum, .filter = filter};
}

bool Bme280::setup(uint8_t temp, uint8_t pres, uint8_t hum, uint8_t filter) {
  if (bme280_set_oversamp_temperature(temp) ||
      bme280_set_oversamp_pressure(pres) || bme280_set_oversamp_humidity(hum)) {
    dprintf("bme280_set_oversamp(...) failed\n");
    return false;
  }

  if (bme280_set_filter(filter)) {
    dprintf("bme280_set_filter(%d) failed\n", filter);
    return false;
  }

  if (bme280_set_power_mode(BME280_FORCED_MODE)) {
    dprintf("bme280_set_power_mode(BME280_FORCED_MODE) failed\n");
    return false;
  }

  delay(calcResponseTime(temp, pres, hum, filter));

  return true;
}

uint32_t Bme280::calcResponseTime(uint8_t temp, uint8_t pres, uint8_t hum,
                                  uint8_t filter) {
  auto filterCoeff = [](int x) -> float {
    switch (x) {
    case BME280_FILTER_COEFF_OFF:
      return 1;
    case BME280_FILTER_COEFF_2:
      return 2;
    case BME280_FILTER_COEFF_4:
      return 5;
    case BME280_FILTER_COEFF_8:
      return 11;
    case BME280_FILTER_COEFF_16:
      return 22;
    default:
      return 1;
    }
  };
  auto oversampCoeff = [](int x) -> float {
    switch (x) {
    case BME280_OVERSAMP_SKIPPED:
      return 0;
    case BME280_OVERSAMP_1X:
      return 1;
    case BME280_OVERSAMP_2X:
      return 2;
    case BME280_OVERSAMP_4X:
      return 4;
    case BME280_OVERSAMP_8X:
      return 8;
    case BME280_OVERSAMP_16X:
      return 16;
    default:
      return 0;
    }
  };
  const auto r = 1.25 + (2.3 * oversampCoeff(temp)) +
                 (2.3 * oversampCoeff(pres) + 0.575) +
                 (2.3 * oversampCoeff(hum) + 0.575);
  return (uint32_t)ceilf(r * filterCoeff(filter));
}

/* ************************************************************************** */

void Bme280::delay_msec(uint32_t value) { delay(value); }

int8_t Bme280::bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data,
                        uint8_t cnt) {
  if (Bme280::bus_write(dev_addr, reg_addr, nullptr, 0)) {
    dprintf("Bme280::bus_write(%x, %x, -, -) failed\n", dev_addr, reg_addr);
    return -1;
  }
  const auto l = Wire.requestFrom(dev_addr, cnt);
  if (l != cnt) {
    dprintf("Wire.requestFrom(%x, %d) failed, %d =/= %d\n", dev_addr, cnt, l,
            cnt);
    return -1;
  }
  for (int i = 0; i < cnt; i += 1) {
    reg_data[i] = (uint8_t)Wire.read();
  }
  return 0;
}

int8_t Bme280::bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data,
                         uint8_t cnt) {
  Wire.beginTransmission(dev_addr);
  if (Wire.write(reg_addr) != 1) {
    dprintf("Wire.write(%d) failed\n", reg_addr);
    return -1;
  }
  const auto l = Wire.write(reg_data, cnt);
  if (l != cnt) {
    dprintf("Wire.write(%p, %d) failed, %d =/= %d\n", reg_data, cnt, l, cnt);
    return -1;
  }
  const auto r = Wire.endTransmission();
  if (r != 0) {
    dprintf("Wire.endTransmission failed, err = %d\n", r);
    return -1;
  }
  return 0;
}
