#include "waveshare_uc8176_bwr.h"
#include <cinttypes>
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace waveshare_uc8176_bwr {

static const char *const TAG = "waveshare_uc8176_bwr";

// POWER ON releases BUSY after ~40 ms, POWER OFF after < 250 ms (measured).
static const uint32_t POWER_ON_TIMEOUT_MS = 300;
static const uint32_t POWER_OFF_TIMEOUT_MS = 500;
// BUSY asserts within a few ms after DISPLAY REFRESH; judged only after this.
static const uint32_t REFRESH_BUSY_SETTLE_MS = 200;
static const uint32_t REFRESH_POLL_INTERVAL_MS = 100;
// Safety net if BUSY never releases (tri-color refresh measured at ~26.5 s).
static const uint32_t REFRESH_TIMEOUT_MS = 45000;
// Fixed refresh durations if BUSY gives no usable signal (measured mono ~4.2 s,
// tri-color ~26.5 s at room temperature; plus margin for colder panels).
static const uint32_t REFRESH_FIXED_MONO_MS = 6000;
static const uint32_t REFRESH_FIXED_FULL_MS = 32000;
// An unchanged frame is still redrawn after this age (keeps the pigments fresh).
static const uint32_t UNCHANGED_MAX_AGE_MS = 6UL * 60UL * 60UL * 1000UL;

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
  this->wait_busy_idle_(POWER_ON_TIMEOUT_MS);  // BUSY usability is judged per refresh in display()
  delay(10);

  // COMMAND PLL CONTROL
  this->command(0x30);
  this->data(0x3C);

  delay(2);
}

bool WaveshareEPaper4P2InBWR::wait_busy_idle_(uint32_t timeout_ms) {
  // Own short wait instead of the base wait_until_idle_() (fixed 1 s timeout):
  // POWER ON/OFF take well below these timeouts on a panel with a working BUSY.
  const uint32_t start = millis();
  while (this->busy_()) {
    if (millis() - start >= timeout_ms)
      return false;
    delay(1);
  }
  return true;
}

uint32_t WaveshareEPaper4P2InBWR::frame_hash_() {
  // FNV-1a over both planes: 4 bytes of state instead of a 30 KB frame copy.
  uint32_t hash = 2166136261UL;
  const uint32_t len = this->get_buffer_length_();
  for (uint32_t i = 0; i < len; i++) {
    hash ^= this->buffer_[i];
    hash *= 16777619UL;
  }
  return hash;
}

void WaveshareEPaper4P2InBWR::note_busy_unusable_(const char *reason) {
  // Logged per refresh rather than once: boot-time logs never reach an API log client.
  ESP_LOGW(TAG, "BUSY gives no usable signal (%s) - check the BUSY wire; using fixed refresh time", reason);
}

void WaveshareEPaper4P2InBWR::update() {
  if (this->refresh_running_) {
    // Panel still busy with the previous refresh: a frame sent now would corrupt
    // the running waveform. Redraw with then-current data once it is done.
    this->update_pending_ = true;
    ESP_LOGD(TAG, "Update deferred: refresh still running");
    return;
  }
  this->do_update_();

  // Skip the refresh if the panel already shows exactly this frame (e.g. the
  // burst of updates while HA connects at boot): a tri-color refresh flashes for
  // ~26 s. Explicit requests and images older than the max age are redrawn.
  const uint32_t hash = this->frame_hash_();
  const bool forced = this->redraw_requested_ || this->full_refresh_requested_;
  const bool stale = millis() - this->last_refresh_ms_ >= UNCHANGED_MAX_AGE_MS;
  if (this->frame_shown_ && hash == this->shown_frame_hash_ && !forced && !stale) {
    ESP_LOGD(TAG, "Frame unchanged, refresh skipped");
    return;
  }
  this->redraw_requested_ = false;
  this->shown_frame_hash_ = hash;
  this->frame_shown_ = true;
  this->display();
}

void WaveshareEPaper4P2InBWR::poll_refresh_done_() {
  const uint32_t elapsed = millis() - this->refresh_start_ms_;
  const bool busy = this->busy_();
  if (busy)
    this->refresh_busy_seen_ = true;
  if (elapsed < REFRESH_BUSY_SETTLE_MS)
    return;

  if (this->refresh_use_busy_ && !this->refresh_busy_seen_) {
    // A working BUSY is active for the whole waveform (seconds): never seen
    // active means the signal is stuck idle.
    this->refresh_use_busy_ = false;
    this->note_busy_unusable_("not active during refresh");
  }

  if (this->refresh_use_busy_) {
    if (busy && elapsed < REFRESH_TIMEOUT_MS)
      return;
    if (busy) {
      ESP_LOGW(TAG, "Refresh: BUSY still active after %" PRIu32 " ms, powering off anyway", elapsed);
    } else {
      ESP_LOGD(TAG, "Refresh done after %" PRIu32 " ms", elapsed);
    }
  } else {
    if (elapsed < (this->refresh_full_ ? REFRESH_FIXED_FULL_MS : REFRESH_FIXED_MONO_MS))
      return;
    ESP_LOGD(TAG, "Refresh assumed done after fixed %" PRIu32 " ms (no BUSY signal)", elapsed);
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
  // Only the tri-color (OTP) waveform moves the red pigment: a mono refresh
  // leaves the previous frame's red visible (verified 2026-09-14). So the full
  // refresh is also needed while the panel still shows red.
  const bool red_leaving = this->panel_shows_red_ && !has_red;
  const bool full = has_red || red_leaving || this->full_refresh_requested_;
  this->full_refresh_requested_ = false;
  this->panel_shows_red_ = has_red;

  ESP_LOGD(TAG, "Refresh: %s%s", full ? "full tri-color (OTP LUT, ~26s)" : "fast mono (custom LUT)",
           (!full || has_red) ? "" : (red_leaving ? " [clearing red]" : " [forced]"));

  // POWER ON (registers persist across the power-off at the end of the previous
  // refresh; e-paper holds the image unpowered in between). A working BUSY is
  // idle again after ~40 ms; otherwise it is unusable for this refresh.
  this->command(0x04);
  this->refresh_use_busy_ = this->wait_busy_idle_(POWER_ON_TIMEOUT_MS);
  if (!this->refresh_use_busy_)
    this->note_busy_unusable_("still active after POWER ON");

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
  this->refresh_full_ = full;
  this->refresh_busy_seen_ = false;
  this->refresh_running_ = true;
  this->refresh_start_ms_ = millis();
  this->last_refresh_ms_ = this->refresh_start_ms_;
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
  // While a refresh runs, BUSY stays active until the waveform ends (the reset
  // at the next boot aborts it cleanly, verified): waiting would only delay.
  if (!this->refresh_running_)
    this->wait_busy_idle_(POWER_OFF_TIMEOUT_MS);
  this->command(0x07);
  this->data(0xA5);  // check byte
}

void WaveshareEPaper4P2InBWR::dump_config() {
  LOG_DISPLAY("", "Waveshare E-Paper (UC8176 BWR, hybrid)", this);
  ESP_LOGCONFIG(TAG, "  Model: 4.2in-B tri-color (400x300, UC8176)");
  ESP_LOGCONFIG(TAG, "  Refresh: mono custom-LUT when no red, else tri-color OTP");
  ESP_LOGCONFIG(TAG, "  Refresh wait: non-blocking via BUSY (LOW-active), fixed times if BUSY unusable");
  ESP_LOGCONFIG(TAG, "  Unchanged frames: skipped, redrawn after %" PRIu32 " min", UNCHANGED_MAX_AGE_MS / 60000UL);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace waveshare_uc8176_bwr
}  // namespace esphome
