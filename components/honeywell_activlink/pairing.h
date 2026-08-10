#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::honeywell_activlink {

constexpr size_t MAX_PAIRED_BUTTONS = 8;
constexpr uint32_t EMPTY_ACTIVLINK_ID = 0xFFFFFFFF;

enum class PairResult : uint8_t {
  ADDED,
  ALREADY_PAIRED,
  FULL,
  INVALID,
};

class PairingTable {
 public:
  PairingTable();

  PairResult add(uint32_t device_id, size_t *slot);
  bool remove(uint32_t device_id, size_t *slot);
  int find(uint32_t device_id) const;
  bool restore(const std::array<uint32_t, MAX_PAIRED_BUTTONS> &device_ids, uint8_t battery_known_mask = 0,
               uint8_t battery_low_mask = 0);
  void clear();
  bool update_battery(size_t slot, bool battery_low);

  size_t size() const;
  bool full() const { return this->size() == MAX_PAIRED_BUTTONS; }
  uint32_t at(size_t slot) const { return this->device_ids_[slot]; }
  bool battery_known(size_t slot) const;
  bool battery_low(size_t slot) const;
  uint8_t battery_known_mask() const { return this->battery_known_mask_; }
  uint8_t battery_low_mask() const { return this->battery_low_mask_; }
  const std::array<uint32_t, MAX_PAIRED_BUTTONS> &device_ids() const { return this->device_ids_; }

 protected:
  std::array<uint32_t, MAX_PAIRED_BUTTONS> device_ids_{};
  uint8_t battery_known_mask_{0};
  uint8_t battery_low_mask_{0};
};

}  // namespace esphome::honeywell_activlink
