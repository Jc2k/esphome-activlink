#include "thread_diagnostics.h"

#include "esphome/core/log.h"

#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <freertos/FreeRTOS.h>
#include <openthread/link.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace esphome::thread_diagnostics {

static const char *const TAG = "thread_diagnostics";

namespace {

constexpr size_t MAX_SUMMARY_LENGTH = 240;

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
  uint8_t router_neighbor_count{0};
  int8_t router_neighbor_best_rssi{OT_RADIO_RSSI_INVALID};
  int8_t router_neighbor_worst_rssi{OT_RADIO_RSSI_INVALID};
  uint8_t router_neighbor_min_link_quality{0};
  uint8_t known_router_count{0};
  uint8_t reachable_router_count{0};
  bool parent_valid{false};
  bool partition_valid{false};
  bool router_neighbors_valid{false};
  bool router_neighbor_rssi_valid{false};
  bool router_table_valid{false};
  bool srp_client_running{false};
  std::string srp_host_state{"disabled"};
  std::string router_neighbors{"unavailable"};
  std::string router_table{"unavailable"};
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

void append_summary(std::string &summary, const char *entry) {
  const size_t separator_length = summary.empty() ? 0 : 2;
  const size_t entry_length = std::strlen(entry);
  if (summary.size() + separator_length + entry_length <= MAX_SUMMARY_LENGTH) {
    if (!summary.empty()) {
      summary.append("; ");
    }
    summary.append(entry);
  } else if (summary.size() + 3 <= MAX_SUMMARY_LENGTH &&
             (summary.size() < 3 || summary.compare(summary.size() - 3, 3, "...") != 0)) {
    summary.append("...");
  }
}

void collect_router_neighbors(otInstance *instance, ThreadSnapshot &snapshot) {
  snapshot.router_neighbors_valid = true;
  snapshot.router_neighbors.clear();

  otNeighborInfoIterator iterator = OT_NEIGHBOR_INFO_ITERATOR_INIT;
  otNeighborInfo neighbor{};
  while (otThreadGetNextNeighborInfo(instance, &iterator, &neighbor) == OT_ERROR_NONE) {
    if (neighbor.mIsChild) {
      continue;
    }

    snapshot.router_neighbor_count++;
    if (snapshot.router_neighbor_count == 1 ||
        neighbor.mLinkQualityIn < snapshot.router_neighbor_min_link_quality) {
      snapshot.router_neighbor_min_link_quality = neighbor.mLinkQualityIn;
    }
    if (neighbor.mAverageRssi != OT_RADIO_RSSI_INVALID) {
      if (!snapshot.router_neighbor_rssi_valid || neighbor.mAverageRssi > snapshot.router_neighbor_best_rssi) {
        snapshot.router_neighbor_best_rssi = neighbor.mAverageRssi;
      }
      if (!snapshot.router_neighbor_rssi_valid || neighbor.mAverageRssi < snapshot.router_neighbor_worst_rssi) {
        snapshot.router_neighbor_worst_rssi = neighbor.mAverageRssi;
      }
      snapshot.router_neighbor_rssi_valid = true;
    }

    char entry[56];
    if (neighbor.mAverageRssi == OT_RADIO_RSSI_INVALID) {
      std::snprintf(entry, sizeof(entry), "0x%04" PRIX16 " RSSI? LQ%u age=%" PRIu32 "s", neighbor.mRloc16,
                    neighbor.mLinkQualityIn, neighbor.mAge);
    } else {
      std::snprintf(entry, sizeof(entry), "0x%04" PRIX16 " %ddBm LQ%u age=%" PRIu32 "s", neighbor.mRloc16,
                    neighbor.mAverageRssi, neighbor.mLinkQualityIn, neighbor.mAge);
    }
    append_summary(snapshot.router_neighbors, entry);
  }

  if (snapshot.router_neighbors.empty()) {
    snapshot.router_neighbors = "none";
  }
}

void collect_router_table(otInstance *instance, ThreadSnapshot &snapshot) {
  if (snapshot.role != OT_DEVICE_ROLE_ROUTER && snapshot.role != OT_DEVICE_ROLE_LEADER) {
    return;
  }

  snapshot.router_table_valid = true;
  snapshot.router_table.clear();
  const uint8_t max_router_id = otThreadGetMaxRouterId(instance);
  for (uint16_t router_id = 0; router_id <= max_router_id; router_id++) {
    otRouterInfo router{};
    if (otThreadGetRouterInfo(instance, router_id, &router) != OT_ERROR_NONE || !router.mAllocated ||
        router.mRloc16 == snapshot.rloc16) {
      continue;
    }

    snapshot.known_router_count++;
    const bool reachable = router.mLinkEstablished || router.mNextHop <= max_router_id;
    if (reachable) {
      snapshot.reachable_router_count++;
    }

    char entry[64];
    if (router.mLinkEstablished) {
      std::snprintf(entry, sizeof(entry), "0x%04" PRIX16 " direct c%u LQ%u/%u", router.mRloc16,
                    router.mPathCost, router.mLinkQualityIn, router.mLinkQualityOut);
    } else if (router.mNextHop <= max_router_id) {
      const uint16_t next_hop_rloc16 = static_cast<uint16_t>(router.mNextHop) << 10;
      std::snprintf(entry, sizeof(entry), "0x%04" PRIX16 " via 0x%04" PRIX16 " c%u", router.mRloc16,
                    next_hop_rloc16, router.mPathCost);
    } else {
      std::snprintf(entry, sizeof(entry), "0x%04" PRIX16 " unreachable", router.mRloc16);
    }
    append_summary(snapshot.router_table, entry);
  }

  if (snapshot.router_table.empty()) {
    snapshot.router_table = "none";
  }
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

  if (attached) {
    collect_router_neighbors(instance, snapshot);
    collect_router_table(instance, snapshot);
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
  if (this->router_neighbors_text_sensor_ != nullptr) {
    this->router_neighbors_text_sensor_->publish_state(snapshot.router_neighbors_valid ? snapshot.router_neighbors
                                                                                       : "unavailable");
  }
  if (this->router_table_text_sensor_ != nullptr) {
    this->router_table_text_sensor_->publish_state(snapshot.router_table_valid ? snapshot.router_table : "unavailable");
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
  if (this->router_neighbor_count_sensor_ != nullptr) {
    this->router_neighbor_count_sensor_->publish_state(snapshot.router_neighbors_valid ? snapshot.router_neighbor_count
                                                                                        : unavailable);
  }
  if (this->router_neighbor_best_rssi_sensor_ != nullptr) {
    this->router_neighbor_best_rssi_sensor_->publish_state(snapshot.router_neighbor_rssi_valid
                                                                ? snapshot.router_neighbor_best_rssi
                                                                : unavailable);
  }
  if (this->router_neighbor_worst_rssi_sensor_ != nullptr) {
    this->router_neighbor_worst_rssi_sensor_->publish_state(snapshot.router_neighbor_rssi_valid
                                                                 ? snapshot.router_neighbor_worst_rssi
                                                                 : unavailable);
  }
  if (this->router_neighbor_min_link_quality_sensor_ != nullptr) {
    this->router_neighbor_min_link_quality_sensor_->publish_state(
        snapshot.router_neighbor_count > 0 ? snapshot.router_neighbor_min_link_quality : unavailable);
  }
  if (this->known_router_count_sensor_ != nullptr) {
    this->known_router_count_sensor_->publish_state(snapshot.router_table_valid ? snapshot.known_router_count
                                                                                : unavailable);
  }
  if (this->reachable_router_count_sensor_ != nullptr) {
    this->reachable_router_count_sensor_->publish_state(snapshot.router_table_valid ? snapshot.reachable_router_count
                                                                                    : unavailable);
  }

  this->status_clear_warning();
}

void ThreadDiagnostics::dump_config() {
  ESP_LOGCONFIG(TAG, "Thread diagnostics:");
  ESP_LOGCONFIG(TAG, "  Update interval: %" PRIu32 " ms", this->get_update_interval());
}

}  // namespace esphome::thread_diagnostics
