#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome::thread_diagnostics {

class ThreadDiagnostics : public PollingComponent {
 public:
  void set_attached_binary_sensor(binary_sensor::BinarySensor *sensor) { this->attached_binary_sensor_ = sensor; }
  void set_srp_client_running_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->srp_client_running_binary_sensor_ = sensor;
  }

  void set_role_text_sensor(text_sensor::TextSensor *sensor) { this->role_text_sensor_ = sensor; }
  void set_rloc16_text_sensor(text_sensor::TextSensor *sensor) { this->rloc16_text_sensor_ = sensor; }
  void set_parent_rloc16_text_sensor(text_sensor::TextSensor *sensor) { this->parent_rloc16_text_sensor_ = sensor; }
  void set_partition_id_text_sensor(text_sensor::TextSensor *sensor) { this->partition_id_text_sensor_ = sensor; }
  void set_srp_host_state_text_sensor(text_sensor::TextSensor *sensor) { this->srp_host_state_text_sensor_ = sensor; }

  void set_attach_duration_sensor(sensor::Sensor *sensor) { this->attach_duration_sensor_ = sensor; }
  void set_parent_average_rssi_sensor(sensor::Sensor *sensor) { this->parent_average_rssi_sensor_ = sensor; }
  void set_parent_link_quality_sensor(sensor::Sensor *sensor) { this->parent_link_quality_sensor_ = sensor; }
  void set_channel_sensor(sensor::Sensor *sensor) { this->channel_sensor_ = sensor; }
  void set_parent_changes_sensor(sensor::Sensor *sensor) { this->parent_changes_sensor_ = sensor; }
  void set_attach_attempts_sensor(sensor::Sensor *sensor) { this->attach_attempts_sensor_ = sensor; }
  void set_ipv6_tx_failures_sensor(sensor::Sensor *sensor) { this->ipv6_tx_failures_sensor_ = sensor; }
  void set_ipv6_rx_failures_sensor(sensor::Sensor *sensor) { this->ipv6_rx_failures_sensor_ = sensor; }
  void set_mac_tx_retries_sensor(sensor::Sensor *sensor) { this->mac_tx_retries_sensor_ = sensor; }
  void set_mac_cca_failures_sensor(sensor::Sensor *sensor) { this->mac_cca_failures_sensor_ = sensor; }
  void set_mac_rx_fcs_errors_sensor(sensor::Sensor *sensor) { this->mac_rx_fcs_errors_sensor_ = sensor; }

  void update() override;
  void dump_config() override;

 protected:
  binary_sensor::BinarySensor *attached_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *srp_client_running_binary_sensor_{nullptr};

  text_sensor::TextSensor *role_text_sensor_{nullptr};
  text_sensor::TextSensor *rloc16_text_sensor_{nullptr};
  text_sensor::TextSensor *parent_rloc16_text_sensor_{nullptr};
  text_sensor::TextSensor *partition_id_text_sensor_{nullptr};
  text_sensor::TextSensor *srp_host_state_text_sensor_{nullptr};

  sensor::Sensor *attach_duration_sensor_{nullptr};
  sensor::Sensor *parent_average_rssi_sensor_{nullptr};
  sensor::Sensor *parent_link_quality_sensor_{nullptr};
  sensor::Sensor *channel_sensor_{nullptr};
  sensor::Sensor *parent_changes_sensor_{nullptr};
  sensor::Sensor *attach_attempts_sensor_{nullptr};
  sensor::Sensor *ipv6_tx_failures_sensor_{nullptr};
  sensor::Sensor *ipv6_rx_failures_sensor_{nullptr};
  sensor::Sensor *mac_tx_retries_sensor_{nullptr};
  sensor::Sensor *mac_cca_failures_sensor_{nullptr};
  sensor::Sensor *mac_rx_fcs_errors_sensor_{nullptr};
};

}  // namespace esphome::thread_diagnostics
