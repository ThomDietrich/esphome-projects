#include "waveshare_uc8176_bwr.h"
#include <cinttypes>
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace waveshare_uc8176_bwr {

static const char *const TAG = "waveshare_uc8176_bwr";

// Short "quick" LUTs for the UC8176 4.2" panel (black/white only), copied from
// the built-in mono 4.20in driver. They give a short waveform (~4 phases =
// 2-3 visible flashes) and are used for the fast mono refresh when the frame
// contains no red. The red pigment cannot be driven by these — red needs the
// panel's built-in OTP tri-color waveform (see display()).
static const uint8_t LUT_VCOM_DC[] = {
    0x00, 0x17, 0x00, 0x00, 0x00, 0x02, 0x00, 0x17, 0x17, 0x00, 0x00, 0x02, 0x00, 0x0A, 0x01,
    0x00, 0x00, 0x01, 0x00, 0x0E, 0x0E, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t LUT_WW[] = {
    0x40, 0x17, 0x00, 0x00, 0x00, 0x02, 0x90, 0x17, 0x17, 0x00, 0x00, 0x02, 0x40, 0x0A,
    0x01, 0x00, 0x00, 0x01, 0xA0, 0x0E, 0x0E, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t LUT_BW[] = {
    0x40, 0x17, 0x00, 0x00, 0x00, 0x02, 0x90, 0x17, 0x17, 0x00, 0x00, 0x02, 0x40, 0x0A,
    0x01, 0x00, 0x00, 0x01, 0xA0, 0x0E, 0x0E, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t LUT_WB[] = {
    0x80, 0x17, 0x00, 0x00, 0x00, 0x02, 0x90, 0x17, 0x17, 0x00, 0x00, 0x02, 0x80, 0x0A,
    0x01, 0x00, 0x00, 0x01, 0x50, 0x0E, 0x0E, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t LUT_BB[] = {
    0x80, 0x17, 0x00, 0x00, 0x00, 0x02, 0x90, 0x17, 0x17, 0x00, 0x00, 0x02, 0x80, 0x0A,
    0x01, 0x00, 0x00, 0x01, 0x50, 0x0E, 0x0E, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void WaveshareEPaper4P2InBWR::load_mono_lut_() {
  this->command(0x20);  // LUT for VCOM
  for (uint8_t v : LUT_VCOM_DC)
    this->data(v);
  this->command(0x21);  // LUT white->white
  for (uint8_t v : LUT_WW)
    this->data(v);
  this->command(0x22);  // LUT black->white
  for (uint8_t v : LUT_BW)
    this->data(v);
  this->command(0x23);  // LUT white->black
  for (uint8_t v : LUT_WB)
    this->data(v);
  this->command(0x24);  // LUT black->black
  for (uint8_t v : LUT_BB)
    this->data(v);
}

void WaveshareEPaper4P2InBWR::initialize() {
  // Base setup() has already pulsed reset. Configure power/booster once; the
  // per-frame panel-setting / LUT / power-on happens in display().

  // COMMAND POWER SETTING
  this->command(0x01);
  this->data(0x03);
  this->data(0x00);
  this->data(0x2B);
  this->data(0x2B);
  this->data(0xFF);

  // COMMAND BOOSTER SOFT START
  this->command(0x06);
  this->data(0x17);
  this->data(0x17);
  this->data(0x17);

  // COMMAND POWER ON
  this->command(0x04);
  this->wait_until_idle_();
  delay(10);

  // COMMAND PLL CONTROL
  this->command(0x30);
  this->data(0x3C);

  delay(2);
}

// BUSY is only sampled after this settle time: the controller asserts BUSY a
// few ms after DISPLAY REFRESH (Waveshare's reference code waits 100 ms too).
static const uint32_t REFRESH_BUSY_SETTLE_MS = 200;
static const uint32_t REFRESH_POLL_INTERVAL_MS = 100;
// Safety net if BUSY never releases (tri-color refresh measured at ~26 s).
static const uint32_t REFRESH_TIMEOUT_MS = 60000;

void WaveshareEPaper4P2InBWR::update() {
  if (this->refresh_running_) {
    // Panel still busy with the previous refresh: a frame sent now would corrupt
    // the running waveform. Redraw with then-current data once it is done.
    this->update_pending_ = true;
    ESP_LOGD(TAG, "Update deferred: refresh still running");
    return;
  }
  this->do_update_();
  this->display();
}

void WaveshareEPaper4P2InBWR::poll_refresh_done_() {
  const uint32_t elapsed = millis() - this->refresh_start_ms_;
  if (elapsed < REFRESH_BUSY_SETTLE_MS)
    return;
  const bool busy = this->busy_pin_ != nullptr && this->busy_pin_->digital_read();
  if (busy && elapsed < REFRESH_TIMEOUT_MS)
    return;

  if (busy) {
    ESP_LOGW(TAG, "Refresh: BUSY still active after %" PRIu32 " ms, powering off anyway", elapsed);
  } else {
    ESP_LOGD(TAG, "Refresh done after %" PRIu32 " ms", elapsed);
  }
  this->cancel_interval("refresh_wait");
  this->command(0x02);  // POWER OFF (< deep sleep; image is retained)
  this->refresh_running_ = false;

  if (this->update_pending_) {
    this->update_pending_ = false;
    // Replay outside this interval callback (display() re-arms the interval).
    this->defer([this]() { this->update(); });
  }
}

void HOT WaveshareEPaper4P2InBWR::display() {
  const uint32_t buf_len = this->get_buffer_length_() / 2u;

  // Does the frame contain any red? (red plane = second buffer half)
  bool has_red = false;
  for (uint32_t i = 0; i < buf_len; i++) {
    if (this->buffer_[buf_len + i] != 0x00) {
      has_red = true;
      break;
    }
  }
  const bool full = has_red || this->full_refresh_requested_;
  this->full_refresh_requested_ = false;

  ESP_LOGD(TAG, "Refresh: %s%s", full ? "full tri-color (OTP LUT, ~26s)" : "fast mono (custom LUT)",
           (full && !has_red) ? " [forced]" : "");

  // POWER ON (registers persist across the power-off at the end of the previous
  // refresh; e-paper holds the image unpowered in between).
  this->command(0x04);
  this->wait_until_idle_();

  // --- Panel setting + LUT (mode dependent) ---
  if (full) {
    // KWR (tri-color) mode, LUT from OTP.
    this->command(0x00);
    this->data(0x0F);
  } else {
    // KW (mono) mode, LUT from register + short custom LUTs.
    this->command(0x00);
    this->data(0xBF);
    this->data(0x0B);
    this->load_mono_lut_();
  }

  // --- Resolution / VCM / VCOM (both modes) ---
  this->command(0x61);  // RESOLUTION SETTING -> 400 x 300
  this->data(0x01);
  this->data(0x90);
  this->data(0x01);
  this->data(0x2C);
  this->command(0x82);  // VCM DC SETTING
  this->data(0x12);
  this->command(0x50);  // VCOM AND DATA INTERVAL SETTING
  this->data(0x97);

  // --- Pixel data ---
  if (full) {
    // Black plane INVERTED -> 0x10 (active-low under the OTP LUT). Invert in
    // place, bulk-write, restore.
    this->command(0x10);
    delay(2);
    for (uint32_t i = 0; i < buf_len; i++)
      this->buffer_[i] ^= 0xFF;
    this->start_data_();
    this->write_array(this->buffer_, buf_len);
    this->end_data_();
    for (uint32_t i = 0; i < buf_len; i++)
      this->buffer_[i] ^= 0xFF;

    // Red plane AS-IS -> 0x13 (active-high: bit=1 -> red).
    this->command(0x13);
    delay(2);
    this->start_data_();
    this->write_array(this->buffer_ + buf_len, buf_len);
    this->end_data_();
  } else {
    // Mono: black plane INVERTED (same active-low wire convention as the OTP
    // path — the BWR draw stores is_on(black)=1, the panel wants 0=black),
    // written to both old (0x10) and new (0x13). Invert in place, write both,
    // restore.
    for (uint32_t i = 0; i < buf_len; i++)
      this->buffer_[i] ^= 0xFF;
    this->command(0x10);
    delay(2);
    this->start_data_();
    this->write_array(this->buffer_, buf_len);
    this->end_data_();
    this->command(0x13);
    delay(2);
    this->start_data_();
    this->write_array(this->buffer_, buf_len);
    this->end_data_();
    for (uint32_t i = 0; i < buf_len; i++)
      this->buffer_[i] ^= 0xFF;
  }

  // --- Refresh (non-blocking) ---
  // BUSY stays active for the whole waveform (~26 s tri-color). Poll for its end
  // instead of blocking the main loop; poll_refresh_done_() then powers off.
  this->command(0x12);  // DISPLAY REFRESH
  this->refresh_running_ = true;
  this->refresh_start_ms_ = millis();
  this->set_interval("refresh_wait", REFRESH_POLL_INTERVAL_MS, [this]() { this->poll_refresh_done_(); });
}

void WaveshareEPaper4P2InBWR::deep_sleep() {
  // Same sequence as the mono UC8176 4.20in driver.
  this->command(0x50);
  this->data(0x17);  // border floating
  this->command(0x82);
  this->command(0x00);
  delay(100);  // NOLINT
  this->command(0x01);
  this->data(0x00);
  this->data(0x00);
  this->data(0x00);
  this->data(0x00);
  this->data(0x00);
  delay(100);  // NOLINT
  this->command(0x02);
  this->wait_until_idle_();
  this->command(0x07);
  this->data(0xA5);  // check byte
}

void WaveshareEPaper4P2InBWR::dump_config() {
  LOG_DISPLAY("", "Waveshare E-Paper (UC8176 BWR, hybrid)", this);
  ESP_LOGCONFIG(TAG, "  Model: 4.2in-B tri-color (400x300, UC8176)");
  ESP_LOGCONFIG(TAG, "  Refresh: mono custom-LUT when no red, else tri-color OTP");
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace waveshare_uc8176_bwr
}  // namespace esphome
