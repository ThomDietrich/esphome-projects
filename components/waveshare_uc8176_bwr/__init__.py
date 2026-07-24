# External ESPHome component: Waveshare 4.2" B (400x300, UC8176) tri-color driver.
#
# The built-in waveshare_epaper `4.20in-bv2-bwr` model targets the newer UC8179 /
# "V2" controller and only refreshes part of the *original* UC8176 4.2" B panel
# (ESPHome issue #5369). This component reuses the proven UC8176 init/resolution
# sequence from the mono `4.20in` model, but drives the panel in tri-color (BWR)
# mode. It subclasses waveshare_epaper's BWR base, so that component must be
# compiled alongside it.
AUTO_LOAD = ["waveshare_epaper"]
