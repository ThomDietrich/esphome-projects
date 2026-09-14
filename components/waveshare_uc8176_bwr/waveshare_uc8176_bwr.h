#pragma once

#include "esphome/components/waveshare_epaper/waveshare_epaper.h"

namespace esphome {
namespace waveshare_uc8176_bwr {

/// Waveshare 4.2" B (400x300, UC8176) black/white/red e-paper — HYBRID refresh.
///
/// The built-in `4.20in-bv2-bwr` model targets the UC8179 / "V2" controller and
/// only refreshes part of this (original UC8176) panel. This driver reuses the
/// proven UC8176 sequences and picks the waveform per frame:
///   * No red in the frame -> fast MONO refresh with the short custom LUTs
///                            (~2-3 flashes), like the built-in mono `4.20in`.
///   * Red present (or a    -> full TRI-COLOR refresh via the panel's OTP LUT
///     forced full refresh)   (~dozens of flashes, ~26 s) — required to move the
///                            red pigment; the long waveform also "deep cleans"
///                            the background to a brighter white.
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
/// busy_pin must be configured with `inverted: true`. A tri-color refresh keeps
/// BUSY active for ~26 s after DISPLAY REFRESH.
///
/// NON-BLOCKING REFRESH: display() starts the refresh and returns; an interval
/// polls BUSY and powers the panel off once it is idle again. Blocking for ~26 s
/// would stall the main loop (API/HA connection, watchdog). update() calls that
/// arrive while a refresh is running are deferred and replayed afterwards — a
/// frame sent to a busy panel would corrupt the running waveform.
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

 protected:
  int get_width_internal() override { return 400; }
  int get_height_internal() override { return 300; }
  void load_mono_lut_();
  /// Interval callback while a refresh runs: power off once BUSY is released.
  void poll_refresh_done_();

  bool full_refresh_requested_{false};
  bool refresh_running_{false};
  bool update_pending_{false};
  uint32_t refresh_start_ms_{0};
};

}  // namespace waveshare_uc8176_bwr
}  // namespace esphome
