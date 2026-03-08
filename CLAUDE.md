# ESPHome Projects

## Entity Naming Convention

Pattern: `Subsystem [Subkomponente] Messgröße [Qualifier]`

Rules:
- German names, ae/oe/ue instead of ä/ö/ü, compound nouns (`Vorlauftemperatur`, `Betriebsstunden`)
- Never repeat the device name — the device provides the namespace
- Subsystem prefix groups related entities: `Mischkreis ...`, `Mischventil ...`, `PID ...`
- If entity *is* the subsystem, name = subsystem alone: `Umwaelzpumpe`
- Optional sub-component for hierarchy: `Mischventil Stellantrieb Motorlaufzeit`
- Parenthetical qualifier for disambiguation: `(10s)`, `(Grundstellung geschlossen)`, `(Reserve)`
- Boilerplate (common/boilerplate.yaml): English, `ESP Device` prefix
- Reusable templates: substitution var as subsystem — `${heat_counter_name} Volumen`

Reference examples: `Mischkreis Vorlauftemperatur`, `PID Regelfehler`, `Mischventil Oeffnung Soll`,
`Umwaelzpumpe Betriebsstunden`, `Primaerkreis Uebertemperatur`, `Mischkreis Automatik`

## Working with Device Files

When reviewing or editing a device YAML file, always read its referenced packages and includes first (`packages:`, `!include`). These define shared IDs, entities, and conventions that the device file depends on.
