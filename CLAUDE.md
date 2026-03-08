# ESPHome Projects

## Sub-Device Naming Convention

Pattern: `Area Anlage Funktion` — must be globally unique and self-explanatory.

Rules:
- Start with the area (e.g. `Keller`), then the system (e.g. `FBH`), then the function
- Names must be unique across the entire HA instance, not just within one ESP device
- Use `devices:` under `esphome:` to define sub-devices; assign entities via `device_id:`
- Define a shared `areas:` entry (id: `area_main`) referencing `${device_area}` for all sub-devices
- Boilerplate entities (ESP Device Status, Restart, Uptime) stay on the main device (no `device_id`)

Examples: `Keller FBH Mischkreis`, `Keller FBH Umwaelzpumpe`, `Keller FBH Waermezaehler`

## Entity Naming Convention

Pattern: `[Subkomponente] Messgröße [Qualifier]`

Rules:
- German names, ae/oe/ue instead of ä/ö/ü, compound nouns (`Vorlauftemperatur`, `Betriebsstunden`)
- Never repeat the sub-device name — the sub-device provides the namespace
- Sub-component prefix groups related entities within a sub-device: `Mischventil ...`, `PID ...`, `Zufluss ...`
- If entity *is* the sub-device (e.g. a pump switch), use a generic name like `Schalter`
- Optional sub-component for hierarchy: `Mischventil Stellantrieb Motorlaufzeit`
- Parenthetical qualifier for disambiguation: `(10s)`, `(Grundstellung geschlossen)`, `(Reserve)`
- Boilerplate (common/boilerplate.yaml): English, `ESP Device` prefix, no `device_id`
- Reusable templates: substitution var as sub-component — `${heat_counter_name} Volumen`

Examples (sub-device → entity → HA result):
- `Keller FBH Mischkreis` → `Vorlauftemperatur` → "Keller FBH Mischkreis Vorlauftemperatur"
- `Keller FBH Mischkreis` → `Mischventil Oeffnung` → "Keller FBH Mischkreis Mischventil Oeffnung"
- `Keller FBH Mischkreis` → `PID Regelfehler` → "Keller FBH Mischkreis PID Regelfehler"
- `Keller FBH Umwaelzpumpe` → `Schalter` → "Keller FBH Umwaelzpumpe Schalter"
- `Keller FBH Umwaelzpumpe` → `Betriebsstunden` → "Keller FBH Umwaelzpumpe Betriebsstunden"

## Working with Device Files

When reviewing or editing a device YAML file, always read its referenced packages and includes first (`packages:`, `!include`). These define shared IDs, entities, and conventions that the device file depends on.
