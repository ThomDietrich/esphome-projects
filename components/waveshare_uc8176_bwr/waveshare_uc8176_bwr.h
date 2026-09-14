#pragma once

#include "esphome/components/waveshare_epaper/waveshare_epaper.h"

namespace esphome {
namespace waveshare_uc8176_bwr {

/// Waveshare 4.2" B (400x300, UC8176) black/white/red e-paper — HYBRID refresh.
///
/// The built-in `4.20in-bv2-bwr` model targets the UC8179 / "V2" controller and
/// only refreshes part of this (original UC8176) panel. This driver reuses the
/// proven UC8176 sequences and picks the waveform per frame:
///   * No red in the frame  -> fast MONO refresh with the short custom LUTs
///     and none on the panel   (~2-3 flashes, 2-4 s), like the built-in mono `4.20in`.
///   * Red in the frame, red -> full TRI-COLOR refresh via the panel's OTP LUT
///     still on the panel, or   (~dozens of flashes, ~26.5 s) — the only waveform
///     a forced full refresh    that moves the red pigment (a mono refresh leaves
///                              red residue); it also "deep cleans" the background.
///                              After boot the panel content is unknown -> full.
///
/// request_full_refresh() forces the OTP path once (wired to an HA button) so the
/// long cleaning waveform can be run on demand even without red.
///
/// COLOUR POLARITY (verified on this panel, 2026-07):
///   * TRI-COLOR (OTP LUT): black plane INVERTED -> 0x10, red plane AS-IS -> 0x13.
///   * MONO (custom LUT): black plane INVERTED to both 0x10 and 0x13 (same
///     active-low wire convention as the OTP path; the BWR draw stores
///     is_on(black)=1).
///
/// BUSY (measured on this panel, 2026-09-14): LOW while the controller works,
/// HIGH when idle — the opposite of the ESPHome base class convention, so the
/// busy_pin must be configured with `inverted: true`. Measured BUSY-active times:
/// POWER ON ~40 ms, POWER OFF < 250 ms, mono refresh 2-4 s, tri-color ~26.5 s.
///
/// NON-BLOCKING REFRESH: display() starts the refresh and returns; an interval
/// polls BUSY and powers the panel off once it is idle again. Blocking for ~26 s
/// would stall the main loop (API/HA connection, watchdog). update() calls that
/// arrive while a refresh is running are deferred and replayed afterwards — a
/// frame sent to a busy panel would corrupt the running waveform.
///
/// NO USABLE BUSY SIGNAL (e.g. broken wire): detected per refresh (BUSY not idle
/// after POWER ON, or never active during the refresh); the refresh then
/// completes after a fixed, measured duration instead of a long safety timeout.
///
/// UNCHANGED FRAMES: update() skips the refresh when the rendered frame equals
/// the one on the panel (FNV-1a hash), e.g. the repeated updates while HA
/// connects at boot. request_redraw() / request_full_refresh() force a refresh,
/// and an unchanged image is redrawn after UNCHANGED_MAX_AGE_MS anyway.
class WaveshareEPaper4P2InBWR : public waveshare_epaper::WaveshareEPaperBWR {
 public:
  void initialize() override;
  void update() override;
  void display() override;
  void deep_sleep() override;
  void dump_config() override;

  /// Force a full tri-color (OTP) refresh on the next update, even without red.
  /// Wired to an HA button to deep-clean / whiten the panel on demand.
  void request_full_refresh() { this->full_refresh_requested_ = true; }
  /// Redraw on the next update even if the frame is unchanged.
  void request_redraw() { this->redraw_requested_ = true; }

 protected:
  int get_width_internal() override { return 400; }
  int get_height_internal() override { return 300; }
  void load_mono_lut_();
  /// Interval callback while a refresh runs: power off once it is done.
  void poll_refresh_done_();
  bool busy_() { return this->busy_pin_ != nullptr && this->busy_pin_->digital_read(); }
  /// Short blocking wait for BUSY idle; returns false on timeout.
  bool wait_busy_idle_(uint32_t timeout_ms);
  uint32_t frame_hash_();
  void note_busy_unusable_(const char *reason);

  bool full_refresh_requested_{false};
  bool redraw_requested_{false};
  bool refresh_running_{false};
  bool update_pending_{false};
  uint32_t refresh_start_ms_{0};
  bool refresh_full_{false};
  bool panel_shows_red_{true};     // unknown after boot: assume red, clear it with a full refresh
  bool refresh_use_busy_{true};    // BUSY usable for the running refresh
  bool refresh_busy_seen_{false};  // BUSY went active during the running refresh
  bool frame_shown_{false};
  uint32_t shown_frame_hash_{0};
  uint32_t last_refresh_ms_{0};
};

}  // namespace waveshare_uc8176_bwr
}  // namespace esphome
