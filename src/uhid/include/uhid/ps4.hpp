#pragma once

#include <cstdint>
#include <linux/uhid.h>
#include <uhid/ps5.hpp>
#include <vector>

/*
 * DualShock 4 (PS4) uhid emulation.
 *
 * Unlike the DualSense, the DS4 has no haptics: games drive its two rumble
 * motors (the legacy strong/weak ERM motors) directly and at full strength,
 * which is exactly what we want to forward to a streaming client. Modern Linux
 * kernels bind the DS4 in `hid-playstation` purely by USB VID/PID, so we expose
 * a minimal-but-valid USB HID device and let the kernel driver take over input
 * mapping; we only need to honour the feature reports it queries on probe
 * (calibration / MAC / firmware) and parse the output report it sends for
 * rumble and the lightbar.
 *
 * Report struct layouts mirror the kernel's `hid-playstation` driver
 * (drivers/hid/hid-playstation.c). We implement the USB variant only, which has
 * no CRC and a fixed-size touch section.
 *
 * Many button / hat / touch-point definitions and the PS_*_CRC32 helpers are
 * shared with the DualSense and reused from ps5.hpp.
 */
namespace uhid {

/* DualShock 4 USB report ids. */
static constexpr uint8_t DS4_INPUT_REPORT_USB = 0x01;
static constexpr uint8_t DS4_OUTPUT_REPORT_USB = 0x05;

/* Feature reports the kernel requests on probe (see hid-playstation.c). */
enum PS4_REPORT_TYPES : unsigned int {
  DS4_FEATURE_PAIRING_INFO = 0x12,    // 16 bytes, contains the MAC address
  DS4_FEATURE_CALIBRATION = 0x02,     // 37 bytes, motion calibration
  DS4_FEATURE_FIRMWARE_INFO = 0xA3,   // 49 bytes, fw/hw version (cosmetic)
};

/*
 * DualShock 4 hardware limits, from hid-playstation.c.
 * Accelerometer is reported in units of 1/DS4_ACC_RES_PER_G of a g, gyro in
 * units of 1/DS4_GYRO_RES_PER_DEG_S of a deg/s.
 */
static constexpr int DS4_ACC_RES_PER_G = 8192;
static constexpr int DS4_GYRO_RES_PER_DEG_S = 1024;
static constexpr int DS4_TOUCHPAD_WIDTH = 1920;
static constexpr int DS4_TOUCHPAD_HEIGHT = 942;

/*
 * Minimal vendor-defined HID report descriptor.
 *
 * `hid-playstation` matches the DS4 by USB VID/PID and parses input reports
 * with a hardcoded parser, building its own input device rather than using the
 * descriptor's usages. So the descriptor only needs to declare the report ids
 * with the correct sizes so the HID core can route raw reports:
 *   - input  0x01: 63 data bytes (+ 1 report id = 64)
 *   - output 0x05: 31 data bytes (+ 1 report id = 32)
 *   - feature 0x02 (36), 0xA3 (48), 0x12 (15), 0x81 (6)
 */
static constexpr unsigned char ds4_rdesc[] = {
    0x06, 0x00, 0xFF, // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,       // Usage (0x01)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x09, 0x01,       //   Usage (0x01)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x85, 0x05,       //   Report ID (5)
    0x09, 0x02,       //   Usage (0x02)
    0x95, 0x1F,       //   Report Count (31)
    0x91, 0x02,       //   Output (Data,Var,Abs)
    0x85, 0x02,       //   Report ID (2)
    0x09, 0x03,       //   Usage (0x03)
    0x95, 0x24,       //   Report Count (36)
    0xB1, 0x02,       //   Feature (Data,Var,Abs)
    0x85, 0xA3,       //   Report ID (163)
    0x09, 0x04,       //   Usage (0x04)
    0x95, 0x30,       //   Report Count (48)
    0xB1, 0x02,       //   Feature (Data,Var,Abs)
    0x85, 0x12,       //   Report ID (18)
    0x09, 0x05,       //   Usage (0x05)
    0x95, 0x0F,       //   Report Count (15)
    0xB1, 0x02,       //   Feature (Data,Var,Abs)
    0x85, 0x81,       //   Report ID (129)
    0x09, 0x06,       //   Usage (0x06)
    0x95, 0x06,       //   Report Count (6)
    0xB1, 0x02,       //   Feature (Data,Var,Abs)
    0xC0,             // End Collection
};

/*
 * Feature report payloads (each begins with its report id, mirroring the way
 * the DualSense blobs in ps5.hpp are stored).
 *
 * NOTE: the calibration values below are synthesised to be non-degenerate
 * (non-zero gyro speed and accelerometer ranges) so the kernel's probe-time
 * calibration parsing succeeds. They are NOT captured from real hardware, so
 * gyro/accel readings will be approximately—but not precisely—scaled. This does
 * not affect rumble, buttons, sticks or triggers. Replace with a real
 * `hid-recorder` capture for accurate motion.
 */
static constexpr unsigned char ds4_calibration_info[] = {
    0x02,                                // report id
    0x00, 0x00,                          // gyro_pitch_bias
    0x00, 0x00,                          // gyro_yaw_bias
    0x00, 0x00,                          // gyro_roll_bias
    0x56, 0x03,                          // gyro_pitch_plus
    0x56, 0x03,                          // gyro_pitch_minus
    0x56, 0x03,                          // gyro_yaw_plus
    0x56, 0x03,                          // gyro_yaw_minus
    0x56, 0x03,                          // gyro_roll_plus
    0x56, 0x03,                          // gyro_roll_minus
    0x30, 0x0F,                          // gyro_speed_plus
    0x30, 0x0F,                          // gyro_speed_minus
    0x55, 0x20,                          // acc_x_plus
    0x9D, 0xDF,                          // acc_x_minus
    0x55, 0x20,                          // acc_y_plus
    0x9D, 0xDF,                          // acc_y_minus
    0x55, 0x20,                          // acc_z_plus
    0x9D, 0xDF,                          // acc_z_minus
    0x00, 0x00,                          // padding (to 37 bytes)
};

static constexpr unsigned char ds4_firmware_info[] = {
    0xA3, 0x4A, 0x75, 0x6C, 0x20, 0x20, 0x39, 0x20, 0x32, 0x30, 0x31, 0x33,
    0x31, 0x36, 0x3A, 0x32, 0x37, 0x3A, 0x33, 0x34, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
};

static constexpr unsigned char ds4_pairing_info[] = {
    0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x25, 0x00, 0x1E, 0x00, 0xEE, 0x74, 0xD0, 0xBC,
};

#pragma pack(push, 1)

/*
 * Common input report body (32 bytes), shared by USB/BT in the kernel. Reused
 * button masks (SQUARE/CROSS/..., L1/R1/..., PS_HOME/TOUCHPAD) and HAT_* values
 * come from ps5.hpp.
 */
struct dualshock4_touch_report {
  uint8_t timestamp = 0;
  dualsense_touch_point points[2] = {};
};

struct dualshock4_input_report {
  uint8_t x = PS5_AXIS_NEUTRAL, y = PS5_AXIS_NEUTRAL;   // LS
  uint8_t rx = PS5_AXIS_NEUTRAL, ry = PS5_AXIS_NEUTRAL; // RS
  // buttons[0]: hat (low nibble) + face buttons; [1]: shoulders/L2R2/share/options/L3R3; [2]: PS/touchpad + counter
  uint8_t buttons[3] = {HAT_NEUTRAL, 0, 0};
  uint8_t z = 0, rz = 0; // L2, R2 analog
  __le16 sensor_timestamp = 0;
  uint8_t sensor_temperature = 0;
  __le16 gyro[3] = {0, 0, 0};  // pitch, yaw, roll
  __le16 accel[3] = {0, 0, 0}; // x, y, z
  uint8_t reserved2[5] = {};
  // status[0]: bits 0-3 battery level, bit 4 cable state; default: cabled + full
  uint8_t status[2] = {0x1B, 0x00};
  uint8_t reserved3 = 0;
  // --- end of 32-byte common section ---
  uint8_t num_touch_reports = 0;
  dualshock4_touch_report touch_reports[3] = {};
  uint8_t reserved[3] = {};
};

/* Common output report (rumble + lightbar). */
struct dualshock4_output_report_common {
  uint8_t valid_flag0; // bit0: update rumble, bit1: update lightbar
  uint8_t valid_flag1;
  uint8_t reserved;
  uint8_t motor_right; // weak / high-frequency motor
  uint8_t motor_left;  // strong / low-frequency motor
  uint8_t lightbar_red;
  uint8_t lightbar_green;
  uint8_t lightbar_blue;
  uint8_t lightbar_blink_on;
  uint8_t lightbar_blink_off;
};

enum DS4_OUTPUT_VALID_FLAG0 : uint8_t {
  DS4_FLAG0_MOTOR = 0x01,
  DS4_FLAG0_LED = 0x02,
};

struct dualshock4_output_report_usb {
  uint8_t report_id; /* 0x05 */
  struct dualshock4_output_report_common common;
  uint8_t reserved[21];
};

#pragma pack(pop)

} // namespace uhid
