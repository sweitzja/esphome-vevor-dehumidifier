#include "vevor_dehumidifier.h"
#include "esphome/core/log.h"

namespace esphome {
namespace vevor_dehumidifier {

static const char *const TAG = "vevor_dehumidifier";

void VevorDehumidifier::setup() {
  ESP_LOGCONFIG(TAG, "Setting up VEVOR dehumidifier component...");
  last_byte_ms_ = millis();
  last_response_ms_ = millis();
  last_polls_per_min_update_ms_ = millis();
}

void VevorDehumidifier::dump_config() {
  ESP_LOGCONFIG(TAG, "VEVOR Dehumidifier (Modbus RTU @ 9600 8N1):");
  this->check_uart_settings(9600);
  ESP_LOGCONFIG(TAG, "  Expecting slave addr 0x01, fn 0x03 responses (23 B) + fn 0x0A ACKs (8 B)");
  ESP_LOGCONFIG(TAG, "  Frame inter-byte timeout: %u ms", (unsigned) FRAME_TIMEOUT_MS);
}

void VevorDehumidifier::loop() {
  const uint32_t now = millis();

  // Drain UART into buffer
  while (this->available()) {
    uint8_t b;
    if (this->read_byte(&b)) {
      this->process_byte_(b);
      last_byte_ms_ = now;
    }
  }

  // 50ms inter-byte timeout → try parsing what we have
  if (buffer_pos_ > 0 && (now - last_byte_ms_) > FRAME_TIMEOUT_MS) {
    this->try_parse_frame_();
    buffer_pos_ = 0;
  }

  // Bus health: publish "polls/min" every 10s, and "panel_present" if poll seen in last 10s
#ifdef USE_BINARY_SENSOR
  if (panel_present_bs_ != nullptr) {
    bool present = (now - last_response_ms_) < 10000;
    if (panel_present_bs_->state != present)
      panel_present_bs_->publish_state(present);
  }
#endif

#ifdef USE_SENSOR
  if (bus_polls_per_minute_sensor_ != nullptr) {
    const uint32_t window_ms = 60000;
    if (now - last_polls_per_min_update_ms_ >= 10000) {
      // We don't keep a sliding window; just publish the most recent count
      // scaled by window. For a steadier display, restart the count each minute.
      // Simple approach: publish current count, reset on minute boundary.
      // (Acceptable for an at-a-glance "is the bus alive" display.)
      const uint32_t elapsed = now - last_polls_per_min_update_ms_;
      const float rate = (elapsed > 0) ? (valid_responses_seen_ * 60000.0f / elapsed) : 0.0f;
      bus_polls_per_minute_sensor_->publish_state(rate);
      // Reset accumulator
      valid_responses_seen_ = 0;
      last_polls_per_min_update_ms_ = now;
    }
  }
#endif
}

void VevorDehumidifier::process_byte_(uint8_t byte) {
  if (buffer_pos_ < BUFFER_SIZE) {
    buffer_[buffer_pos_++] = byte;
  } else {
    ESP_LOGW(TAG, "Frame buffer overflow, dropping");
    buffer_pos_ = 0;
  }
}

void VevorDehumidifier::try_parse_frame_() {
  if (buffer_pos_ < 4) return;  // too short for any meaningful Modbus frame

  // We only care about slave addr 0x01
  if (buffer_[0] != 0x01) {
    ESP_LOGV(TAG, "Frame with unexpected addr 0x%02X, %u B", buffer_[0], (unsigned) buffer_pos_);
    return;
  }

  const uint8_t fn = buffer_[1];

  // ===== Modbus 0x03: Read Holding Registers response =====
  // Layout: addr fn byte_count <byte_count bytes of data> crc_lo crc_hi
  if (fn == 0x03 && buffer_pos_ >= 5) {
    const uint8_t bc = buffer_[2];
    const size_t expected = 3 + bc + 2;
    if (buffer_pos_ < expected) {
      ESP_LOGW(TAG, "Short 0x03 response: %u B (expected %u for bc=%u)",
               (unsigned) buffer_pos_, (unsigned) expected, bc);
      return;
    }

    // CRC check (slave only sends ~9 registers despite master asking for 12;
    // bc is whatever the slave declared, we trust it for CRC scope)
    const uint16_t calc = crc16_modbus_(buffer_, expected - 2);
    const uint16_t recv = (uint16_t) buffer_[expected - 2] | ((uint16_t) buffer_[expected - 1] << 8);
    if (calc != recv) {
      ESP_LOGW(TAG, "CRC mismatch on 0x03 response: calc=0x%04X recv=0x%04X (%u B)",
               calc, recv, (unsigned) expected);
      return;
    }

    // We need at least 9 registers (18 bytes) to fully decode
    if (bc < 18) {
      ESP_LOGW(TAG, "0x03 response has only %u bytes of data (need 18)", bc);
      return;
    }

    // Decode (slots are 0-indexed, all 16-bit big-endian)
    setpoint_     = ((uint16_t) buffer_[3]  << 8) | buffer_[4];   // slot 0 — target RH%
    current_rh_   = ((uint16_t) buffer_[5]  << 8) | buffer_[6];   // slot 1 — current RH%
    ambient_temp_ = ((uint16_t) buffer_[7]  << 8) | buffer_[8];   // slot 2 — ambient °C
    // slot 3 (buffer_[9..10]) — reserved
    coil_temp_    = ((uint16_t) buffer_[11] << 8) | buffer_[12];  // slot 4 — coil °C
    // slot 5 (buffer_[13..14]) — reserved
    status_word_  = ((uint16_t) buffer_[15] << 8) | buffer_[16];  // slot 6 — status flags
    error_word_   = ((uint16_t) buffer_[17] << 8) | buffer_[18];  // slot 7 — alarm bitfield
    // slot 8 (buffer_[19..20]) — reserved

    has_valid_data_ = true;
    valid_responses_seen_++;
    last_response_ms_ = millis();

    ESP_LOGD(TAG, "RESP sp=%u rh=%u Ta=%uC coil=%uC s6=0x%04X s7=0x%04X",
             setpoint_, current_rh_, ambient_temp_, coil_temp_, status_word_, error_word_);

    this->publish_all_();
    return;
  }

  // ===== Slave write ACK (non-standard fn 0x0A) =====
  // Layout: addr 0x0A addr_hi addr_lo 00 01 crc_lo crc_hi  (8 B)
  if (fn == 0x0A && buffer_pos_ == 8) {
    const uint16_t calc = crc16_modbus_(buffer_, 6);
    const uint16_t recv = (uint16_t) buffer_[6] | ((uint16_t) buffer_[7] << 8);
    if (calc != recv) {
      ESP_LOGW(TAG, "CRC mismatch on 0x0A ACK: calc=0x%04X recv=0x%04X", calc, recv);
      return;
    }
    const uint16_t addr = ((uint16_t) buffer_[2] << 8) | buffer_[3];
    ESP_LOGD(TAG, "Write ACK for addr 0x%04X", addr);
    return;
  }

  ESP_LOGV(TAG, "Unrecognised %u-byte frame, fn=0x%02X", (unsigned) buffer_pos_, fn);
}

// Standard Modbus RTU CRC-16: poly 0xA001 (reflected 0x8005), init 0xFFFF
uint16_t VevorDehumidifier::crc16_modbus_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t) data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void VevorDehumidifier::publish_all_() {
  if (!has_valid_data_) return;

#ifdef USE_SENSOR
  if (target_humidity_sensor_)      target_humidity_sensor_->publish_state(setpoint_);
  if (current_humidity_sensor_)     current_humidity_sensor_->publish_state(current_rh_);
  if (ambient_temperature_sensor_)  ambient_temperature_sensor_->publish_state(ambient_temp_);
  if (coil_temperature_sensor_)     coil_temperature_sensor_->publish_state(coil_temp_);
  if (status_word_sensor_)          status_word_sensor_->publish_state(status_word_);
  if (error_word_sensor_)           error_word_sensor_->publish_state(error_word_);
#endif

#ifdef USE_BINARY_SENSOR
  const bool power      = (status_word_ & S6_BIT_POWER) != 0;
  const bool running    = (status_word_ & S6_BIT_COMP_RUNNING) != 0;
  const bool allowed    = (status_word_ & S6_BIT_COMP_ALLOWED) != 0;
  const bool demand_sat = (status_word_ & S6_BIT_DEMAND_SAT) != 0;
  const bool flood      = (status_word_ & S6_BIT_FLOOD) != 0;
  const bool latched    = (status_word_ & S6_BIT_LATCHED_ALARM) != 0;
  const bool hum_fault  = (error_word_ & S7_HUM_SIDE_MASK) != 0;
  const bool coil_fault = (error_word_ & S7_COIL_SENSOR_FAULT) != 0;

  // Derived "the unit is trying to cool" states are only meaningful when
  // the unit is logically powered on. When power = OFF, all of these should
  // report FALSE — the unit isn't trying to do anything, so it can't be
  // "calling for cooling" or "in a safety lockout."
  const bool calling        = power && !demand_sat;
  const bool lockout_safety = power && !allowed;

  if (power_bs_)              power_bs_->publish_state(power);
  if (compressor_running_bs_) compressor_running_bs_->publish_state(running);
  if (compressor_allowed_bs_) compressor_allowed_bs_->publish_state(allowed);
  if (calling_for_cooling_bs_) calling_for_cooling_bs_->publish_state(calling);
  if (lockout_safety_bs_)     lockout_safety_bs_->publish_state(lockout_safety);
  if (flood_bs_)              flood_bs_->publish_state(flood);
  if (alarm_latched_bs_)      alarm_latched_bs_->publish_state(latched);
  if (hum_sensor_fault_bs_)   hum_sensor_fault_bs_->publish_state(hum_fault);
  if (coil_sensor_fault_bs_)  coil_sensor_fault_bs_->publish_state(coil_fault);
#endif

#ifdef USE_TEXT_SENSOR
  if (mode_ts_)        mode_ts_->publish_state(this->decode_mode_str_());
  if (error_code_ts_)  error_code_ts_->publish_state(this->decode_error_code_str_());
  if (status_ts_)      status_ts_->publish_state(this->decode_status_str_());
#endif
}

std::string VevorDehumidifier::decode_mode_str_() const {
  const uint16_t m = status_word_ & S6_MODE_MASK;
  if (m & S6_MODE_SLEEP)      return "sleep";
  if (m & S6_MODE_CONTINUOUS) return "continuous";
  if (m & S6_MODE_AUTO)       return "auto";
  return "unknown";
}

std::string VevorDehumidifier::decode_error_code_str_() const {
  // Coil-side faults: E2 (coil sensor failure)
  if ((error_word_ & S7_COIL_SENSOR_FAULT) != 0) return "E2";

  // HUM-side: the panel shows different codes based on Ta / RH context,
  // but the bus uses the same bit pattern for CH/LO/E1 collectively.
  // Disambiguate using current readings.
  if ((error_word_ & S7_HUM_SIDE_FAULT) != 0) {
    // CH: ambient temp > 113°F (~45°C). Slot 2 is in °C.
    if (ambient_temp_ > 45) return "CH";
    // CL: ambient temp < 36°F (~2°C).
    if (ambient_temp_ < 2)  return "CL";
    // LO: RH < 20%
    if (current_rh_ < 20)   return "LO";
    // Otherwise treat as sensor failure
    return "E1";
  }

  return "";
}

std::string VevorDehumidifier::decode_status_str_() const {
  const bool power     = (status_word_ & S6_BIT_POWER) != 0;
  const bool running   = (status_word_ & S6_BIT_COMP_RUNNING) != 0;
  const bool allowed   = (status_word_ & S6_BIT_COMP_ALLOWED) != 0;
  const bool satisfied = (status_word_ & S6_BIT_DEMAND_SAT) != 0;
  const bool flood     = (status_word_ & S6_BIT_FLOOD) != 0;
  const bool latched   = (status_word_ & S6_BIT_LATCHED_ALARM) != 0;

  if (!power)            return "off";
  if (latched)           return "fault (latched)";
  const std::string err = this->decode_error_code_str_();
  if (!err.empty())      return std::string("fault: ") + err;
  if (flood)             return "flood";
  if (!allowed)          return "inhibit";
  if (running)           return "running";
  if (!satisfied)        return "lockout";  // calling but not running, no safety inhibit
  return "idle";
}

}  // namespace vevor_dehumidifier
}  // namespace esphome
