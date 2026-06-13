#pragma once
#include <functional>
#include <inputtino/input.hpp>
#include <optional>
#include <uhid/ps4.hpp>
#include <uhid/uhid.hpp>

namespace inputtino {
struct PS4JoypadState {
  std::shared_ptr<uhid::Device> dev;

  /**
   * MAC address of the device; must be unique per virtual device or the kernel
   * driver refuses it ("Duplicate device found for MAC address ..."). Also used
   * to match the device against its /dev/input/* nodes; see get_nodes().
   */
  unsigned char mac_address[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint16_t vendor_id;

  uhid::dualshock4_input_report current_state = {};
  uint8_t last_touch_id = 0;

  std::optional<std::function<void(int, int)>> on_rumble = std::nullopt;
  std::optional<std::function<void(int, int, int)>> on_led = std::nullopt;

  bool stop_repeat_thread = false;
};
} // namespace inputtino
