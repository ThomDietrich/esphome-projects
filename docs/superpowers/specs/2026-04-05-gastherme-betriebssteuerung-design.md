# Gastherme Betriebssteuerung — Design-Spezifikation

**Datum:** 2026-04-05
**Betrifft:** keller-zh-regelung (Keller ZH Regelung)
**Status:** Entwurf


## 1. Problemstellung

Die Gastherme (Buderus GB 162-25) bedient zwei unabhaengige Anforderungen: Heizung und
Warmwasserbereitung (WWB). Physisch kann sie nur eine zur Zeit ausfuehren — ein internes
3-Wege-Umschaltventil lenkt den Waermetraeger entweder in den Heizkreis oder den
Warmwasserwaermetauscher. Die Umschaltung wird ueber ein Relais (SLP-Signal) gesteuert.

### 1.1 Ist-Zustand (Probleme)

Ein 3-Wege-Select auf dem ESP (`Aus`/`Heizen`/`Warmwasser`) wird direkt von HA-Automationen
gesetzt. Das fuehrt zu:

- **Vermischung von Anforderung und Zustand:** Der Select repraesentiert gleichzeitig was HA
  will und was die Gastherme tut. Zwei unabhaengige Anforderungen werden in einen einzelnen
  Zustand gepresst.
- **Race-Conditions:** Beim Beenden der WWB setzt HA den Sollwert auf 0 (→ ESP schaltet auf
  "Aus") und dann den Modus auf "Heizen". Durch die bidirektionale Kopplung im ESP gewinnt
  der erste Befehl und die Gastherme bleibt auf "Aus".
- **Keine ESP-Autonomie:** Der ESP fuehrt nur Befehle aus. Sicherheitskritische Abschaltungen
  (Uebertemperatur, Sensorausfall) liegen in HA und sind von dessen Verfuegbarkeit abhaengig.

### 1.2 Architekturentscheidung

**Anforderungsbasiertes Modell:** HA sendet unabhaengige Anforderungen, der ESP arbitriert
und steuert die Gastherme autonom.

**Begruendung:**
- Zwei logisch unabhaengige Anforderungen (Heizung, WWB) ergeben genau einen physischen
  Gastherme-Zustand — diese Abbildung gehoert auf die hardwarenahe Ebene (ESP)
- Sicherheitskritische Abschaltungen (Uebertemperatur, Sensorausfall) muessen ohne HA
  funktionieren
- Race-Conditions werden eliminiert, da der ESP den Zustand atomar verwaltet

**Verworfene Alternativen:**
- *Minimaler Umbau (nur WWB-Abschaltung auf ESP):* Loest das Kernproblem nicht
  (Anforderung ≠ Zustand)
- *Vollstaendige ESP-Autonomie inkl. Zeitsteuerung:* ESPHome nicht ideal fuer komplexe
  Zeitlogik (Wochentage, Legionellenschutz samstags). Aenderungen erfordern Firmware-Update.


## 2. Anforderungsmodell

### 2.1 Unabhaengige Anforderungen (Eingaenge)

| Anforderung | Quelle (HA) | API-Action | Parameter |
|-------------|-------------|------------|-----------|
| Heizung | Heizkennlinie, Absenkungen | `heizung_anfordern` | `sollwert: float` (0 = kein Bedarf) |
| Warmwasser | Zeitplaene, Schwellwerte | `warmwasser_anfordern` | `zieltemperatur: float` |

Jede Anforderung kann unabhaengig aktiviert/deaktiviert werden ueber bestehende
`input_boolean`-Toggles in HA:
- `input_boolean.keller_zh_heizung_automation`
- `input_boolean.keller_zh_warmwasser_automation`

"Gastherme aus" = beide Automationen deaktiviert → keine Anforderungen → ESP geht auf Aus.

### 2.2 Resultierender Gastherme-Zustand (Ausgang)

| Zustand | Bedingung | Relais 4 | Sollwert |
|---------|-----------|----------|----------|
| Aus | Keine Anforderung aktiv | AUS | 0°C |
| Heizen | Heizung angefordert, WWB nicht aktiv | AUS | Gespeicherter Sollwert |
| Warmwasser | WWB aktiv (hat immer Vorrang) | EIN | Gespeicherter Sollwert (von Therme ignoriert) |

### 2.3 WWB-Vorrang

Warmwasserbereitung hat immer Vorrang vor Heizung. Begruendung:
- Gastherme kann physisch nur eins zur Zeit (3-Wege-Umschaltventil)
- WWB dauert typisch 5-25 Minuten — thermische Traegheit des Gebaeudes ueberbrueckt die Pause
- Kaltes Wasser am Hahn ist sofort spuerbar, eine kurze Heizpause nicht
- Hygiene (Legionellenschutz) erfordert zeitnahe Warmwasserbereitung
- Branchenstandard: Warmwasservorrangschaltung ist die Norm bei Gasthermen


## 3. ESP-seitige Implementierung

### 3.1 Neue Datei: `include/keller_zh_gastherme_betriebssteuerung.yaml`

Zentrale Steuerungsdatei. Enthaelt: Globals, API-Actions, Arbitrierungslogik,
WWB-Ueberwachung, Status-Sensoren.

### 3.2 Globals

| ID | Typ | Persistent | Default | Beschreibung |
|----|-----|------------|---------|--------------|
| `global_heizung_sollwert` | float | ja | 0.0 | Letzter Heizungs-Sollwert von HA |
| `global_wwb_zieltemperatur` | float | nein | 0.0 | WWB-Zieltemperatur (0 = nicht angefordert) |
| `global_wwb_aktiv` | bool | nein | false | WWB laeuft gerade |
| `global_wwb_startzeit_ms` | uint32_t | nein | 0 | millis() bei WWB-Start (fuer Laufzeitbegrenzung) |

`global_wwb_aktiv` und `global_wwb_startzeit_ms` sind nicht persistent — nach ESP-Neustart
ist keine WWB aktiv. Das ist korrekt: ein Neustart ist ein sauberer Zustand, HA wird bei
Bedarf erneut anfordern.

### 3.3 API-Actions

#### `heizung_anfordern(sollwert: float)`
1. `global_heizung_sollwert = sollwert`
2. Arbitrierung auswerten (→ 3.4)

#### `warmwasser_anfordern(zieltemperatur: float)`
1. **Guard-Check:** Wenn `warmwasserspeicher_oben >= zieltemperatur` → Anforderung ignorieren,
   Ergebnis = "Zieltemperatur erreicht"
2. **Guard-Check:** Wenn `warmwasserspeicher_oben` ist NaN → Anforderung ablehnen,
   Ergebnis = "Sensorausfall"
3. `global_wwb_zieltemperatur = zieltemperatur`
4. `global_wwb_aktiv = true`
5. `global_wwb_startzeit_ms = millis()`
6. Arbitrierung auswerten (→ 3.4)

**Bisherige API-Action `gastherme_solltemperatur_set` entfaellt.**

### 3.4 Arbitrierungslogik (Script `betriebssteuerung_auswerten`)

Aufgerufen bei: API-Action, WWB-Abbruch, Boot.

```
IF global_wwb_aktiv:
  → Betriebsmodus = "Warmwasser"
  → Relais EIN
  → Sollwert anwenden (gespeichert, wird von Therme im WWB-Modus ignoriert)
ELIF global_heizung_sollwert > 0:
  → Betriebsmodus = "Heizen"
  → Relais AUS
  → Sollwert anwenden
ELSE:
  → Betriebsmodus = "Aus"
  → Relais AUS
  → Sollwert = 0
```

### 3.5 WWB-Abbruchbedingungen

Waehrend `global_wwb_aktiv == true` ueberwacht der ESP kontinuierlich:

| Prio | Bedingung | Ausloeser | Ergebnis-Text | Reaktion |
|------|-----------|-----------|---------------|----------|
| 1 | Sensorausfall | `warmwasserspeicher_oben` ist NaN fuer > 30s | `Sensorausfall` | WWB beenden, zurueck zu Heizen (falls Sollwert > 0) |
| 2 | Max. Speichertemp | `warmwasserspeicher_oben >= max_temperatur` | `Maximale Speichertemperatur` | WWB beenden |
| 3 | Zieltemperatur | `warmwasserspeicher_oben >= zieltemperatur` | `Zieltemperatur erreicht` | WWB beenden |
| 4 | Max. Laufzeit | `millis() - startzeit > max_laufzeit_ms` | `Maximale Laufzeit` | WWB beenden |

**Abbruch-Prozedur:**
1. `global_wwb_aktiv = false`
2. `global_wwb_zieltemperatur = 0`
3. Ergebnis-Sensor publizieren
4. Arbitrierung auswerten → faellt zurueck zu "Heizen" oder "Aus"

**Sensorausfall-Verhalten:**
- Waehrend WWB: Abbruch nach 30s NaN, zurueck zu Heizen (Sensor nicht sicherheitsrelevant
  im Heizmodus — Gastherme heizt den Heizkreis, nicht den Speicher)
- Bei WWB-Anforderung: Anforderung ablehnen (sofort, ohne Wartezeit)
- Waehrend Heizen: Kein Einfluss (Heizkennlinie basiert auf Aussentemperatur)

### 3.6 Konfigurierbare Parameter (number-Entities)

| Entity | Default | Min | Max | Beschreibung |
|--------|---------|-----|-----|--------------|
| WWB Max. Laufzeit | 25 min | 5 | 60 | Maximale Dauer einer WWB |
| WWB Max. Speichertemperatur | 70°C | 50 | 90 | Sicherheitsgrenze Speicher |

### 3.7 Status-Sensoren (Rueckmeldung an HA)

| Sensor | Typ | Werte | Beschreibung |
|--------|-----|-------|--------------|
| `Betriebsmodus` | text_sensor | Aus / Heizen / Warmwasser | Aktueller Gastherme-Zustand (read-only) |
| `Warmwasserbereitung Ergebnis` | text_sensor | Bereit / Aktiv / Zieltemperatur erreicht / Maximale Laufzeit / Maximale Speichertemperatur / Sensorausfall | Letztes WWB-Ergebnis |

Der bisherige `select` (Aus/Heizen/Warmwasser) entfaellt und wird durch den read-only
`text_sensor` Betriebsmodus ersetzt.

### 3.8 Boot-Verhalten

1. `global_wwb_aktiv = false` (nicht persistent)
2. `global_heizung_sollwert` wird aus Flash wiederhergestellt
3. Arbitrierung auswerten → Heizen (falls Sollwert > 0) oder Aus
4. Ergebnis-Sensor = "Bereit"

HA erkennt den ESP-Neustart und sendet erneut den aktuellen Heizkennlinie-Sollwert.


## 4. HA-seitige Aenderungen

### 4.1 heizen_warmwasser.yaml

**Entfaellt:**
- Scripts `keller_zh_warmwasserbereitung_ein` / `_aus`
- `input_number.keller_zh_warmwasser_temperatur_zielwert_intern`
- Template-Sensor + Button `Warmwasser Manuell Bereiten`
- 5 Abschalt-Automationen (Solltemp, Fallback, Laufzeit, Max-Temp, Kreislauf-Trigger)

**Bleibt:**
- Zeitplan-Automationen (t1 Abends, t2 Morgens, Legionellenschutz samstags)
- Schwellwert- und Minimalwert-Automationen
- `input_boolean.keller_zh_warmwasser_automation`
- Alle `input_number` fuer Sollwerte, `input_datetime` fuer Zeitpunkte

**Aenderung:** Zeitplan-Automationen rufen statt Script die neue ESP-API auf:
```yaml
action:
  - action: esphome.keller_zh_regelung_warmwasser_anfordern
    data:
      zieltemperatur: "{{ states('input_number...') | float(0) }}"
```

**Neu:** Benachrichtigungs-Automation auf Ergebnis-Sensor:
```yaml
trigger:
  - trigger: state
    entity_id: sensor.keller_zh_gastherme_warmwasserbereitung_ergebnis
    not_to:
      - "Bereit"
      - "Aktiv"
      - "unavailable"
      - "unknown"
action:
  - choose:  # Warnung bei Sensorausfall/Max-Temp, Info bei Laufzeit/Zieltemp
```

### 4.2 heizen_heizkennlinie.yaml

Automation "Heizung folgt Heizkennlinie" aendern:
- Statt `input_number.set_value` → `esphome.keller_zh_regelung_heizung_anfordern`
- Statt Condition `sensor.keller_zh_gastherme_kreislauf == "Heizen"` → Condition entfernen
  (ESP arbitriert selbst, HA sendet immer den aktuellen Sollwert)
- Bei deaktivierter Heizungs-Automation: `heizung_anfordern(sollwert=0)` senden

Fehlerfall-Automation analog anpassen.

### 4.3 gastherme.yaml

- Template-Sensor `Keller ZH Gastherme Kreislauf` anpassen: Quelle wird neuer Text-Sensor
  statt Select
- Binary-Sensoren `Kreislauf Warmwasser`/`Kreislauf Heizung` anpassen

### 4.4 ug_bad_taster_heizung.yaml

Badezimmer-Taster: `button.press` → `esphome.keller_zh_regelung_warmwasser_anfordern`
mit `zieltemperatur` aus dem manuellen Sollwert.

### 4.5 Entfallende HA-Entities

| Entity | Grund |
|--------|-------|
| `input_number.keller_zh_warmwasser_temperatur_zielwert_intern` | ESP trackt Zieltemperatur |
| `input_number.keller_zh_gastherme_sollwert_temperatur` | ESP speichert Sollwert persistent |
| `sensor.keller_zh_warmwasser_temperatur_zielwert` | ESP hat eigenen Sensor |
| `button.keller_zh_warmwasser_manuell_bereiten` | Direkte API-Action |


## 5. ESP-seitige Aenderungen bestehender Dateien

### 5.1 include/keller_zh_relais.yaml

- Betriebsmodus-Select entfernen (Zeilen 90-113)
- `on_boot` Lambda fuer Select-Wiederherstellung entfernen (Zeilen 115-123)
- Relais `wwb_anforderung` bleibt (wird von Betriebssteuerung gesteuert)
- `on_turn_on/off` Events fuer Flusserkennung und Zaehler bleiben

### 5.2 include/keller_zh_em10_pwm.yaml

- API-Action `gastherme_solltemperatur_set` entfernen
- Bidirektionale Kopplung (Sollwert ↔ Modus) entfernen
- Script `apply_gastherme_sollwert` bleibt, wird vereinfacht: liest `global_heizung_sollwert`
  direkt und wendet ihn an (keine Moduspruefung mehr noetig — die Betriebssteuerung ruft das
  Script nur auf, wenn der Sollwert tatsaechlich angewendet werden soll; im "Aus"-Zustand wird
  stattdessen 0 direkt an den PWM-Ausgang geschrieben)
- `global_gastherme_sollwert_degc` entfaellt, wird durch `global_heizung_sollwert` in der
  Betriebssteuerung ersetzt
- Diagnose-Sensoren (Sollwert °C, Volt, PWM%, Regelabweichung) bleiben

### 5.3 keller-zh-regelung.yaml

Neues Package einbinden:
```yaml
packages:
  # ... bestehende ...
  betriebssteuerung: !include include/keller_zh_gastherme_betriebssteuerung.yaml
```


## 6. Dashboard-Aenderungen (lovelace-sitemap, View "Heizung")

### 6.1 Gastherme-Karte

- `select.keller_zh_gastherme_betriebsmodus` ersetzen durch neuen Text-Sensor
  `sensor.keller_zh_gastherme_betriebsmodus`

### 6.2 Warmwasserbereitung-Karte

- `binary_sensor.keller_zh_gastherme_kreislauf_warmwasser` ggf. durch Text-Sensor ersetzen
- Neuen Sensor `Warmwasserbereitung Ergebnis` hinzufuegen


## 7. Dokumentations-Aenderungen

### 7.1 keller-zh-regelung.md

Sektion 2.1 "Betriebsmodus-Zustandsmaschine" vollstaendig ersetzen durch:
- Neues Anforderungsmodell mit Diagramm
- Architekturentscheidungen mit Begruendungen
- Schnittstellenvertrag ESP ↔ HA (API-Actions, Sensoren, Invarianten)
- WWB-Abbruchbedingungen mit Prioritaeten
- Sensorausfall-Verhalten

Sektion 2.2 "Sollwert-Signalkette" anpassen (neuer Einstiegspunkt via Betriebssteuerung).


## 8. Invarianten (was der ESP garantiert)

1. **Exklusivitaet:** Genau ein Betriebsmodus ist zu jeder Zeit aktiv
2. **WWB-Vorrang:** Solange WWB aktiv, wird nicht geheizt
3. **Autonome Abschaltung:** WWB wird bei Zieltemperatur, Laufzeit, Uebertemperatur oder
   Sensorausfall beendet — unabhaengig von HA
4. **Sicherer Ruhezustand:** Nach Boot ist WWB inaktiv, Betriebsmodus = "Aus" oder "Heizen"
   (abhaengig vom persistierten Sollwert)
5. **Guard bei Anforderung:** WWB wird nicht gestartet wenn Zieltemperatur bereits erreicht
   oder Sensor ausgefallen
6. **Ergebnis-Transparenz:** Jeder WWB-Abbruch wird mit Grund im Text-Sensor publiziert


## 9. Verifikation

### 9.1 ESP-Konfiguration
- `esphome config keller-zh-regelung.yaml` muss ohne Fehler/Warnungen durchlaufen

### 9.2 HA-Konfiguration
- `ha_check_config` muss valid sein
- Alle reloadbaren Komponenten neu laden (scripts, automations, templates, input_numbers)

### 9.3 Funktionstests (nach Deployment)
- Heizung anfordern → Betriebsmodus = "Heizen", Sollwert korrekt
- WWB anfordern → Betriebsmodus = "Warmwasser", Relais EIN
- WWB Zieltemperatur erreicht → automatisch zurueck zu "Heizen"
- WWB waehrend Heizung → unterbricht Heizung, danach Heizung fortgesetzt
- Heizung Sollwert = 0 → Betriebsmodus = "Aus"
- ESP-Neustart → korrekter Zustand wiederhergestellt
- Benachrichtigungen bei allen WWB-Abbruchgruenden pruefen
- Dashboard zeigt korrekte Entities
