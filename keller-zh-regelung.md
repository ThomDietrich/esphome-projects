# Keller ZH Regelung — Strukturelle und funktionale Uebersicht

Zentraler Heizungscontroller fuer die gesamte Heizungsanlage im Keller.
Hardware: AZ-Delivery ESP-32 Dev Kit C V4 auf kundenspezifischer Platine.
Produktiv seit ca. 2 Jahren.


## 1. Systemarchitektur

### 1.1 Hydraulik-Uebersicht

```
                         ┌──────────────────┐
                         │  Sonnenkollektor  │
                         │    (Dach)         │
                         └───┬──────────┬────┘
                        VL ↑ │          │ ↓ RL            PT1000 (GPIO17)
                        kalt │          │ warm            am Kollektor
                             │          │
                    ┌────────┴──────────┴────────┐
                    │  Solarkreis Pumpe           │
                    │  Grundfos Alpha Pro 25-40   │
                    │  (Relais 2, GPIO0)          │
                    └────────┬──────────┬────────┘
                        VL ↓ │          │ ↑ RL
                    DS18B20  │          │  DS18B20
                             │          │
     ┌───────────────────────┼──────────┼────────────────────────────────┐
     │                  SPEICHER / PUFFER                                │
     │                                                                   │
     │  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────────┐  │
     │  │ Puffer grau     │  │ Puffer blau      │  │ Warmwasser-      │  │
     │  │ EPS 600, 600L   │  │ Logalux SF551    │  │ speicher         │  │
     │  │                 │  │ 550L             │  │                  │  │
     │  │  DS18B20 (1x)   │  │  DS18B20 (1x)   │  │  DS18B20 oben   │  │
     │  │                 │  │  Sensorband (11x)│  │  DS18B20 unten  │  │
     │  │                 │  │  GPIO33           │  │  DS18B20 Entn.  │  │
     │  └─────────────────┘  └─────────────────┘  └───┬──────────┬───┘  │
     │                                                 │          │      │
     └─────────────────────────────────────────────────┼──────────┼──────┘
                                                       │          │
                                                       │          │
     ┌──────────────────────────┐              ┌───────┴──────────┴──────┐
     │  MPM DS WOOD 18 kW       │              │  Buderus GB 162-25      │
     │  Holzvergaserkessel       │              │  Gastherme              │
     │                          │              │                         │
     │  PT1000 Abgas (GPIO5)   │              │  0-10V Sollwert (GPIO27)│
     │  DS18B20 VL/RL          │              │  WWB Anforderung (R4)   │
     │  DS18B20 Pufferkreis    │              │  DS18B20 VL/RL          │
     │  VL/RL                  │              │                         │
     └──────┬──────────┬───────┘              └───────┬──────────┬──────┘
        VL ↓ │          │ ↑ RL                   VL ↓ │          │ ↑ RL
     ┌──────┴──────────┴───────┐                      │          │
     │  Kesselpufferkreis      │               ┌──────┴──────────┴──────┐
     │  Pumpe (Relais 1, GPIO4)│               │  Betriebsmodus:        │
     └─────────────────────────┘               │  Heizen → Heizkreis    │
                                               │  WWB → Warmwasser HX   │
                                               └───────┬──────────┬─────┘
                                                       │          │
                    ┌──────────────────────────────────┘          │
                    │                                              │
              ┌─────┴─────────────┐                    ┌──────────┴──────┐
              │  Heizkreis         │                    │  WW Zirkulation  │
              │  DS18B20 VL/RL     │                    │  Pumpe (R3,GPIO2)│
              │  Zenner S0 (GPIO34)│                    │  Zenner S0(GPIO35)│
              └────────────────────┘                    └─────────────────┘

     Stromzaehler: S0 Hutschiene (GPIO39) — nicht angeschlossen
```

### 1.2 Sub-Devices

| Sub-Device ID       | HA-Name                      | Rolle                                   | Sensoren | Schalter | Sonstige |
|---------------------|------------------------------|------------------------------------------|----------|----------|----------|
| `gastherme`         | Keller ZH Gastherme          | Gas-Brennwertkessel, Sollwert + Modus    | 7        | 1 (int.) | 1 Select |
| `kessel`            | Keller ZH Kessel             | Holzvergaser + Pufferkreis               | 9        | 1        | —        |
| `solarthermie`      | Keller ZH Solarthermie       | Solarkollektoren + Energieberechnung     | 14       | 1        | 1 Binary |
| `pufferspeicher`    | Keller ZH Pufferspeicher     | 2 Puffer + Sensorband + Ladegrad        | 16       | —        | —        |
| `warmwasserspeicher`| Keller ZH Warmwasserspeicher | WW-Speicher + Zaehler + Schutz          | 9        | 1        | 2 Binary |
| `heizkreis`         | Keller ZH Heizkreis          | Heizkreisverteilung + Zaehler           | 7        | —        | 1 Binary |
| `stromzaehler`      | Keller ZH Stromverbrauch     | Elektrischer Verbrauch (defekt)          | 2        | —        | —        |
| *(Hauptgeraet)*     | Keller ZH Regelung           | Gehaeuse, LEDs, Boilerplate             | 2+       | —        | 1 Light  |

### 1.3 Dateiabhaengigkeitsbaum

```
keller-zh-regelung.yaml
├── common/wifi.yaml
│   └── common/secrets.yaml (transitiv)
├── common/boilerplate.yaml
├── include/keller_zh_i2c_enclosure.yaml
├── include/keller_zh_relais.yaml
├── include/keller_zh_temp_pt1000_spi.yaml
├── include/keller_zh_temp_ds18b20_gpio32.yaml
│   └── include/ds18b20_template.yaml (×15)
├── include/keller_zh_temp_ds18b20_gpio33.yaml
│   └── include/ds18b20_template.yaml (×11)
├── include/keller_zh_em10_pwm.yaml
├── include/keller_zh_temp_diff.yaml
├── include/keller_zh_energy_heat_counter.yaml        ← verschachtelte packages:
│   └── include/water_meter_heat_counter_template.yaml (×2, Heizkreis + WWB)
├── include/keller_zh_flusserkennung.yaml
├── include/keller_zh_energy_solar_production.yaml
└── include/s0_energy_meter_template.yaml (×1, Stromzaehler)
```

**15 Dateien, 13 direkte Packages, 29 Template-Instanzen** (26× DS18B20, 2× Waermezaehler, 1× S0)

### 1.4 GPIO-Belegung

| GPIO   | Funktion                      | Richtung | Strapping | Datei                        |
|--------|-------------------------------|----------|-----------|-------------------------------|
| GPIO0  | Relais 2 (Solarkreis Pumpe)   | Output   | Ja        | keller_zh_relais.yaml         |
| GPIO2  | Relais 3 (WW Zirkulation)     | Output   | Ja        | keller_zh_relais.yaml         |
| GPIO4  | Relais 1 (Pufferkreis Pumpe)  | Output   | —         | keller_zh_relais.yaml         |
| GPIO5  | SPI CS0 (MAX31865 Kessel)     | Output   | Ja        | keller_zh_temp_pt1000_spi.yaml|
| GPIO12 | SPI MISO                      | Input    | Ja        | keller_zh_temp_pt1000_spi.yaml|
| GPIO13 | SPI MOSI                      | Output   | —         | keller_zh_temp_pt1000_spi.yaml|
| GPIO14 | SPI CLK                       | Output   | —         | keller_zh_temp_pt1000_spi.yaml|
| GPIO15 | Relais 4 (WWB Anforderung)    | Output   | Ja        | keller_zh_relais.yaml         |
| GPIO17 | SPI CS1 (MAX31865 Solar)      | Output   | —         | keller_zh_temp_pt1000_spi.yaml|
| GPIO19 | Status-LED rot                | Output   | —         | keller-zh-regelung.yaml       |
| GPIO21 | I2C SCL (vertauscht!)         | Bidir    | —         | keller_zh_i2c_enclosure.yaml  |
| GPIO22 | I2C SDA (vertauscht!)         | Bidir    | —         | keller_zh_i2c_enclosure.yaml  |
| GPIO23 | Status-LED gruen              | Output   | —         | keller-zh-regelung.yaml       |
| GPIO27 | PWM (EM10 0-10V)              | Output   | —         | keller_zh_em10_pwm.yaml       |
| GPIO32 | 1-Wire Bus 1 (15× DS18B20)   | Bidir    | —         | keller_zh_temp_ds18b20_gpio32 |
| GPIO33 | 1-Wire Bus 2 (11× Sensorband) | Bidir   | —         | keller_zh_temp_ds18b20_gpio33 |
| GPIO34 | S0 Heizkreis Zenner           | Input    | —         | water_meter_heat_counter_tmpl |
| GPIO35 | S0 Warmwasser Zenner          | Input    | —         | water_meter_heat_counter_tmpl |
| GPIO39 | S0 Stromzaehler               | Input    | —         | s0_energy_meter_template      |

**Frei:** GPIO16, GPIO18, GPIO25, GPIO26, GPIO36


## 2. Datenfluss und Steuerlogik

### 2.1 Betriebsmodus-Zustandsmaschine

```
                        ┌──────────────┐
                        │     Aus      │
                        │ Relais 4: AUS│
                        │ Sollwert: 0°C│
                        └──┬───────┬───┘
              Sollwert>0°C │       │ Sollwert=0°C
              (auto "Heizen")     (auto "Aus")
                           │       │
                 ┌─────────┴───┐   │
                 │   Heizen    │◄──┘
                 │ Relais 4:AUS│
                 │ Sollwert:   │
                 │ gespeichert │
                 └──────┬──────┘
                        │ Manuell via Select
                        ▼
                 ┌─────────────┐
                 │ Warmwasser  │
                 │ Relais 4:EIN│
                 │ Sollwert:   │   Gastherme ignoriert 0-10V
                 │ gespeichert │   im WWB-Modus, regelt intern
                 └─────────────┘
```

**Bidirektionale Kopplung** (em10_pwm.yaml ↔ relais.yaml):
- API setzt Sollwert → 0°C: Modus wechselt automatisch zu "Aus"
- API setzt Sollwert > 0°C bei Modus "Aus": Modus wechselt zu "Heizen"
- Moduswechsel → Script `apply_gastherme_sollwert` setzt PWM-Ausgang

**Boot-Reihenfolge:**
1. Prioritaet 600 (relais.yaml): Relais-Zustand aus gespeichertem Modus wiederherstellen
2. Prioritaet 500 (em10_pwm.yaml): PWM-Sollwert anwenden

### 2.2 Sollwert-Signalkette

```
global_gastherme_sollwert_degc (persistent, default 30°C)
        │
        ▼
apply_gastherme_sollwert (Script)
        │
        ├── Modus "Aus" → degc = 0°C
        ├── Sonst → degc = gespeicherter Wert
        │
        ▼  Lineare Regression (kalibriert):
   T [°C] ──→ V = T × 0.1253 − 1.2751 [V] ──→ PWM = (V − 0.4535) × 10.05 [%]
        │
        ├── → gastherme_sollwert_degc_sensor
        ├── → gastherme_sollwert_volt_sensor
        ├── → gastherme_sollwert_pwm_sensor
        └── → heiz_sollwert_pwm_output (GPIO27, LEDC 1220 Hz)
```

### 2.3 Temperatur → Spreizung → Energie (Berechnungsketten)

**Heizkreis-Kette:**
```
heizkreis_vl ──┐
               ├──→ heizkreis_diff (VL−RL) [K]
heizkreis_rl ──┘           │
                           ├──→ Waermezaehler Energie (Impuls × ΔT × cp)
                           └──→ Waermezaehler Leistung (Durchfluss × ΔT)
                                       ↑
               intern_heizkreis_waerme_tick (GPIO34, Zenner 0,25 L/Impuls)
                           │
                           └──→ Waermezaehler Volumen, Durchfluss
```

**Warmwasser-Kette:** (identisch, mit warmwasser_diff und GPIO35)
```
gastherme_vl ──┐
               ├──→ warmwasser_diff (VL−RL) [K]     (device_id: gastherme!)
gastherme_rl ──┘           │
                           ├──→ WWB Waermezaehler Energie
                           └──→ WWB Waermezaehler Leistung
                                       ↑
                      intern_wwb_waerme_tick (GPIO35)
```

**Solarthermie-Kette:**
```
solarkreis_rl ──┐
                ├──→ solarkreis_diff_rlvl (RL−VL) [K]
solarkreis_vl ──┘           │
                             ├──→ on_value Lambda (nur bei Pumpe EIN):
                             │    delta_t × Volumenstrom × Dichte × cp × ΔT
                             │    → global_solarkreis_energie_kwh (kumulativ)
                             │    → sensor_solarkreis_energie_kwh
                             │
                             └──→ Thermische Leistung (30s update, Pumpe EIN)
                                    V̇ × ρ × cp × ΔT = kW

Stoffwerte (LUT aus TYFOCOR-L Datenblatt):
  solarkreis_rl → calibrate_linear → Dichte [kg/m³], cp [kJ/(kg·K)]
  solarkreis_vl → calibrate_linear → Viskositaet [mm²/s] (diagnostisch)
  Volumenstrom: festcodiert 2,0 L/min (TODO: temperaturabhaengig)
```

**Kessel:**
```
kessel_vl ──┐                 kessel_abgas (PT1000, 3s)
            ├→ kessel_diff        │
kessel_rl ──┘                     └→ on_value: Gradient [°C/min]
                                     (Δ(T)/Δ(t) via millis())
kesselpufferkreis_vl ──┐
                       ├→ kesselpufferkreis_diff
kesselpufferkreis_rl ──┘
```

### 2.4 Flusserkennung (3-stufig)

```
Zenner S0 Impuls (GPIO34/35)
        │
        ▼
Stufe 1: intern_*_waerme_tick          (aus Template, intern, 100ms Entprellung)
        │  on_press / on_release
        ▼
Stufe 2: intern_*_aktiv              (Template Binary Sensor, intern)
        │  delayed_off: 60s           → Totmannschalter
        ▼
Stufe 3: sensor_*_aktiv              (Veroeffentlicht, lambda pollt Stufe 2)
        │
        └── wwb_anforderung on_turn_on/off → sofortiges OFF im jeweils anderen Kreis
```

### 2.5 Puffer-Ladegrad (Sensorband)

```
puffer_blau_lg_sensor_01 (unten) ──┐
puffer_blau_lg_sensor_02           │
...                                ├──→ Trigger nur von 01 und 11
puffer_blau_lg_sensor_10           │    (vermeidet 9 redundante Berechnungen)
puffer_blau_lg_sensor_11 (oben) ──┘
        │
        ▼
Ladegrad [%] = Mittelwert aller 11 Sensoren (normiert 30°C=0%, max(oben,60°C)=100%)
Durchschnittstemperatur [°C] = arithmetisches Mittel
Schichtungsguete [K] = oben − unten
```

### 2.6 Cross-File !extend-Referenzen

Mehrere Entities werden in anderen Dateien per `!extend` erweitert:

| Entity-ID                | Definiert in          | Erweitert in                     | Erweiterung                    |
|--------------------------|-----------------------|----------------------------------|-------------------------------|
| `wwb_anforderung`        | relais.yaml           | energy_heat_counter.yaml         | on_turn_on/off: Zaehler-Update |
|                          |                       | flusserkennung.yaml              | on_turn_on/off: Flow-Reset     |
| `warmwasserspeicher_unten` | ds18b20_gpio32.yaml | temp_diff.yaml                   | on_value: Schichtung-Update    |
| `warmwasserspeicher_oben`  | ds18b20_gpio32.yaml | temp_diff.yaml                   | on_value: Schichtung-Update    |
| `gastherme_vl`           | ds18b20_gpio32.yaml   | em10_pwm.yaml                    | on_value: Regelabweichung      |
| `solarkreis_diff_rlvl`  | temp_diff.yaml        | energy_solar_production.yaml     | on_value: Energieberechnung    |
| `intern_heizkreis_waerme_tick` | heat_counter_tmpl | flusserkennung.yaml              | on_press/release: Flow-Detect  |
| `intern_wwb_waerme_tick`   | heat_counter_tmpl     | flusserkennung.yaml              | on_press/release: Flow-Detect  |

`wwb_anforderung` wird in **3 Dateien** gesteuert — die verteilte Logik ist schwer nachzuvollziehen.


## 3. Entity-Uebersicht

<details>
<summary><strong>Gastherme</strong> (7 Sensoren, 1 Schalter intern, 1 Select)</summary>

| Name                  | ID                                | Typ    | Quelldatei              | Abhaengigkeiten               |
|-----------------------|-----------------------------------|--------|-------------------------|-------------------------------|
| Vorlauftemperatur     | `gastherme_vl`                    | sensor | ds18b20_gpio32          | → warmwasser_diff, Regelabw.  |
| Ruecklauftemperatur   | `gastherme_rl_wwb`                | sensor | ds18b20_gpio32          | → warmwasser_diff             |
| Sollwert Temperatur   | `gastherme_sollwert_degc_sensor`  | sensor | em10_pwm                | ← apply_gastherme_sollwert    |
| Sollwert Spannung     | `gastherme_sollwert_volt_sensor`  | sensor | em10_pwm                | ← apply_gastherme_sollwert    |
| Sollwert PWM          | `gastherme_sollwert_pwm_sensor`   | sensor | em10_pwm                | ← apply_gastherme_sollwert    |
| Regelabweichung       | `gastherme_regelabweichung`       | sensor | em10_pwm                | ← gastherme_vl, Sollwert      |
| Spreizung             | `warmwasser_diff`                 | sensor | temp_diff               | ← gastherme_vl, rl_wwb        |
| WWB Anforderung       | `wwb_anforderung`                 | switch | relais (internal)       | ← Betriebsmodus               |
| Betriebsmodus         | `gastherme_betriebsmodus`         | select | relais                  | → wwb_anforderung, Sollwert   |

</details>

<details>
<summary><strong>Kessel</strong> (9 Sensoren, 1 Schalter)</summary>

| Name                            | ID                              | Typ    | Quelldatei        | Abhaengigkeiten              |
|---------------------------------|---------------------------------|--------|-------------------|------------------------------|
| Abgastemperatur                 | `kessel_abgas`                  | sensor | pt1000_spi        | → Gradient                   |
| Abgas Gradient                  | `kessel_abgas_gradient`         | sensor | pt1000_spi        | ← kessel_abgas on_value      |
| Vorlauftemperatur               | `kessel_vl`                     | sensor | ds18b20_gpio32    | → kessel_diff                |
| Ruecklauftemperatur             | `kessel_rl`                     | sensor | ds18b20_gpio32    | → kessel_diff                |
| Pufferkreis Vorlauftemperatur   | `kesselpufferkreis_vl`          | sensor | ds18b20_gpio32    | → kesselpufferkreis_diff     |
| Pufferkreis Ruecklauftemperatur | `kesselpufferkreis_rl`          | sensor | ds18b20_gpio32    | → kesselpufferkreis_diff     |
| Spreizung                       | `kessel_diff`                   | sensor | temp_diff         | ← kessel_vl, rl              |
| Pufferkreis Spreizung           | `kesselpufferkreis_diff`        | sensor | temp_diff         | ← Pufferkreis VL/RL          |
| Pufferkreis Umwaelzpumpe Betr.  | *(duty_time)*                   | sensor | relais            | ← switch_kesselpufferkreis   |
| Pufferkreis Umwaelzpumpe        | `switch_kesselpufferkreis_pumpe`| switch | relais            | —                            |

</details>

<details>
<summary><strong>Solarthermie</strong> (14 Sensoren, 1 Schalter, 1 Binary Sensor)</summary>

| Name                          | ID                                          | Typ     | Quelldatei                | Abhaengigkeiten             |
|-------------------------------|---------------------------------------------|---------|---------------------------|-----------------------------|
| Kollektortemperatur           | `sonnenkollektor`                           | sensor  | pt1000_spi                | → solarkreis_diff_ou        |
| Vorlauftemperatur             | `solarkreis_vl`                             | sensor  | ds18b20_gpio32            | → solarkreis_diff_rlvl      |
| Ruecklauftemperatur           | `solarkreis_rl`                             | sensor  | ds18b20_gpio32            | → solarkreis_diff_rlvl      |
| Spreizung                     | `solarkreis_diff_rlvl`                      | sensor  | temp_diff                 | → Energieberechnung         |
| Einschaltdifferenz            | `solarkreis_diff_ou`                        | sensor  | temp_diff                 | ← Kollektor, WW unten       |
| Volumenstrom                  | `sensor_solarkreis_flow_rate`               | sensor  | energy_solar_production   | Festcodiert 2,0 L/min       |
| Waermetraeger Dichte          | `sensor_solarkreis_solarfluid_density_*`    | sensor  | energy_solar_production   | ← solarkreis_rl (LUT)       |
| Waermetraeger Waermekapazitaet| `sensor_solarkreis_solarfluid_capacity_*`   | sensor  | energy_solar_production   | ← solarkreis_rl (LUT)       |
| Waermetraeger Viskositaet     | `sensor_solarkreis_solarfluid_viscosity_*`  | sensor  | energy_solar_production   | ← solarkreis_vl (LUT)       |
| Messintervall                 | `sensor_solarkreis_delta_t_seconds`         | sensor  | energy_solar_production   | Diagnostisch                |
| Masseninkrement               | `sensor_solarkreis_m_g`                     | sensor  | energy_solar_production   | Diagnostisch                |
| Waermeertrag                  | `sensor_solarkreis_energie_kwh`             | sensor  | energy_solar_production   | ← kumulativ (global)        |
| Thermische Leistung           | `sensor_solarkreis_leistung`                | sensor  | energy_solar_production   | ← Durchfluss, ρ, cp, ΔT     |
| Umwaelzpumpe Betriebsstunden  | *(duty_time)*                               | sensor  | relais                    | ← switch_solarkreis_pumpe   |
| Umwaelzpumpe                  | `switch_solarkreis_pumpe`                   | switch  | relais                    | —                           |
| Kollektorstillstand           | `solar_kollektorstillstand`                 | binary  | energy_solar_production   | ← Kollektor>120°C, Pumpe AUS|

</details>

<details>
<summary><strong>Pufferspeicher</strong> (16 Sensoren)</summary>

| Name                      | ID                           | Typ    | Quelldatei        | Abhaengigkeiten          |
|---------------------------|------------------------------|--------|--------------------|--------------------------|
| Blau Temperatur           | *(kein ID)*                  | sensor | ds18b20_gpio32     | —                        |
| Grau Temperatur           | *(kein ID)*                  | sensor | ds18b20_gpio32     | —                        |
| Blau Sensorband 01–11    | `puffer_blau_lg_sensor_01–11`| sensor | ds18b20_gpio33     | 01+11 → Ladegrad etc.   |
| Blau Ladegrad             | `puffer_blau_ladegrad`       | sensor | ds18b20_gpio33     | ← alle 11 Sensoren      |
| Blau Durchschnittstemperatur | `puffer_blau_durchschnitt` | sensor | ds18b20_gpio33     | ← alle 11 Sensoren      |
| Blau Schichtungsguete     | `puffer_blau_schichtung`     | sensor | ds18b20_gpio33     | ← Sensor 01 + 11        |

</details>

<details>
<summary><strong>Warmwasserspeicher</strong> (9 Sensoren, 1 Schalter, 2 Binary Sensoren)</summary>

| Name                          | ID                           | Typ     | Quelldatei              | Abhaengigkeiten                |
|-------------------------------|------------------------------|---------|-------------------------|-------------------------------|
| Temperatur unten              | `warmwasserspeicher_unten`   | sensor  | ds18b20_gpio32          | → solarkreis_diff_ou, Schicht.|
| Temperatur oben               | `warmwasserspeicher_oben`    | sensor  | ds18b20_gpio32          | → Schichtung, Legionellen     |
| Entnahmetemperatur            | *(kein ID)*                  | sensor  | ds18b20_gpio32          | —                             |
| Schichtung                    | `warmwasser_schichtung`      | sensor  | temp_diff               | ← oben, unten                 |
| Zirkulationspumpe Betr.       | *(duty_time)*                | sensor  | relais                  | ← switch_warmwasser_zirk.     |
| Waermezaehler Volumen         | `sensor_wwb_waerme_volumen`          | sensor  | heat_counter_template   | ← GPIO35 Impulse              |
| Waermezaehler Energie         | `sensor_wwb_waerme_energie`         | sensor  | heat_counter_template   | ← Impulse × warmwasser_diff   |
| Waermezaehler Durchfluss      | `sensor_wwb_waerme_durchfluss`      | sensor  | heat_counter_template   | ← Volumen/Zeit                |
| Waermezaehler Leistung        | `sensor_wwb_waerme_leistung`        | sensor  | heat_counter_template   | ← Durchfluss × warmwasser_diff|
| Zirkulationspumpe             | `switch_warmwasser_zirkulation`| switch | relais                  | —                             |
| Legionellenschutz             | `warmwasser_legionellenschutz`| binary | temp_diff               | ← oben <60°C fuer 7 Tage     |
| Flusserkennung                | `sensor_wwb_aktiv`           | binary  | flusserkennung          | ← GPIO35, wwb_anforderung     |

</details>

<details>
<summary><strong>Heizkreis</strong> (7 Sensoren, 1 Binary Sensor)</summary>

| Name                     | ID                             | Typ     | Quelldatei            | Abhaengigkeiten              |
|--------------------------|--------------------------------|---------|------------------------|------------------------------|
| Vorlauftemperatur        | `heizkreis_vl`                 | sensor  | ds18b20_gpio32         | → heizkreis_diff             |
| Ruecklauftemperatur      | `heizkreis_rl`                 | sensor  | ds18b20_gpio32         | → heizkreis_diff             |
| Spreizung                | `heizkreis_diff`               | sensor  | temp_diff              | → Waermezaehler Energie/Lstg.|
| Waermezaehler Volumen    | `sensor_heizkreis_waerme_volumen`      | sensor  | heat_counter_template  | ← GPIO34 Impulse             |
| Waermezaehler Energie    | `sensor_heizkreis_waerme_energie`     | sensor  | heat_counter_template  | ← Impulse × heizkreis_diff   |
| Waermezaehler Durchfluss | `sensor_heizkreis_waerme_durchfluss`  | sensor  | heat_counter_template  | ← Volumen/Zeit               |
| Waermezaehler Leistung   | `sensor_heizkreis_waerme_leistung`    | sensor  | heat_counter_template  | ← Durchfluss × heizkreis_diff|
| Flusserkennung           | `sensor_heizkreis_aktiv`       | binary  | flusserkennung         | ← GPIO34, wwb_anforderung    |

</details>

<details>
<summary><strong>Stromzaehler</strong> (2 Sensoren — defekt)</summary>

| Name                  | ID                                | Typ    | Quelldatei               | Abhaengigkeiten |
|-----------------------|-----------------------------------|--------|--------------------------|-----------------|
| Elektrische Leistung  | `sensor_heizung_strom_leistung`      | sensor | s0_energy_meter_template | GPIO39 (defekt) |
| Elektrische Energie   | `sensor_heizung_strom_energie`     | sensor | s0_energy_meter_template | GPIO39 (defekt) |

</details>

### Globals

| ID                                  | Typ      | Restore | Datei                    | Zweck                                   |
|-------------------------------------|----------|---------|--------------------------|------------------------------------------|
| `global_gastherme_sollwert_degc`    | float    | Ja      | em10_pwm                 | Heiz-Sollwert, persistent (default 30°C) |
| `global_abgas_prev_temp`            | float    | Nein    | pt1000_spi               | Gradient: vorheriger Messwert            |
| `global_abgas_prev_time_ms`         | uint32_t | Nein    | pt1000_spi               | Gradient: vorheriger Zeitstempel         |
| `global_dt_last_increment`          | long int | Nein    | energy_solar_production  | Solar: letzter Berechnungszeitpunkt      |
| `global_solarkreis_energie_kwh`     | float    | Nein    | energy_solar_production  | Solar: kumulative Energie                |
| `global_heizkreis_waerme_volumen_l` | float    | Nein    | heat_counter_template    | Heizkreis: kumul. Volumen                |
| `global_heizkreis_waerme_energie_kwh`| float   | Nein    | heat_counter_template    | Heizkreis: kumul. Energie                |
| `global_heizkreis_waerme_prev_vol_l`| float    | Nein    | heat_counter_template    | Heizkreis: Durchflussberechnung          |
| `global_heizkreis_waerme_prev_time_ms`| uint32_t| Nein   | heat_counter_template    | Heizkreis: Durchflussberechnung          |
| `global_wwb_waerme_volumen_l`       | float    | Nein    | heat_counter_template    | WWB: kumul. Volumen                      |
| `global_wwb_waerme_energie_kwh`     | float    | Nein    | heat_counter_template    | WWB: kumul. Energie                      |
| `global_wwb_waerme_prev_vol_l`      | float    | Nein    | heat_counter_template    | WWB: Durchflussberechnung                |
| `global_wwb_waerme_prev_time_ms`    | uint32_t | Nein    | heat_counter_template    | WWB: Durchflussberechnung                |

Alle Globals ohne `restore_value` setzen beim Neustart auf 0 zurueck. Fuer `total_increasing`-Sensoren ist das korrekt — Home Assistant akkumuliert.


## 4. Sicherheit und Verriegelungen

### Thermische Sicherheit

| Mechanismus           | Schwellwert      | Verzoegerung | Datei                    | Anmerkung                                    |
|-----------------------|------------------|-------------|--------------------------|----------------------------------------------|
| Legionellenschutz     | oben < 60°C      | 7 Tage      | temp_diff                | Setzt bei ESP-Neustart zurueck               |
| Kollektorstillstand   | Kollektor > 120°C + Pumpe AUS | Keine | energy_solar_production | Problem-Klasse, keine Entprellung          |
| Abgas-Gradient        | Informativ       | —           | pt1000_spi               | Steigerung = Feuer, Abfall = Ausbrand        |

### Relais-Sicherheit

- **Strapping-Pins** (GPIO0, 2, 5, 12, 15): `inverted: true` + `ALWAYS_OFF` = physisch HIGH beim Boot (Relais AUS)
- **Boot-Synchronisation** (Prioritaet 600): Relais-Zustand wird aus gespeichertem Betriebsmodus wiederhergestellt
- **Pumpen starten immer AUS** (`restore_mode: ALWAYS_OFF`)

### Messabsicherung

- **DS18B20**: `filter_out: 85` (Reset-Wert), Delta 0.3°C, Heartbeat 30min
- **PT1000**: `filter_out: NaN`, Delta 0.3°C, Heartbeat 30min
- **PT1000 Kollektor**: +2°C Offset fuer ~20m Kabellaenge (2-Leiter)
- **S0-Impulse**: 100ms Entprellung (`delayed_on` + `delayed_off`)


## 5. Beobachtungen und Verbesserungspotenzial

### 5.1 Architektur / Wartbarkeit

**B01 — Verteilte Trigger-Logik durch mehrfaches `!extend`.**
`wwb_anforderung` wird in 3 Dateien erweitert (relais, energy_heat_counter, flusserkennung). Die on_turn_on/off-Aktionen sind ueber das Projekt verstreut und nur durch Suche in allen Dateien nachvollziehbar. Gleiches gilt fuer `warmwasserspeicher_unten`, `warmwasserspeicher_oben`, `gastherme_vl`, `solarkreis_diff_rlvl` und die Template-IDs `intern_*_waerme_tick`.

**B02 — Verschachteltes `packages:` in keller_zh_energy_heat_counter.yaml.**
Diese Datei ist selbst ein Package und enthaelt wiederum `packages:` mit zwei Template-Instanzen. Das funktioniert, ist aber ein tieferer Abstraktionslevel als alle anderen Includes und koennte bei kuenftigen ESPHome-Versionen fragil sein.

**B03 — Kein einheitliches Namensschema fuer interne IDs.**
Manche IDs verwenden Praefix (`sensor_solarkreis_*`, `sensor_heizkreis_*`), andere nicht (`kessel_abgas`, `solarkreis_vl`). Es gibt kein erkennbares System, wann ein Praefix verwendet wird.

### 5.2 Namenskonventionen

**B04 — `warmwasser_diff` unter `device_id: gastherme`.**
Diese Spreizung misst physisch am Gastherme-Anschluss (VL−RL), wird aber als `heat_counter_temp_diff` fuer den Warmwasserspeicher-Waermezaehler verwendet. Die Zuordnung zur Gastherme ist physisch korrekt, aber semantisch mehrdeutig: In HA erscheint sie als "Keller ZH Gastherme Spreizung" — nicht offensichtlich als WWB-Spreizung erkennbar.

**B05 — S0-Template-Entitynamen weichen vom Projekt-Stil ab.**
`s0_energy_meter_template.yaml` erzeugt "Elektrische Leistung" und "Elektrische Energie" — das sind allgemeine Bezeichnungen, waehrend der Rest des Projekts spezifischere Compound-Nomen nutzt (z.B. "Waermezaehler Energie").

**B06 — Entity "Warmwasserbereitung Anforderung" hat einen Namen, ist aber `internal: true`.**
Der Name wird nie in HA angezeigt. Kosmetisch, aber verwirrend beim Lesen.

### 5.3 Fehlende Sensorabdeckung / Luecken

**B07 — Pufferspeicher grau hat nur 1 Sensor, kein Sensorband und keinen Ladegrad.**
Der blaue Puffer hat 11 Sensoren + berechneten Ladegrad, der graue nur einen einzelnen DS18B20 ohne ID. Die Temperaturschichtung und der Ladezustand des grauen Puffers sind unbekannt.

**B08 — Puffer blau und grau: Sensorposition (oben/mitte/unten) undokumentiert.**
Die GPIO32-Einzelsensoren fuer beide Puffer haben keine Positionsangabe. Kommentar im Code: `TODO: Position der Einzelsensoren (oben/mitte/unten)?`

**B09 — Kein Temperatursensor am Warmwasserspeicher-Ruecklauf (Solarseite).**
Solarkreis-Einschaltdifferenz nutzt `warmwasserspeicher_unten`. Falls der Solarkreis direkt in den Puffer einspeist (statt in den WWS), waere der Bezugspunkt falsch. Die Hydraulik-Verschaltung zwischen Puffern und Solarthermie ist undokumentiert (TODO im Hauptfile).

**B10 — Stromzaehler (GPIO39) nie in Betrieb genommen.**
Bekanntes Problem. Entity-Definitionen sind vorhanden, Hardware ist nicht angeschlossen. Der `pulse_meter`-Sensor meldet vermutlich 0W (timeout) oder unregelmaessige Werte durch Rauschen auf dem offenen Pin.

### 5.4 Robustheit / Randfaelle

**B11 — Kollektorstillstand-Sensor hat keine Entprellung.**
`solar_kollektorstillstand` triggert sofort bei >120°C ohne `delayed_on`. Wenn der PT1000-Messwert um 120°C oszilliert (z.B. durch Messrauschen), flackert der Alarm. Zum Vergleich: Der Legionellenschutz hat `delayed_on: 7d`.

**B12 — Flusserkennung: `binary_sensor.template.publish` wird durch naechste Lambda-Auswertung ueberschrieben.**
`sensor_heizkreis_aktiv` und `sensor_wwb_aktiv` verwenden sowohl `lambda:` (Polling) als auch `binary_sensor.template.publish` (erzwungenes OFF bei Moduswechsel). Das veroeffentlichte OFF wird beim naechsten update_interval durch die Lambda-Auswertung ueberschrieben, falls `intern_*_aktiv` noch ON ist. Der sofortige Reset ist daher nur fuer maximal einen Update-Zyklus wirksam.

**B13 — Legionellenschutz setzt bei jedem ESP-Neustart zurueck.**
Der 7-Tage-Timer startet nach jedem Reboot neu. Bei regelmaessigen OTA-Updates oder instabiler Verbindung koennte der Alarm nie ausloesen, obwohl die Temperatur dauerhaft unter 60°C liegt. (Dokumentiert als akzeptable Einschraenkung.)

**B14 — Solarenergie-Berechnung: `global_solarkreis_energie_kwh` nicht persistent.**
`restore_value: no` — nach Neustart beginnt die Zaehlung bei 0. Da `total_increasing` von HA korrekt verarbeitet wird, ist das funktional kein Problem. Aber ein Neustart waehrend Solarproduktion erzeugt einen kurzen Datenluecke (verlorene Energie zwischen letztem HA-Poll und Neustart).

### 5.5 Hardcodierte Werte

**B15 — Solar-Volumenstrom festcodiert auf 2,0 L/min.**
TODO im Code fuer temperaturabhaengige Korrektur ueber TacoSetter-Korrekturkurven. Der tatsaechliche Volumenstrom haengt von der Viskositaet ab (Viskositaets-LUT ist bereits implementiert, wird aber nicht fuer die Korrektur genutzt).

**B16 — Wasser-Stoffwerte im Heat-Counter-Template festcodiert.**
`m_g = 248` (0,25L × 990 g/L) und `c_wh_kgk = 1.1625` sind fuer Wasser bei ~35°C korrekt. Bei hoeheren Vorlauftemperaturen (z.B. 60°C bei WWB) weicht die Dichte ab (983 g/L statt 990 → ~0,7% Fehler). Im Vergleich: Die Solarthermie-Berechnung nutzt temperaturabhaengige Stoffwerte.

**B17 — Ladegrad-Schwellen (30°C Untergrenze, 60°C Minimum fuer Obergrenze) festcodiert im Lambda.**
Diese Werte sind direkt im C++-Code und nicht als Substitutionsvariablen extrahiert.

### 5.6 Offene TODOs im Code

| Ort                        | TODO                                                              |
|----------------------------|-------------------------------------------------------------------|
| keller-zh-regelung.yaml:12 | Hydraulische Verschaltung der Pufferspeicher dokumentieren        |
| keller-zh-regelung.yaml:14 | Modell und Volumen des Warmwasserspeichers                        |
| keller-zh-regelung.yaml:16 | Rolle des ESBE LTC300 — ersetzt durch ESP oder noch aktiv?        |
| keller-zh-regelung.yaml:17 | Kollektortyp/-modell und Anzahl                                   |
| ds18b20_gpio32.yaml:25     | Position der Puffer-Einzelsensoren (oben/mitte/unten)             |
| energy_solar_production:5  | Kollektortyp/-modell, Anzahl und Gesamtflaeche                    |
| energy_solar_production:54 | Volumenstrom temperaturabhaengig korrigieren                      |
