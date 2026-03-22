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

Both expose identical sensor IDs (`sensor_fbh_strom_leistung`, `sensor_fbh_strom_energie`) so downstream logic works with either.

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

### 2. Valve end-stop detection
When `mischventil_laeuft` goes FALSE while a valve relay is still ON, the motor has hit the mechanical end-stop. The movement script detects this, stops the relay, and clamps position to 0% or 100%.

### 3. Valve travel time calibration (`mischventil_kalibrieren`)
Three-phase sequence:
1. Close until end-stop (power drop) — position = 0%
2. Open until end-stop — measure duration → `mischventil_laufzeit_oeffnen_s`, position = 100%
3. Close until end-stop — measure duration → `mischventil_laufzeit_schliessen_s`, position = 0%

Plausibility guard: only accept durations in 100-180s range. Otherwise keep previous value and log error.

Measured durations stored in persistent globals, exposed as diagnostic sensors, and used by the position controller for pulse duration calculation.

### 4. Pump failure detection (`pumpe_leistungsausfall`)
Binary sensor: pump relay ON but actual power < expected - tolerance. Delayed 3s for relay transients. After 30s persistence: log WARNING, HA notification. No lockout.

### 5. Valve motor failure detection (`stellantrieb_leistungsausfall`)
Binary sensor: valve driving but actual power < expected - tolerance. Delayed 3s. After 10s persistence: log WARNING, HA notification. No lockout.

### 6. Overconsumption detection (`strom_ueberverbrauch`)
Binary sensor: actual power > expected + tolerance. After 10s persistence: log ERROR, HA notification. No lockout.

### 7. Power-based state display (`strom_betriebszustand`)
Text sensor on `stromzaehler`:
- `"Leerlauf"` — idle only
- `"Pumpe aktiv"` — idle + pump
- `"Pumpe + Ventil oeffnet"` / `"Pumpe + Ventil schliesst"`
- `"Ventil oeffnet"` / `"Ventil schliesst"`
- `"Abweichung"` — any anomaly active

### 8. Energy accounting
Cumulative kWh (`strom_energie`), carried forward from the S0 meter concept. State class `total_increasing`.

## Expected Power Calculation

Template sensor `strom_leistung_erwartet` [W]:
```
expected = leistung_idle_w
if pumpe_schalter ON:  expected += leistung_pumpe_w
if valve relay ON:     expected += leistung_stellantrieb_w
```

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
| `recalibrate_valve` | `mischventil_kalibrieren` |
| `ventil_faehrt` | removed (replaced by `mischventil_laeuft`) |
| `safety_lockout` | `sicherheitsabschaltung` |
| `safety_reset` | `sicherheits_reset` |

### Substitution removal
`stellantrieb_laufzeit_s` removed. Replaced by two persistent globals with directional travel times, defaulting to 140s.

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

## Safety Integration

Power anomalies are **diagnostic alerts**, not safety lockouts:
- `strom_ueberverbrauch` >10s → ERROR log + HA notification
- `pumpe_leistungsausfall` >30s → WARNING log + HA notification
- `stellantrieb_leistungsausfall` >10s → WARNING log + HA notification

Existing hard safety (overheat >50C, sensor faults) unchanged.

## File Structure

### New files
| File | Purpose |
|---|---|
| `include/pzem004t_energy_meter_template.yaml` | PZEM-004T hardware abstraction |
| `include/sdm120_energy_meter_template.yaml` | SDM120-M hardware abstraction |
| `include/keller_fbh_stromueberwachung.yaml` | Power monitoring, anomaly detection, state display |

### Modified files
| File | Changes |
|---|---|
| `keller-fbh-regelung.yaml` | Replace S0 include, add `stromzaehler` sub-device, add power substitutions, remove `stellantrieb_laufzeit_s` |
| `include/keller_fbh_mischventil.yaml` | Rename IDs to `mischventil_` German prefix, direction-specific travel times, end-stop via `mischventil_laeuft`, new calibration script |
| `include/keller_fbh_sicherheit.yaml` | Add power anomaly notifications, rename `safety_lockout` → `sicherheitsabschaltung` |
| `include/keller_fbh_betrieb.yaml` | Update references to renamed IDs |
| `include/keller_fbh_pumpe.yaml` | Update references if needed |
| `include/keller_fbh_temperatursensoren.yaml` | Update `ventil_faehrt` → `mischventil_laeuft` |
| `include/keller_fbh_pid.yaml` | Update global references |

### Removed
| File | Reason |
|---|---|
| `include/s0_energy_meter_template.yaml` | Replaced by PZEM/SDM templates (check cross-project usage first) |

### Dependency
`keller_fbh_mischventil.yaml` now depends on `keller_fbh_stromueberwachung.yaml` for `mischventil_laeuft`. Include order in main YAML must reflect this (stromueberwachung before mischventil).
