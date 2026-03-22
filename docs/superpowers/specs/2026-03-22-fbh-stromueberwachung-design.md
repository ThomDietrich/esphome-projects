# FBH Stromueberwachung — Design Spec

## Summary

Replace the TOMZN S0 energy meter (2000 imp/kWh, too coarse for real-time monitoring) with either a PZEM-004T v3 (UART) or Eastron SDM120-M (Modbus RS485 via SP3485 adapter). Use real-time power readings (~1s, sub-watt resolution) to decompose total FBH installation power into component states, detect anomalies, and provide power-confirmed valve end-stop detection.

All logic runs on the ESP (Astra controller). No HA dependency for decision-making.

## Hardware

### Option A: PZEM-004T v3
- Interface: UART direct, 9600 baud, 2 GPIOs (e.g. GPIO14 TX, GPIO15 RX)
- Resolution: 0.1W
- DIN rail: 3D-printed enclosure + closed CT clamp
- Starting current: ~20mA (~4.6W at 230V)

### Option B: Eastron SDM120-M
- Interface: Modbus RS485 via SP3485 (3.3V) adapter, 9600 baud, 3 GPIOs (e.g. GPIO14 TX, GPIO15 RX, GPIO27 DE/RE)
- Resolution: 1W
- DIN rail: native 1 DIN module, inline connection
- Starting current: ~20mA (~4.6W at 230V)

Both templates use the same substitution variable pattern (`energy_meter_id`, `energy_meter_device_id`, etc.) and expose identical sensor IDs: `sensor_${energy_meter_id}_strom_leistung`, `sensor_${energy_meter_id}_strom_energie`. With `energy_meter_id: "fbh"`, downstream logic references `sensor_fbh_strom_leistung` regardless of which meter is installed.

### Template substitution interface (both templates)
```yaml
energy_meter_id: "fbh"                    # ID prefix for all sensors
energy_meter_uart_id: "uart_stromzaehler" # UART bus ID
energy_meter_device_id: "stromzaehler"    # Sub-device assignment
```
SDM120 additionally requires: `energy_meter_modbus_address: "1"`

### GPIO allocation
- GPIO34: freed (was S0 input)
- GPIO14, GPIO15: UART TX/RX to meter (both options)
- GPIO27: DE/RE direction control (SDM120 only)

## Sub-device

New sub-device in `keller-fbh-regelung.yaml`:
```yaml
- id: stromzaehler
  name: "Keller FBH Stromzaehler"
  area_id: area_main
```

All electrical power entities get `device_id: stromzaehler`.

## Substitution Variables

```yaml
leistung_idle_w: "3"            # Astra + PSU idle draw [W]
leistung_pumpe_w: "6"           # Pump additional draw [W]
leistung_stellantrieb_w: "5"    # Valve motor additional draw [W]
leistung_toleranz_w: "2"        # Acceptable deviation [W]
```

Values to be measured after installation and updated.

## Use Cases

### 1. Power-confirmed valve movement (`mischventil_laeuft`)
Binary sensor: TRUE when the valve motor is actually drawing power. Replaces the old script-based `ventil_faehrt` which only assumed movement. Derived from: actual power > expected-without-motor + tolerance, while a valve relay is ON.

Brief `delayed_on` (~500ms) to handle relay switching transients and motor inrush.

**Fallback when power sensor unavailable:** If the power sensor has no valid reading (NaN, no value, or boot grace period), `mischventil_laeuft` falls back to time-based behavior — it mirrors the relay state directly (TRUE while relay is ON), equivalent to the old `ventil_faehrt`. This ensures the valve controller works without the power meter (graceful degradation). A binary sensor `strom_sensor_verfuegbar` tracks whether power readings are valid.

### 2. Valve end-stop detection
When `mischventil_laeuft` goes FALSE while a valve relay is still ON, the motor has hit the mechanical end-stop. The movement script detects this, stops the relay, and clamps position to 0% or 100%.

Only active when power sensor is available (fallback mode uses time-based movement as before).

### 3. Valve travel time calibration (`mischventil_kalibrieren`)
Three-phase sequence:
1. Close until end-stop (power drop) — position = 0%
2. Open until end-stop — measure duration → `mischventil_laufzeit_oeffnen_s`, position = 100%
3. Close until end-stop — measure duration → `mischventil_laufzeit_schliessen_s`, position = 0%

Preconditions:
- Power sensor must be available (`strom_sensor_verfuegbar` = TRUE). Calibration refuses to start otherwise.
- PID controller and position controller (`mischventil_regler`) are paused during calibration.
- Automation (`fbh_betrieb`) remains enabled but manual override is temporarily set.

Abort conditions:
- Safety lockout (`sicherheitsabschaltung`) triggers → abort, restore previous state.
- Manual button press during calibration → abort (future consideration).

Plausibility guard: only accept durations in 100-180s range. Otherwise keep previous value and log error.

Travel time globals:
- `mischventil_laufzeit_oeffnen_s`: type `float`, persistent, default `140.0`
- `mischventil_laufzeit_schliessen_s`: type `float`, persistent, default `140.0`

Exposed as diagnostic sensors:
- Entity name: `Mischventil Laufzeit Oeffnen` [s]
- Entity name: `Mischventil Laufzeit Schliessen` [s]

Both on sub-device `mischer`.

### 4. Pump failure detection (`pumpe_leistungsausfall`)
Binary sensor: pump relay ON but actual power < expected - tolerance. Delayed 3s for relay transients. After 30s persistence: log WARNING, HA notification. No lockout.

### 5. Valve motor failure detection (`mischventil_leistungsausfall`)
Binary sensor: valve driving but actual power < expected - tolerance. Delayed 3s. After 10s persistence: log WARNING, HA notification. No lockout.

### 6. Overconsumption detection (`strom_ueberverbrauch`)
Binary sensor: actual power > expected + tolerance. After 10s persistence: log ERROR, HA notification. No lockout.

### 7. Power deviation sensor (`strom_leistung_abweichung`)
Template sensor [W]: `actual - expected`. Shows real-time deviation for diagnostics and tuning substitution values.

### 8. Power-based state display (`strom_betriebszustand`)
Text sensor on `stromzaehler`, updated via `on_value` from the power sensor (not polling):
- `"Leerlauf"` — idle only
- `"Pumpe aktiv"` — idle + pump
- `"Pumpe + Ventil oeffnet"` / `"Pumpe + Ventil schliesst"`
- `"Ventil oeffnet"` / `"Ventil schliesst"`
- `"Abweichung"` — any anomaly active
- `"Sensor nicht verfuegbar"` — power sensor unavailable

### 9. Energy accounting
Cumulative kWh (`strom_energie`), carried forward from the S0 meter concept. State class `total_increasing`. Note: HA energy dashboard handles meter resets gracefully with `total_increasing`; the counter will start from near-zero after the meter swap.

## Expected Power Calculation

Template sensor `strom_leistung_erwartet` [W]:
```
expected = leistung_idle_w
if pumpe_schalter ON:  expected += leistung_pumpe_w
if valve relay ON:     expected += leistung_stellantrieb_w
```

Note: `leistung_idle_w` should include the meter's own consumption if the CT/inline connection measures the meter's own circuit. To be verified during installation.

## Valve Control Refactoring

### ID renames (English → German, consistent `mischventil_` prefix)

| Current | New |
|---|---|
| `estimated_valve_position` | `mischventil_position` |
| `valve_motor_runtime_s` | `mischventil_motorlaufzeit_s` |
| `valve_cycle_count` | `mischventil_schaltzyklen` |
| `valve_stop_time_ms` | `mischventil_stopzeit_ms` |
| `pulse_duration_ms` | `mischventil_impulsdauer_ms` |
| `manual_valve_override` | `mischventil_override` |
| `last_recal_uptime_s` | `mischventil_letzte_kalibrierung_s` |
| `drive_valve_open` | `mischventil_oeffnen` |
| `drive_valve_close` | `mischventil_schliessen` |
| `position_controller` | `mischventil_regler` |
| `recalibrate_valve` | `mischventil_kalibrieren` (now three-phase calibration, fully replaces old single-phase recalibration) |
| `ventil_faehrt` | removed (replaced by `mischventil_laeuft` from stromueberwachung) |
| `safety_lockout` | `sicherheitsabschaltung` |
| `safety_reset` | `sicherheitsreset` |

### Substitution removal
`stellantrieb_laufzeit_s` removed. Replaced by two persistent globals with directional travel times, defaulting to 140.0s.

### Position controller changes
Pulse duration uses direction-specific travel time:
- Opening: `|diff| * mischventil_laufzeit_oeffnen_s`
- Closing: `|diff| * mischventil_laufzeit_schliessen_s`

### Movement script changes
During movement, monitor `mischventil_laeuft`. If it goes FALSE while relay is ON:
- End-stop reached
- Stop relay
- Clamp position to 0% (closing) or 100% (opening)
- Record elapsed time (used during calibration)

When power sensor is unavailable (fallback mode): movement scripts use time-based behavior as before (run for calculated pulse duration, update position estimate by elapsed time).

## Safety Integration

Power anomalies are **diagnostic alerts**, not safety lockouts:
- `strom_ueberverbrauch` >10s → ERROR log + HA notification
- `pumpe_leistungsausfall` >30s → WARNING log + HA notification
- `mischventil_leistungsausfall` >10s → WARNING log + HA notification

Existing hard safety (overheat >50C, sensor faults) unchanged.

## File Structure

### New files
| File | Purpose |
|---|---|
| `include/pzem004t_energy_meter_template.yaml` | PZEM-004T hardware abstraction (template with substitution vars) |
| `include/sdm120_energy_meter_template.yaml` | SDM120-M hardware abstraction (template with substitution vars) |
| `include/keller_fbh_stromueberwachung.yaml` | Power monitoring, anomaly detection, state display, `mischventil_laeuft` |

### Modified files
| File | Changes |
|---|---|
| `keller-fbh-regelung.yaml` | Replace S0 include, add `stromzaehler` sub-device, add power substitutions, remove `stellantrieb_laufzeit_s` |
| `include/keller_fbh_mischventil.yaml` | Rename IDs to `mischventil_` German prefix, direction-specific travel times, end-stop via `mischventil_laeuft`, three-phase calibration replaces old recalibration, fallback mode when power sensor unavailable |
| `include/keller_fbh_sicherheit.yaml` | Add power anomaly notifications, rename `safety_lockout` → `sicherheitsabschaltung`, `safety_reset` → `sicherheitsreset` |
| `include/keller_fbh_betrieb.yaml` | Update references to renamed globals (`sicherheitsabschaltung`, `mischventil_override`, `mischventil_regler`) |
| `include/keller_fbh_pumpe.yaml` | Update references if needed |
| `include/keller_fbh_pid.yaml` | Update global references (`mischventil_position` etc.) |

### Retained (cross-project)
| File | Note |
|---|---|
| `include/s0_energy_meter_template.yaml` | NOT removed — used by `keller-zh-regelung.yaml`. No longer included by `keller-fbh-regelung.yaml`. |

### Dependencies
`keller_fbh_mischventil.yaml` depends on `keller_fbh_stromueberwachung.yaml` for `mischventil_laeuft` and `strom_sensor_verfuegbar`. ESPHome's package merge resolves cross-package ID references regardless of include order.
