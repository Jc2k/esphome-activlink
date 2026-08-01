#include "thread_diagnostics.h"

#include "esphome/core/log.h"

#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <freertos/FreeRTOS.h>
#include <openthread/link.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>

#include <cinttypes>
#include <cstdio>
#include <limits>
#include <string>

namespace esphome::thread_diagnostics {

static const char *const TAG = "thread_diagnostics";

namespace {

struct ThreadSnapshot {
  otDeviceRole role{OT_DEVICE_ROLE_DISABLED};
  uint16_t rloc16{0};
  uint16_t parent_rloc16{0};
  uint32_t partition_id{0};
  uint32_t attach_duration{0};
  int8_t parent_average_rssi{0};
  uint8_t parent_link_quality{0};
  uint8_t channel{0};
  uint16_t parent_changes{0};
  uint16_t attach_attempts{0};
  uint32_t ipv6_tx_failures{0};
  uint32_t ipv6_rx_failures{0};
  uint32_t mac_tx_retries{0};
  uint32_t mac_cca_failures{0};
  uint32_t mac_rx_fcs_errors{0};
  bool parent_valid{false};
  bool partition_valid{false};
  bool srp_client_running{false};
  std::string srp_host_state{"disabled"};
};

const char *role_to_string(otDeviceRole role) {
  switch (role) {
    case OT_DEVICE_ROLE_DISABLED:
      return "disabled";
    case OT_DEVICE_ROLE_DETACHED:
      return "detached";
    case OT_DEVICE_ROLE_CHILD:
      return "child";
    case OT_DEVICE_ROLE_ROUTER:
      return "router";
    case OT_DEVICE_ROLE_LEADER:
      return "leader";
    default:
      return "unknown";
  }
}

bool role_is_attached(otDeviceRole role) {
  return role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER;
}

std::string format_hex16(uint16_t value) {
  char buffer[7];
  std::snprintf(buffer, sizeof(buffer), "0x%04" PRIX16, value);
  return buffer;
}

std::string format_hex32(uint32_t value) {
  char buffer[11];
  std::snprintf(buffer, sizeof(buffer), "0x%08" PRIX32, value);
  return buffer;
}

}  // namespace

void ThreadDiagnostics::update() {
  if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(100))) {
    ESP_LOGW(TAG, "Could not acquire the OpenThread lock");
    this->status_set_warning();
    return;
  }

  ThreadSnapshot snapshot;
  otInstance *instance = esp_openthread_get_instance();
  if (instance == nullptr) {
    esp_openthread_lock_release();
    ESP_LOGW(TAG, "OpenThread instance is not available");
    this->status_set_warning();
    return;
  }

  snapshot.role = otThreadGetDeviceRole(instance);
  const bool attached = role_is_attached(snapshot.role);
  snapshot.rloc16 = otThreadGetRloc16(instance);
  snapshot.attach_duration = otThreadGetCurrentAttachDuration(instance);
  snapshot.channel = otLinkGetChannel(instance);

  if (attached) {
    otLeaderData leader_data{};
    if (otThreadGetLeaderData(instance, &leader_data) == OT_ERROR_NONE) {
      snapshot.partition_id = leader_data.mPartitionId;
      snapshot.partition_valid = true;
    }
  }

  if (snapshot.role == OT_DEVICE_ROLE_CHILD) {
    otRouterInfo parent_info{};
    int8_t parent_average_rssi = 0;
    if (otThreadGetParentInfo(instance, &parent_info) == OT_ERROR_NONE &&
        otThreadGetParentAverageRssi(instance, &parent_average_rssi) == OT_ERROR_NONE) {
      snapshot.parent_rloc16 = parent_info.mRloc16;
      snapshot.parent_link_quality = parent_info.mLinkQualityIn;
      snapshot.parent_average_rssi = parent_average_rssi;
      snapshot.parent_valid = true;
    }
  }

  if (const otMleCounters *counters = otThreadGetMleCounters(instance); counters != nullptr) {
    snapshot.parent_changes = counters->mParentChanges;
    snapshot.attach_attempts = counters->mAttachAttempts;
  }
  if (const otIpCounters *counters = otThreadGetIp6Counters(instance); counters != nullptr) {
    snapshot.ipv6_tx_failures = counters->mTxFailure;
    snapshot.ipv6_rx_failures = counters->mRxFailure;
  }
  if (const otMacCounters *counters = otLinkGetCounters(instance); counters != nullptr) {
    snapshot.mac_tx_retries = counters->mTxRetry;
    snapshot.mac_cca_failures = counters->mTxErrCca;
    snapshot.mac_rx_fcs_errors = counters->mRxErrFcs;
  }

#if defined(CONFIG_OPENTHREAD_SRP_CLIENT) && CONFIG_OPENTHREAD_SRP_CLIENT
  snapshot.srp_client_running = otSrpClientIsRunning(instance);
  if (const otSrpClientHostInfo *host_info = otSrpClientGetHostInfo(instance); host_info != nullptr) {
    snapshot.srp_host_state = otSrpClientItemStateToString(host_info->mState);
  }
#endif

  esp_openthread_lock_release();

  if (this->attached_binary_sensor_ != nullptr) {
    this->attached_binary_sensor_->publish_state(attached);
  }
  if (this->role_text_sensor_ != nullptr) {
    this->role_text_sensor_->publish_state(role_to_string(snapshot.role));
  }
  if (this->rloc16_text_sensor_ != nullptr) {
    this->rloc16_text_sensor_->publish_state(attached ? format_hex16(snapshot.rloc16) : "unavailable");
  }
  if (this->parent_rloc16_text_sensor_ != nullptr) {
    this->parent_rloc16_text_sensor_->publish_state(snapshot.parent_valid ? format_hex16(snapshot.parent_rloc16)
                                                                           : "unavailable");
  }
  if (this->partition_id_text_sensor_ != nullptr) {
    this->partition_id_text_sensor_->publish_state(snapshot.partition_valid ? format_hex32(snapshot.partition_id)
                                                                            : "unavailable");
  }
  if (this->srp_client_running_binary_sensor_ != nullptr) {
    this->srp_client_running_binary_sensor_->publish_state(snapshot.srp_client_running);
  }
  if (this->srp_host_state_text_sensor_ != nullptr) {
    this->srp_host_state_text_sensor_->publish_state(snapshot.srp_host_state);
  }

  const float unavailable = std::numeric_limits<float>::quiet_NaN();
  if (this->attach_duration_sensor_ != nullptr) {
    this->attach_duration_sensor_->publish_state(snapshot.attach_duration);
  }
  if (this->parent_average_rssi_sensor_ != nullptr) {
    this->parent_average_rssi_sensor_->publish_state(snapshot.parent_valid ? snapshot.parent_average_rssi : unavailable);
  }
  if (this->parent_link_quality_sensor_ != nullptr) {
    this->parent_link_quality_sensor_->publish_state(snapshot.parent_valid ? snapshot.parent_link_quality : unavailable);
  }
  if (this->channel_sensor_ != nullptr) {
    this->channel_sensor_->publish_state(snapshot.channel);
  }
  if (this->parent_changes_sensor_ != nullptr) {
    this->parent_changes_sensor_->publish_state(snapshot.parent_changes);
  }
  if (this->attach_attempts_sensor_ != nullptr) {
    this->attach_attempts_sensor_->publish_state(snapshot.attach_attempts);
  }
  if (this->ipv6_tx_failures_sensor_ != nullptr) {
    this->ipv6_tx_failures_sensor_->publish_state(snapshot.ipv6_tx_failures);
  }
  if (this->ipv6_rx_failures_sensor_ != nullptr) {
    this->ipv6_rx_failures_sensor_->publish_state(snapshot.ipv6_rx_failures);
  }
  if (this->mac_tx_retries_sensor_ != nullptr) {
    this->mac_tx_retries_sensor_->publish_state(snapshot.mac_tx_retries);
  }
  if (this->mac_cca_failures_sensor_ != nullptr) {
    this->mac_cca_failures_sensor_->publish_state(snapshot.mac_cca_failures);
  }
  if (this->mac_rx_fcs_errors_sensor_ != nullptr) {
    this->mac_rx_fcs_errors_sensor_->publish_state(snapshot.mac_rx_fcs_errors);
  }

  this->status_clear_warning();
}

void ThreadDiagnostics::dump_config() {
  ESP_LOGCONFIG(TAG, "Thread diagnostics:");
  ESP_LOGCONFIG(TAG, "  Update interval: %" PRIu32 " ms", this->get_update_interval());
}

}  // namespace esphome::thread_diagnostics
