#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace vevor_dehumidifier {

// Slot 6 (status word) bit masks
static const uint16_t S6_BIT_POWER          = 0x8000;  // unit powered on
static const uint16_t S6_BIT_DEMAND_SAT     = 0x4000;  // 1 = no cooling needed; 0 = calling
static const uint16_t S6_BIT_LATCHED_ALARM  = 0x1000;  // sticky alarm (e.g. sustained CH)
static const uint16_t S6_BIT_COMP_ALLOWED   = 0x0800;  // safety: allowed to run compressor
static const uint16_t S6_BIT_COMP_RUNNING   = 0x0200;  // compressor relay closed
static const uint16_t S6_BIT_FLOOD          = 0x0001;  // external flood-contact closed
static const uint16_t S6_MODE_AUTO          = 0x0040;
static const uint16_t S6_MODE_CONTINUOUS    = 0x0020;
static const uint16_t S6_MODE_SLEEP         = 0x0010;
static const uint16_t S6_MODE_MASK          = 0x0070;

// Slot 7 (error word) bit masks
static const uint16_t S7_HUM_SIDE_FAULT     = 0xC000;  // HUM RH+T sensor or ambient out-of-range
static const uint16_t S7_HUM_SIDE_MASK      = 0xFF00;
static const uint16_t S7_COIL_SENSOR_FAULT  = 0x0080;  // coil NTC fault (E2)
static const uint16_t S7_COIL_SIDE_MASK     = 0x00FF;

class VevorDehumidifier : public Component, public uart::UARTDevice {
 public:
  // --- ESPHome lifecycle ---
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

#ifdef USE_SENSOR
  void set_target_humidity_sensor(sensor::Sensor *s) { target_humidity_sensor_ = s; }
  void set_current_humidity_sensor(sensor::Sensor *s) { current_humidity_sensor_ = s; }
  void set_ambient_temperature_sensor(sensor::Sensor *s) { ambient_temperature_sensor_ = s; }
  void set_coil_temperature_sensor(sensor::Sensor *s) { coil_temperature_sensor_ = s; }
  void set_status_word_sensor(sensor::Sensor *s) { status_word_sensor_ = s; }
  void set_error_word_sensor(sensor::Sensor *s) { error_word_sensor_ = s; }
  void set_bus_polls_per_minute_sensor(sensor::Sensor *s) { bus_polls_per_minute_sensor_ = s; }
#endif

#ifdef USE_BINARY_SENSOR
  void set_power_bs(binary_sensor::BinarySensor *bs) { power_bs_ = bs; }
  void set_compressor_running_bs(binary_sensor::BinarySensor *bs) { compressor_running_bs_ = bs; }
  void set_compressor_allowed_bs(binary_sensor::BinarySensor *bs) { compressor_allowed_bs_ = bs; }
  void set_calling_for_cooling_bs(binary_sensor::BinarySensor *bs) { calling_for_cooling_bs_ = bs; }
  void set_lockout_safety_bs(binary_sensor::BinarySensor *bs) { lockout_safety_bs_ = bs; }
  void set_flood_bs(binary_sensor::BinarySensor *bs) { flood_bs_ = bs; }
  void set_alarm_latched_bs(binary_sensor::BinarySensor *bs) { alarm_latched_bs_ = bs; }
  void set_hum_sensor_fault_bs(binary_sensor::BinarySensor *bs) { hum_sensor_fault_bs_ = bs; }
  void set_coil_sensor_fault_bs(binary_sensor::BinarySensor *bs) { coil_sensor_fault_bs_ = bs; }
  void set_panel_present_bs(binary_sensor::BinarySensor *bs) { panel_present_bs_ = bs; }
#endif

#ifdef USE_TEXT_SENSOR
  void set_mode_text_sensor(text_sensor::TextSensor *ts) { mode_ts_ = ts; }
  void set_error_code_text_sensor(text_sensor::TextSensor *ts) { error_code_ts_ = ts; }
  void set_status_text_sensor(text_sensor::TextSensor *ts) { status_ts_ = ts; }
#endif

 protected:
  // Modbus RTU frame buffering / parsing
  void process_byte_(uint8_t byte);
  void try_parse_frame_();
  static uint16_t crc16_modbus_(const uint8_t *data, size_t len);

  // Publish helpers
  void publish_all_();
  std::string decode_mode_str_() const;
  std::string decode_error_code_str_() const;
  std::string decode_status_str_() const;

  // Frame buffer
  static const size_t BUFFER_SIZE = 64;
  uint8_t buffer_[BUFFER_SIZE]{};
  size_t buffer_pos_{0};
  uint32_t last_byte_ms_{0};
  static const uint32_t FRAME_TIMEOUT_MS = 50;

  // Latest decoded values (from a valid Modbus 0x03 response)
  uint16_t setpoint_{0};
  uint16_t current_rh_{0};
  uint16_t ambient_temp_{0};
  uint16_t coil_temp_{0};
  uint16_t status_word_{0};
  uint16_t error_word_{0};
  bool has_valid_data_{false};

  // Bus health tracking
  uint32_t valid_responses_seen_{0};
  uint32_t last_response_ms_{0};
  uint32_t last_polls_per_min_update_ms_{0};

#ifdef USE_SENSOR
  sensor::Sensor *target_humidity_sensor_{nullptr};
  sensor::Sensor *current_humidity_sensor_{nullptr};
  sensor::Sensor *ambient_temperature_sensor_{nullptr};
  sensor::Sensor *coil_temperature_sensor_{nullptr};
  sensor::Sensor *status_word_sensor_{nullptr};
  sensor::Sensor *error_word_sensor_{nullptr};
  sensor::Sensor *bus_polls_per_minute_sensor_{nullptr};
#endif

#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *power_bs_{nullptr};
  binary_sensor::BinarySensor *compressor_running_bs_{nullptr};
  binary_sensor::BinarySensor *compressor_allowed_bs_{nullptr};
  binary_sensor::BinarySensor *calling_for_cooling_bs_{nullptr};
  binary_sensor::BinarySensor *lockout_safety_bs_{nullptr};
  binary_sensor::BinarySensor *flood_bs_{nullptr};
  binary_sensor::BinarySensor *alarm_latched_bs_{nullptr};
  binary_sensor::BinarySensor *hum_sensor_fault_bs_{nullptr};
  binary_sensor::BinarySensor *coil_sensor_fault_bs_{nullptr};
  binary_sensor::BinarySensor *panel_present_bs_{nullptr};
#endif

#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *mode_ts_{nullptr};
  text_sensor::TextSensor *error_code_ts_{nullptr};
  text_sensor::TextSensor *status_ts_{nullptr};
#endif
};

}  // namespace vevor_dehumidifier
}  // namespace esphome
