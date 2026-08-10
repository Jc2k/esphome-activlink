#include "components/honeywell_activlink/pairing.h"

#include <array>
#include <cassert>

using esphome::honeywell_activlink::EMPTY_ACTIVLINK_ID;
using esphome::honeywell_activlink::MAX_PAIRED_BUTTONS;
using esphome::honeywell_activlink::PairingTable;
using esphome::honeywell_activlink::PairResult;

void test_add_find_and_remove() {
  PairingTable table;
  size_t slot = MAX_PAIRED_BUTTONS;

  assert(table.size() == 0);
  assert(table.add(0xFB100, &slot) == PairResult::ADDED);
  assert(slot == 0);
  assert(table.find(0xFB100) == 0);
  assert(table.size() == 1);
  assert(table.add(0xFB100, &slot) == PairResult::ALREADY_PAIRED);
  assert(slot == 0);
  assert(table.remove(0xFB100, &slot));
  assert(slot == 0);
  assert(table.find(0xFB100) == -1);
  assert(!table.remove(0xFB100, nullptr));
}

void test_reuses_first_empty_slot() {
  PairingTable table;
  size_t slot = 0;
  assert(table.add(1, nullptr) == PairResult::ADDED);
  assert(table.update_battery(0, true));
  assert(table.add(2, nullptr) == PairResult::ADDED);
  assert(table.remove(1, nullptr));
  assert(!table.battery_known(0));
  assert(table.add(3, &slot) == PairResult::ADDED);
  assert(slot == 0);
  assert(table.at(0) == 3);
  assert(!table.battery_known(0));
}

void test_full_and_invalid() {
  PairingTable table;
  for (uint32_t id = 0; id < MAX_PAIRED_BUTTONS; id++) {
    assert(table.add(id, nullptr) == PairResult::ADDED);
  }
  assert(table.full());
  assert(table.add(0x12345, nullptr) == PairResult::FULL);
  assert(table.add(0x100000, nullptr) == PairResult::INVALID);
}

void test_restore_validates_persisted_data() {
  PairingTable table;
  std::array<uint32_t, MAX_PAIRED_BUTTONS> stored{};
  stored.fill(EMPTY_ACTIVLINK_ID);
  stored[0] = 0x12345;
  stored[4] = 0xABCDE;
  assert(table.restore(stored));
  assert(table.size() == 2);
  assert(table.at(4) == 0xABCDE);

  stored[5] = 0x12345;
  assert(!table.restore(stored));
  assert(table.size() == 2);

  stored[5] = 0x100000;
  assert(!table.restore(stored));
  assert(table.size() == 2);
}

void test_battery_state_survives_restore() {
  PairingTable table;
  assert(table.add(0x12345, nullptr) == PairResult::ADDED);
  assert(table.add(0xABCDE, nullptr) == PairResult::ADDED);

  assert(!table.battery_known(0));
  assert(table.update_battery(0, false));
  assert(table.battery_known(0));
  assert(!table.battery_low(0));
  assert(!table.update_battery(0, false));
  assert(table.update_battery(0, true));
  assert(table.battery_low(0));
  assert(table.update_battery(1, false));

  PairingTable restored;
  assert(restored.restore(table.device_ids(), table.battery_known_mask(), table.battery_low_mask()));
  assert(restored.battery_known(0));
  assert(restored.battery_low(0));
  assert(restored.battery_known(1));
  assert(!restored.battery_low(1));

  // Version 1 records restore IDs without inventing a battery state.
  PairingTable legacy;
  assert(legacy.restore(table.device_ids()));
  assert(!legacy.battery_known(0));
  assert(!legacy.battery_known(1));
}

void test_restore_rejects_invalid_battery_masks() {
  PairingTable table;
  std::array<uint32_t, MAX_PAIRED_BUTTONS> stored{};
  stored.fill(EMPTY_ACTIVLINK_ID);
  stored[0] = 0x12345;

  assert(!table.restore(stored, 0x00, 0x01));
  assert(!table.restore(stored, 0x02, 0x00));
  assert(table.size() == 0);
}

int main() {
  test_add_find_and_remove();
  test_reuses_first_empty_slot();
  test_full_and_invalid();
  test_restore_validates_persisted_data();
  test_battery_state_survives_restore();
  test_restore_rejects_invalid_battery_masks();
}
