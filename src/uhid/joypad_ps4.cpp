#include <algorithm>
#include <climits>
#include <cmath>
#include <endian.h>
#include <filesystem>
#include <fstream>
#include <inputtino/input.hpp>
#include <iomanip>
#include <random>
#include <sstream>
#include <uhid/ps4.hpp>
#include <uhid/protected_ps4_types.hpp>
#include <uhid/uhid.hpp>

namespace inputtino {

static void send_report(PS4JoypadState &state) {
  // The DS4 sensor timestamp is a free-running 16-bit counter; bump it so
  // readers that diff timestamps keep seeing fresh reports.
  state.current_state.sensor_timestamp = htole16(le16toh(state.current_state.sensor_timestamp) + 188);

  struct uhid_event ev{};
  ev.type = UHID_INPUT2;
  ev.u.input2.data[0] = uhid::DS4_INPUT_REPORT_USB;
  unsigned char *data = (unsigned char *)&state.current_state;
  std::copy(data, data + sizeof(state.current_state), &ev.u.input2.data[1]);
  ev.u.input2.size = 1 + sizeof(state.current_state);

  if (state.dev) {
    state.dev->send(ev);
  }
}

static void on_uhid_event(std::shared_ptr<PS4JoypadState> state, uhid_event ev, int fd) {
  switch (ev.type) {
  case UHID_GET_REPORT: {
    uhid_event answer{};
    answer.type = UHID_GET_REPORT_REPLY;
    answer.u.get_report_reply.id = ev.u.get_report.id;
    answer.u.get_report_reply.err = 0;
    switch (ev.u.get_report.rnum) {
    case uhid::DS4_FEATURE_CALIBRATION: {
      std::copy(&uhid::ds4_calibration_info[0],
                &uhid::ds4_calibration_info[0] + sizeof(uhid::ds4_calibration_info),
                &answer.u.get_report_reply.data[0]);
      answer.u.get_report_reply.size = sizeof(uhid::ds4_calibration_info);
      break;
    }
    case uhid::DS4_FEATURE_PAIRING_INFO: {
      std::copy(&uhid::ds4_pairing_info[0],
                &uhid::ds4_pairing_info[0] + sizeof(uhid::ds4_pairing_info),
                &answer.u.get_report_reply.data[0]);
      // The kernel reads the controller MAC from bytes 1..6 of this report.
      std::copy(&state->mac_address[0],
                &state->mac_address[0] + sizeof(state->mac_address),
                &answer.u.get_report_reply.data[1]);
      answer.u.get_report_reply.size = sizeof(uhid::ds4_pairing_info);
      break;
    }
    case uhid::DS4_FEATURE_FIRMWARE_INFO: {
      std::copy(&uhid::ds4_firmware_info[0],
                &uhid::ds4_firmware_info[0] + sizeof(uhid::ds4_firmware_info),
                &answer.u.get_report_reply.data[0]);
      answer.u.get_report_reply.size = sizeof(uhid::ds4_firmware_info);
      break;
    }
    default:
      answer.u.get_report_reply.err = -EINVAL;
      break;
    }

    uhid::uhid_write(fd, &answer);
    break;
  }
  case UHID_OUTPUT: {
    // The kernel sends us the DS4 output report carrying rumble + lightbar.
    uint8_t report_type = ev.u.output.data[0];
    if (report_type != uhid::DS4_OUTPUT_REPORT_USB) {
      break;
    }
    auto report = ((uhid::dualshock4_output_report_usb *)ev.u.output.data)->common;

    /*
     * RUMBLE
     * DS4 motors are reported in the range 0-255; scale to 0-0xFFFF to match
     * the wider range expected by callers (mirrors the DualSense path).
     */
    if (report.valid_flag0 & uhid::DS4_FLAG0_MOTOR) {
      auto left = static_cast<int>((report.motor_left / 255.0f) * 0xFFFF);
      auto right = static_cast<int>((report.motor_right / 255.0f) * 0xFFFF);
      if (state->on_rumble) {
        (*state->on_rumble)(left, right);
      }
    }

    /*
     * LED
     */
    if ((report.valid_flag0 & uhid::DS4_FLAG0_LED) && state->on_led) {
      (*state->on_led)(report.lightbar_red, report.lightbar_green, report.lightbar_blue);
    }
    break;
  }
  default:
    break;
  }
}

PS4Joypad::PS4Joypad(uint16_t vendor_id, std::array<unsigned char, 6> mac_address)
    : _state(std::make_shared<PS4JoypadState>()) {
  std::copy(mac_address.begin(), mac_address.end(), this->_state->mac_address);
  this->_state->vendor_id = vendor_id;
  // Touchpad fingers start released (contact bit set == inactive).
  for (auto &touch : this->_state->current_state.touch_reports) {
    touch.points[0].contact = 1;
    touch.points[1].contact = 1;
  }
  this->_state->current_state.num_touch_reports = 1;
}

PS4Joypad::~PS4Joypad() {
  if (this->_state && this->_state->dev) {
    this->_state->stop_repeat_thread = true;
    if (this->_send_input_thread.joinable()) {
      this->_send_input_thread.join();
    }
    this->_state->dev->stop_thread();
    this->_state->dev.reset();
  }
}

Result<PS4Joypad> PS4Joypad::create(const DeviceDefinition &device) {
  auto def = uhid::DeviceDefinition{
      .name = device.name,
      .phys = device.device_phys,
      .uniq = device.device_uniq,
      .bus = BUS_USB,
      .vendor = static_cast<uint32_t>(device.vendor_id),
      .product = static_cast<uint32_t>(device.product_id),
      .version = static_cast<uint32_t>(device.version),
      .country = 0,
      .report_description = {&uhid::ds4_rdesc[0], &uhid::ds4_rdesc[0] + sizeof(uhid::ds4_rdesc)}};

  std::array<unsigned char, 6> mac_address = {};
  if (def.uniq.empty()) {
    mac_address = generate_mac_address();
  } else {
    // Parse a MAC address in the format xx:xx:xx:xx:xx:xx.
    std::stringstream ss(def.uniq);
    for (int i = 0; i < 6; ++i) {
      unsigned int value;
      ss >> std::hex >> value;
      mac_address[i] = static_cast<unsigned char>(value);
      if (i < 5)
        ss.ignore(1, ':');
    }
  }
  auto joypad = PS4Joypad(device.vendor_id, mac_address);

  if (def.phys.empty()) {
    def.phys = "INPUTTINO_USB_LINK";
  }
  if (def.uniq.empty()) {
    def.uniq = joypad.get_mac_address();
  }

  auto dev =
      uhid::Device::create(def, [state = joypad._state](uhid_event ev, int fd) { on_uhid_event(state, ev, fd); });
  if (dev) {
    joypad._state->dev = std::make_shared<uhid::Device>(std::move(*dev));

    // Readers expect frequent reports even when the state hasn't changed.
    joypad._send_input_thread = std::thread([state = joypad._state]() {
      while (!state->stop_repeat_thread) {
        send_report(*state);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
    joypad._send_input_thread.detach();

    return joypad;
  }
  return Error(dev.getErrorMessage());
}

static int scale_value(int input, int input_start, int input_end, int output_start, int output_end) {
  auto slope = 1.0 * (output_end - output_start) / (input_end - input_start);
  return output_start + std::round(slope * (input - input_start));
}

template <typename T> static std::string to_hex(T i) {
  std::stringstream stream;
  stream << std::hex << std::uppercase << i;
  return stream.str();
}

std::string PS4Joypad::get_mac_address() const {
  std::stringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(2) << (unsigned int)_state->mac_address[0] << ":" << std::setw(2)
         << (unsigned int)_state->mac_address[1] << ":" << std::setw(2) << (unsigned int)_state->mac_address[2] << ":"
         << std::setw(2) << (unsigned int)_state->mac_address[3] << ":" << std::setw(2)
         << (unsigned int)_state->mac_address[4] << ":" << std::setw(2) << (unsigned int)_state->mac_address[5];
  return stream.str();
}

std::vector<std::string> PS4Joypad::get_sys_nodes() const {
  std::vector<std::string> nodes;
  auto base_path = "/sys/devices/virtual/misc/uhid/";
  auto target_mac = get_mac_address();
  if (std::filesystem::exists(base_path)) {
    auto uhid_entries = std::filesystem::directory_iterator{base_path};
    for (auto uhid_entry : uhid_entries) {
      auto uhid_candidate_path = uhid_entry.path().filename().string();
      auto target_id = to_hex(this->_state->vendor_id);
      if (uhid_entry.is_directory() && uhid_candidate_path.find(target_id) != std::string::npos) {
        if (std::filesystem::exists(uhid_entry.path() / "input")) {
          auto dev_entries = std::filesystem::directory_iterator{uhid_entry.path() / "input"};
          for (auto dev_entry : dev_entries) {
            if (dev_entry.is_directory()) {
              auto dev_uniq_path = dev_entry.path() / "uniq";
              if (std::filesystem::exists(dev_uniq_path)) {
                std::ifstream dev_uniq_file{dev_uniq_path};
                std::string line;
                std::getline(dev_uniq_file, line);
                if (line == target_mac) {
                  nodes.push_back(dev_entry.path().string());
                }
              }
            }
          }
        }
      }
    }
  }
  return nodes;
}

std::vector<std::string> PS4Joypad::get_nodes() const {
  std::vector<std::string> nodes;

  auto sys_nodes = get_sys_nodes();
  for (const auto dev_entry : sys_nodes) {
    auto dev_nodes = std::filesystem::directory_iterator{dev_entry};
    for (auto dev_node : dev_nodes) {
      if (dev_node.is_directory() && (dev_node.path().filename().string().rfind("event", 0) == 0 ||
                                      dev_node.path().filename().string().rfind("js", 0) == 0)) {
        nodes.push_back(("/dev/input/" / dev_node.path().filename()).string());
      }
    }
  }

  return nodes;
}

void PS4Joypad::set_pressed_buttons(unsigned int pressed) {
  { // Reset everything except L2/R2 (driven by set_triggers).
    this->_state->current_state.buttons[0] = 0;
    this->_state->current_state.buttons[1] &= (uhid::L2 | uhid::R2);
    this->_state->current_state.buttons[2] = 0;
  }
  {
    if (DPAD_UP & pressed) {
      if (DPAD_LEFT & pressed) {
        this->_state->current_state.buttons[0] |= uhid::HAT_NW;
      } else if (DPAD_RIGHT & pressed) {
        this->_state->current_state.buttons[0] |= uhid::HAT_NE;
      } else {
        this->_state->current_state.buttons[0] |= uhid::HAT_N;
      }
    }

    if (DPAD_DOWN & pressed) {
      if (DPAD_LEFT & pressed) {
        this->_state->current_state.buttons[0] |= uhid::HAT_SW;
      } else if (DPAD_RIGHT & pressed) {
        this->_state->current_state.buttons[0] |= uhid::HAT_SE;
      } else {
        this->_state->current_state.buttons[0] |= uhid::HAT_S;
      }
    }

    if (DPAD_LEFT & pressed) {
      if (!(DPAD_UP & pressed) && !(DPAD_DOWN & pressed)) {
        this->_state->current_state.buttons[0] |= uhid::HAT_W;
      }
    }

    if (DPAD_RIGHT & pressed) {
      if (!(DPAD_UP & pressed) && !(DPAD_DOWN & pressed)) {
        this->_state->current_state.buttons[0] |= uhid::HAT_E;
      }
    }

    if (!(DPAD_UP & pressed) && !(DPAD_DOWN & pressed) && !(DPAD_LEFT & pressed) && !(DPAD_RIGHT & pressed)) {
      this->_state->current_state.buttons[0] |= uhid::HAT_NEUTRAL;
    }

    if (X & pressed)
      this->_state->current_state.buttons[0] |= uhid::SQUARE;
    if (Y & pressed)
      this->_state->current_state.buttons[0] |= uhid::TRIANGLE;
    if (A & pressed)
      this->_state->current_state.buttons[0] |= uhid::CROSS;
    if (B & pressed)
      this->_state->current_state.buttons[0] |= uhid::CIRCLE;
    if (LEFT_BUTTON & pressed)
      this->_state->current_state.buttons[1] |= uhid::L1;
    if (RIGHT_BUTTON & pressed)
      this->_state->current_state.buttons[1] |= uhid::R1;
    if (LEFT_STICK & pressed)
      this->_state->current_state.buttons[1] |= uhid::L3;
    if (RIGHT_STICK & pressed)
      this->_state->current_state.buttons[1] |= uhid::R3;
    if (START & pressed)
      this->_state->current_state.buttons[1] |= uhid::OPTIONS;
    if (BACK & pressed)
      this->_state->current_state.buttons[1] |= uhid::CREATE;
    if (TOUCHPAD_FLAG & pressed)
      this->_state->current_state.buttons[2] |= uhid::TOUCHPAD;
    if (HOME & pressed)
      this->_state->current_state.buttons[2] |= uhid::PS_HOME;
  }
  send_report(*this->_state);
}

void PS4Joypad::set_triggers(int16_t left, int16_t right) {
  this->_state->current_state.z = scale_value(left, 0, 255, uhid::PS5_AXIS_MIN, uhid::PS5_AXIS_MAX);
  this->_state->current_state.rz = scale_value(right, 0, 255, uhid::PS5_AXIS_MIN, uhid::PS5_AXIS_MAX);

  if (left == 0)
    this->_state->current_state.buttons[1] &= ~uhid::L2;
  else
    this->_state->current_state.buttons[1] |= uhid::L2;

  if (right == 0)
    this->_state->current_state.buttons[1] &= ~uhid::R2;
  else
    this->_state->current_state.buttons[1] |= uhid::R2;

  send_report(*this->_state);
}

void PS4Joypad::set_stick(Joypad::STICK_POSITION stick_type, short x, short y) {
  switch (stick_type) {
  case RS: {
    this->_state->current_state.rx = scale_value(x, -32768, 32767, uhid::PS5_AXIS_MIN, uhid::PS5_AXIS_MAX);
    this->_state->current_state.ry = scale_value(-y, -32768, 32767, uhid::PS5_AXIS_MIN, uhid::PS5_AXIS_MAX);
    send_report(*this->_state);
    break;
  }
  case LS: {
    this->_state->current_state.x = scale_value(x, -32768, 32767, uhid::PS5_AXIS_MIN, uhid::PS5_AXIS_MAX);
    this->_state->current_state.y = scale_value(-y, -32768, 32767, uhid::PS5_AXIS_MIN, uhid::PS5_AXIS_MAX);
    send_report(*this->_state);
    break;
  }
  }
}

void PS4Joypad::set_on_rumble(const std::function<void(int, int)> &callback) {
  this->_state->on_rumble = callback;
}

static __le16 to_le_signed(float value) {
  value = std::clamp(value, static_cast<float>(SHRT_MIN), static_cast<float>(SHRT_MAX));
  return htole16(static_cast<int16_t>(value));
}

void PS4Joypad::set_motion(PS4Joypad::MOTION_TYPE type, float x, float y, float z) {
  switch (type) {
  case ACCELERATION: {
    this->_state->current_state.accel[0] = to_le_signed(x / uhid::SDL_STANDARD_GRAVITY_CONST * uhid::DS4_ACC_RES_PER_G);
    this->_state->current_state.accel[1] = to_le_signed(y / uhid::SDL_STANDARD_GRAVITY_CONST * uhid::DS4_ACC_RES_PER_G);
    this->_state->current_state.accel[2] = to_le_signed(z / uhid::SDL_STANDARD_GRAVITY_CONST * uhid::DS4_ACC_RES_PER_G);
    send_report(*this->_state);
    break;
  }
  case GYROSCOPE: {
    this->_state->current_state.gyro[0] = to_le_signed(x * uhid::DS4_GYRO_RES_PER_DEG_S);
    this->_state->current_state.gyro[1] = to_le_signed(y * uhid::DS4_GYRO_RES_PER_DEG_S);
    this->_state->current_state.gyro[2] = to_le_signed(z * uhid::DS4_GYRO_RES_PER_DEG_S);
    send_report(*this->_state);
    break;
  }
  }
}

void PS4Joypad::set_battery(PS4Joypad::BATTERY_STATE state, int percentage) {
  // status[0]: bits 0-3 battery level (0-10), bit 4 cable state (1 == cabled).
  uint8_t level = std::clamp(static_cast<int>(std::lround(percentage / 10.0)), 0, 0x0F);
  bool cabled = state != BATTERY_DISCHARGING;
  this->_state->current_state.status[0] = level | (cabled ? 0x10 : 0x00);
  send_report(*this->_state);
}

void PS4Joypad::set_on_led(const std::function<void(int, int, int)> &callback) {
  this->_state->on_led = callback;
}

void PS4Joypad::place_finger(int finger_nr, uint16_t x, uint16_t y) {
  if (finger_nr <= 1) {
    auto &point = this->_state->current_state.touch_reports[0].points[finger_nr];
    if (point.contact == 1) {
      point.id = ++this->_state->last_touch_id;
    }
    point.contact = 0;
    point.x_lo = static_cast<uint8_t>(x & 0x00FF);
    point.x_hi = static_cast<uint8_t>((x & 0x0F00) >> 8);
    point.y_lo = static_cast<uint8_t>(y & 0x000F);
    point.y_hi = static_cast<uint8_t>((y & 0x0FF0) >> 4);
    send_report(*this->_state);
  }
}

void PS4Joypad::release_finger(int finger_nr) {
  if (finger_nr <= 1) {
    if (this->_state->last_touch_id >= 0x7E) {
      this->_state->last_touch_id = 0;
    }
    this->_state->current_state.touch_reports[0].points[finger_nr].contact = 1;
    send_report(*this->_state);
  }
}

} // namespace inputtino
