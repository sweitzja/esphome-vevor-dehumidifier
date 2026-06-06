# ESPHome VEVOR Dehumidifier

An ESPHome external component that interfaces with VEVOR's OL-series commercial
dehumidifiers (OL60D / OL80 / OL100 / 125-pint family) by tapping the display
UART between the panel and the mainboard. Decodes the Modbus RTU traffic into
Home Assistant sensors, binary_sensors, and text_sensors — turning a "dumb"
appliance into a fully-monitored smart device without modifying its firmware
or compromising any of its built-in safety logic.

## Status

**Phase 1 (current): read-only.** Listens to the mainboard→panel response wire,
decodes every Modbus 0x03 response, exposes everything the panel sees to Home
Assistant. Verified working end-to-end on an ESP32-C3 board against a live
125-pint VEVOR dehumidifier.

**Phase 2 (next board rev): writes.** Adds Modbus master writes for power /
setpoint / mode / timer, with proper bus arbitration using a 74HCT1G125 buffer
with software-controlled OE#. Hardware is planned; firmware skeleton in place.

## Quick start

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/jjsweitzer/esphome-vevor-dehumidifier
      ref: main

uart:
  - id: bus_uart
    rx_pin: GPIO20        # connected to mainboard-TX wire via 10k/20k divider
    tx_pin: GPIO21        # connected to 74HCT1G125 input A (rev 2+)
    baud_rate: 9600
    rx_buffer_size: 256

vevor_dehumidifier:
  id: dehumidifier
  uart_id: bus_uart

sensor:
  - platform: vevor_dehumidifier
    current_humidity:
      name: "Current Humidity"
    target_humidity:
      name: "Target Humidity"
    ambient_temperature:
      name: "Ambient Temperature"
    coil_temperature:
      name: "Coil Temperature"

binary_sensor:
  - platform: vevor_dehumidifier
    compressor_running:
      name: "Compressor Running"
    flood:
      name: "Flood"

text_sensor:
  - platform: vevor_dehumidifier
    mode:
      name: "Mode"
    status:
      name: "Status"
    error_code:
      name: "Error Code"
```

See [`dehumidifier-c3.yaml`](dehumidifier-c3.yaml) for the full reference
configuration with every available entity wired up.

---

## Hardware

### Architecture

The board sits **inline** between the panel and the mainboard. Both sides of
the bus connect through the board, and the ESP taps and (optionally) drives
the data wires.

```
                   ┌───────────────────────────────┐
                   │  ESP32 inline board           │
                   │                                │
   PANEL ←──5V──→  │   ┌── divider (mainboard-TX)  │  ←──5V──→ MAINBOARD
   PANEL ←──GND─→  │   │      ↓                     │  ←──GND─→ MAINBOARD
                   │   │   ESP RX (GPIO20)          │
   PANEL ←──TX───→ │ ──┤                            │  ←──RX──→ MAINBOARD
                   │   │                            │
   PANEL ←──RX───→ │ ──┤  ┌─[74HCT1G125 buffer]─┐  │  ←──TX──→ MAINBOARD
                   │      │      ↑              │  │
                   │      │   ESP TX (GPIO21)   │  │
                   │      └─── OE# control ─────┘  │
                   │                                │
                   └───────────────────────────────┘
```

The bus is **5V TTL UART**. Two data wires (one per direction) plus 5V and
GND. The ESP runs at 3.3V — hence the level-shifting on both directions.

### Board revisions

**Rev 1 (read-only, current):**

- ESP32-C3-WROOM-02-N4 module powered from a 3.3V LDO off the panel's 5V rail
- **10kΩ + 20kΩ divider** on the mainboard-TX wire → ESP GPIO20
  - The 30kΩ total is high-impedance enough not to load the bus
  - Tap voltage = 5V × (20kΩ/30kΩ) = 3.33V — exactly what a 3.3V ESP wants
- **74HCT1G125 buffer** placed on the panel-TX wire side, but with its Y output
  lifted (disconnected). No transmission is possible yet, but the chip is
  in-place ready for rev 2.
- OE# tied to GND (works for rev 2 in Phase 2b "full panel emulation" mode;
  not safe for "co-talk with panel" until OE# is moved to a GPIO)

**Rev 2 (planned, Phase 2):**

- Add a **second divider** on the panel-TX wire → second ESP GPIO (e.g. GPIO10)
  - Lets the component see panel polls + button-press writes
  - Enables emitting HA logbook events like "user pressed UP at the panel"
- **Reconnect 74HCT1G125 Y pin** to the panel-TX wire
- **Move OE# from GND to an ESP GPIO** (with a 10kΩ pull-up to 3.3V for safe
  default during ESP reset/boot)
- Firmware adds a Modbus master state machine that:
  - Waits for a quiet window (~3 seconds between panel polls)
  - Asserts OE# LOW, sends the write frame, asserts OE# HIGH
  - Watches for the `0x0A` ACK and retries on collision

### Why a level shifter is needed

The bus runs at **5V CMOS logic**. The ESP32-C3 outputs only 3.3V on its TX
pin. There are two problems with connecting them directly:

1. **Voltage mismatch.** The mainboard's UART input might or might not register
   3.3V as a logic HIGH (depends on whether it's TTL- or CMOS-compatible).
2. **Drive strength.** ESP GPIO outputs source only ~12mA, and the bus has
   capacitance + the panel's own TX driver in parallel. ESP's weak drive could
   be overwhelmed.

The 74HCT1G125 solves both:

- **HCT family inputs are TTL-compatible** — they recognise 3.3V as HIGH
  (VIH ≥ 2.0V)
- **HCT outputs are rail-to-rail at 5V** with ±20mA drive

The OE# (output enable, active LOW) pin is the key to **bus arbitration**:

| OE# state | 74HCT output | Bus arbitration semantics |
|-----------|--------------|---------------------------|
| **HIGH** | High-Z (disconnected) | Panel/mainboard drive freely. Safe default. |
| **LOW** | Driving 5V/0V per ESP TX | ESP is "on the bus." Anything else must wait. |

By default OE# stays HIGH; firmware only asserts it LOW for the duration of a
write transmission, then releases it. The 74HCT cleanly tristates between
transmissions, so the panel can keep doing its thing without contention.

---

## Protocol

### Physical layer

- **Modbus RTU** over **TTL UART** at **9600 baud, 8 data bits, no parity,
  1 stop bit** (9600 8N1)
- **5V signalling** on the wire
- **Point-to-point** between the panel (master) and the mainboard (slave)
- **No RS-485** — the mainboard has an unpopulated 8-pin SOIC footprint that
  would have been an RS-485 transceiver, but it's not stuffed on any SKU we've
  examined. The protocol is Modbus RTU running over plain TTL.

### Frame format

Both directions use standard Modbus RTU framing:

```
+---------+-----+-----------+---------+
| Addr    | Fn  | Payload   | CRC-16  |
| 1 byte  | 1 B | variable  | 2 bytes |
+---------+-----+-----------+---------+
```

**CRC-16:** standard Modbus CRC, polynomial `0xA001` (reflected `0x8005`),
initial value `0xFFFF`. Transmitted little-endian (CRC low byte first, then
CRC high byte).

**Frame boundaries:** delimited by an inter-byte silence of >50ms. The
component uses a 50ms timeout to flush the receive buffer and attempt to
parse.

### Mainboard slave address

Always **`0x01`**. Any frame with a different address byte is ignored by
the component.

### Master polling

The panel polls the mainboard **every ~3.2 seconds** with a fixed frame:

```
01 03 00 00 00 0C 45 CF
│  │  └─┬─┘ └─┬─┘ └─┬─┘
│  │    │     │     └─ CRC-16 (LSB first)
│  │    │     └─────── Quantity = 12 registers
│  │    └───────────── Start register = 0
│  └────────────────── Function 0x03 (Read Holding Registers)
└───────────────────── Slave address (mainboard)
```

The poll asks for 12 holding registers starting at address 0.

### Slave response (telemetry)

The mainboard responds ~35–42 ms later with a non-standard quirk: even though
the master asks for **12 registers**, the slave returns only **9** (byte count
`0x12` = 18 bytes of data = 9 registers). Treat the response as a fixed-shape
9-register status block.

```
01 03 12 00 26 00 26 00 1D 00 00 00 1C 00 00 C0 40 00 00 00 00 0B 21
│  │  │  └──slots─0─through─8──(9 × 2 bytes = 18 B)──────────┘  └─CRC─┘
│  │  └─── Byte count = 0x12 = 18 (= 9 registers)
│  └─────── Function 0x03 echoed
└────────── Slave address
```

#### Register map (read)

All registers are 16-bit big-endian (MSB first).

| Slot (0-indexed) | Bytes in response | Meaning                                                              |
|------------------|-------------------|----------------------------------------------------------------------|
| **0**            | `[3..4]`          | **Target RH** (setpoint, %)                                          |
| **1**            | `[5..6]`          | **Current RH** (%, from the combined digital HUM sensor)             |
| **2**            | `[7..8]`          | **Ambient temperature** (°C, same HUM sensor)                        |
| 3                | `[9..10]`         | Reserved (always 0)                                                  |
| **4**            | `[11..12]`        | **Evaporator coil temperature** (°C, from a separate 2-pin NTC)      |
| 5                | `[13..14]`        | Reserved (always 0)                                                  |
| **6**            | `[15..16]`        | **Status word** — mode / compressor / flood — see below              |
| **7**            | `[17..18]`        | **Error word** — sensor / temperature faults — see below             |
| 8                | `[19..20]`        | Reserved (always 0)                                                  |

### Status word (slot 6)

A 16-bit bitfield. The high byte holds compressor/safety state; the low byte
holds the current mode plus the flood-alarm bit.

#### High byte

| Bit  | Mask      | Meaning                                                                                  |
|------|-----------|------------------------------------------------------------------------------------------|
| 15   | `0x8000`  | **Powered on** (unit logically on, not just mains)                                       |
| 14   | `0x4000`  | **Demand satisfied** — 1 = no cooling needed; 0 = calling for cooling                    |
| 13   | `0x2000`  | (untoggled in all observations)                                                          |
| 12   | `0x1000`  | **Latched alarm** — sticky fault that needs a power-cycle / manual reset                 |
| 11   | `0x0800`  | **Compressor allowed** by safety logic — 0 = inhibited (flood / overtemp / sensor fault) |
| 10   | `0x0400`  | (untoggled)                                                                              |
| 9    | `0x0200`  | **Compressor running** (relay closed, refrigerant actively flowing)                      |
| 8    | `0x0100`  | (untoggled)                                                                              |

#### Low byte

| Bit  | Mask      | Meaning                                                            |
|------|-----------|--------------------------------------------------------------------|
| 6    | `0x0040`  | **Mode: AUTO**                                                     |
| 5    | `0x0020`  | **Mode: CONTINUOUS**                                               |
| 4    | `0x0010`  | **Mode: SLEEP** (also auto-bumps the setpoint to 60%)              |
| 0    | `0x0001`  | **External flood-contact closed** (dry-contact input on WF header) |

#### Derived states

These are computed by the component, not stored as separate bits:

- `lockout_safety` = `!bit11` (safety logic is inhibiting the compressor)
- `lockout_timer` = compressor recently stopped and 3-min anti-short-cycle
  timer hasn't elapsed yet. **Not exposed as a bit** — derived in firmware
  from the falling edge of bit 9. (Will be implemented in a future component
  release.)
- `calling_for_cooling` = `!bit14`

### Error word (slot 7)

A 16-bit bitfield split into **HUM-sensor side** (high byte) and
**coil-sensor side** (low byte). Bits within each half stack.

| Bit / mask | Side | Maps to displayed code | When it fires                                                       |
|------------|------|------------------------|---------------------------------------------------------------------|
| `0xC000`   | HUM  | **E1** / **CH** / **LO** | HUM sensor invalid (`E1`), OR ambient temp >113°F (`CH`), OR RH < 20% (`LO`). The panel disambiguates based on the current Ta/RH values; the component does the same to publish the right `error_code` text. |
| `0x0080`   | Coil | **E2**                 | Coil NTC failure (open / short / out-of-range)                      |

| **Important:** HI (RH > 95%) does **NOT** propagate to the bus. The panel
displays it but the mainboard never sets it on Modbus, because per the manual
the unit keeps running normally during HI — there's no operational state to
flag. Build any "RH too high" HA automation off the `current_humidity` sensor
directly. |

### Documented error codes (from VEVOR's manual)

| Code | Trigger condition           | Manual behaviour                                              |
|------|------------------------------|---------------------------------------------------------------|
| E1   | Temp + humidity sensor fail  | "Check or replace sensor"                                     |
| E2   | Coil sensor fail              | "Check or replace sensor"                                     |
| CL   | Ambient Tr < 36°F (~2°C)      | Compressor stops, fan delays; resumes at 39°F                 |
| CH   | Ambient Tr > 113°F (~45°C)    | Compressor stops, fan delays; resumes at 108°F                |
| LO   | Ambient Hr < 20%              | Compressor stops, fan delays; resumes >20%                    |
| HI   | Ambient Hr > 95%              | Compressor and fan **continue running normally** (not on bus) |

No "FULL" / water-full code exists on the 125-pint commercial SKU — the unit
is designed for continuous drainage. (The "FULL" connector on the mainboard is
an **external dry-contact input** for an optional flood sensor, surfaced
through slot 6 bit 0.)

### Write commands (panel → mainboard)

The panel writes user actions using **`Write Multiple Registers` (function
`0x10`)**, always with quantity 1. The frame layout:

```
01 10 [ADDR_HI] [ADDR_LO] 00 01 02 [VAL_HI] [VAL_LO] [CRC_LO] [CRC_HI]
│  │  └────────┬────────┘ └─┬─┘ │  └────────┬────────┘
│  │      Start register   qty  bc       new value
│  └─ Function 0x10 (Write Multiple Registers)
└──── Slave address
```

#### Confirmed write addresses

| Address  | Function   | Values observed                  | Notes                                                                          |
|----------|------------|----------------------------------|--------------------------------------------------------------------------------|
| `0x0000` | POWER      | `0` = off, `1` = on              |                                                                                |
| `0x0001` | SETPOINT   | Raw RH% (saw 30–60, range likely 30–80) |                                                                          |
| `0x0004` | TIMER      | 0–24 hours; `0` = disabled       | Countdown is internal to the mainboard, **not exposed on the bus**             |
| `0x0005` | MODE       | `1` = sleep, `2` = continuous, `3` = auto | Sleep mode auto-bumps the setpoint to 60                              |

#### Slave write ACK (non-standard)

The mainboard responds to every write with a **non-standard ACK frame**:

```
01 0A [ADDR_HI] [ADDR_LO] 00 01 [CRC_LO] [CRC_HI]
│  │  └────────┬────────┘ └─┬─┘
│  │       Address echo    Qty echo
│  └─── Function byte = 0x0A (NOT 0x10 like standard Modbus)
└────── Slave address
```

The ACK function byte is `0x0A`, **not** `0x10` like the request. This is a
non-compliance with Modbus RTU — any Modbus-master library expecting an echo
of the request function will reject these ACKs. The component handles them
explicitly.

### Address quirk

There's an **off-by-one** between read indexing and write addressing:

- **Reads** are 0-indexed: register 0 is at slot 0 in the response
- **Writes** are 1-indexed: writing to address `0x0001` modifies what shows
  up at slot 0 in the next read response

So `write_addr = read_slot + 1` — but only for the user-controlled slots
(0, 1, 4, 5). The mapping doesn't generalize to other slots.

### Hardware sensors behind the protocol

This was discovered through reverse-engineering and overrides the manufacturer
schematic notes:

- **The 4-pin "HUM" connector carries a combined digital RH + temperature
  sensor** (likely an SHT-family chip). It supplies both slot 1 (current RH)
  AND slot 2 (ambient temperature). Unplugging it knocks out both
  simultaneously.
- **The 2-pin "T1/PT1" NTC is the coil temperature sensor**, not an ambient
  thermistor. Supplies slot 4.
- **No separate ambient temperature sensor exists** — ambient is the HUM
  sensor's temperature channel.

This explains why some error states (CH, LO, E1) all share the same `0xC000`
bit pattern: heating the HUM sensor pegs both its channels, which the
mainboard flags as "HUM subsystem fault." The panel applies context-aware
logic to choose which code to display.

---

## Configuration reference

### Top-level

```yaml
vevor_dehumidifier:
  id: dehumidifier        # any id you want to reference elsewhere
  uart_id: bus_uart       # must reference a configured uart:
```

### Sensor platform

All optional — declare only the ones you want exposed to HA.

```yaml
sensor:
  - platform: vevor_dehumidifier
    target_humidity:
      name: "Target Humidity"          # %
    current_humidity:
      name: "Current Humidity"         # %
    ambient_temperature:
      name: "Ambient Temperature"      # °C
    coil_temperature:
      name: "Coil Temperature"         # °C (diagnostic by default)
    status_word:
      name: "Status Word"              # raw uint16, diagnostic
    error_word:
      name: "Error Word"               # raw uint16, diagnostic
    bus_polls_per_minute:
      name: "Bus Polls Per Minute"     # bus-alive health, diagnostic
```

### Binary sensor platform

```yaml
binary_sensor:
  - platform: vevor_dehumidifier
    power:
      name: "Power"                    # device_class: power
    compressor_running:
      name: "Compressor Running"       # device_class: running
    compressor_allowed:
      name: "Compressor Allowed"       # diagnostic
    calling_for_cooling:
      name: "Calling For Cooling"      # diagnostic
    lockout_safety:
      name: "Safety Lockout"           # device_class: problem
    flood:
      name: "Flood"                    # device_class: moisture
    alarm_latched:
      name: "Latched Alarm"            # device_class: problem
    hum_sensor_fault:
      name: "Humidity Sensor Fault"    # device_class: problem, diagnostic
    coil_sensor_fault:
      name: "Coil Sensor Fault"        # device_class: problem, diagnostic
    panel_present:
      name: "Panel Present"            # device_class: connectivity, diagnostic
```

### Text sensor platform

```yaml
text_sensor:
  - platform: vevor_dehumidifier
    mode:
      name: "Mode"                     # "auto" / "continuous" / "sleep" / "unknown"
    error_code:
      name: "Error Code"               # "" / "E1" / "E2" / "CH" / "CL" / "LO"
    status:
      name: "Status"                   # composite single-string for dashboards
```

---

## Reverse-engineering history

This component is the result of a single overnight effort tapping the
mainboard↔panel UART with an ESP32-S3, capturing live frames, and decoding
each field by observing the unit's behaviour under deliberate inputs:

- ~30 captures totalling thousands of frames
- Triggered every documented error code by manipulating sensors physically
  (unplugging HUM and coil sensors, heating the HUM with body warmth, etc.)
- Identified the SPI-to-touch-panel bus on CN6 with a Saleae logic analyzer
- Mapped the mainboard's MCU pinout, confirmed no separate WiFi UART exists
- Settled the Phase 2a vs 2b vs 2c architecture decision with continuity tests

The full play-by-play including dead ends, false leads, and "aha" moments
lives in [`decode-notes.md`](decode-notes.md). It's a useful read if you want
to extend the component to other VEVOR SKUs or related Chinese-ODM
dehumidifiers — many of them appear to use this same Modbus dialect.

---

## Compatible models

Confirmed:

- VEVOR 125-pint commercial dehumidifier (OL-series, OL60D / OL80 / OL100
  family)
- Mainboard ID **LDD241125 / BD-D083.5Q / BD-3500088**, board house KB-5150

Likely compatible (same chassis family, untested):

- Smaller VEVOR commercial dehumidifiers using the same mainboard
- Other Chinese-ODM dehumidifiers using the LDD24xxxx mainboard series

If you have a different VEVOR model, **the protocol is likely identical**.
Worth testing — feedback / PRs welcome with model + observation.

---

## Roadmap

### Phase 1 — read-only ✅ (this release)

- All sensors, binary_sensors, text_sensors decoded and published
- Verified on ESP32-C3 against live hardware

### Phase 2 — Modbus master writes

- Board rev 2: second divider, OE# on a GPIO, 74HCT Y reconnected
- Firmware: Modbus master state machine with bus arbitration
- HA entities:
  - `switch.power`
  - `number.target_humidity` (range 30–80 %, step 1)
  - `select.mode` ("Sleep" / "Continuous" / "Auto")
- HA logbook events on panel-side activity:
  - `esphome.dehumidifier_panel_mode_change`
  - `esphome.dehumidifier_panel_setpoint_change`
  - `esphome.dehumidifier_panel_power`
  - `esphome.dehumidifier_panel_timer_set` / `…_cancel`
  - `esphome.dehumidifier_compressor_start` / `…_stop` (with run duration)
  - `esphome.dehumidifier_alarm_set` / `…_clear`

### Phase 3 (optional polish)

- `lockout_timer` binary sensor — software-derived 3-min anti-short-cycle
  window tracking via bit 9 falling-edge detection
- `defrost_active` binary sensor (if/when defrost cycles are decoded)
- HA `humidifier` integration shim in YAML

---

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgements

Built collaboratively with Claude (Anthropic) over a single intensive session.
The protocol decode, hardware analysis, and component implementation all took
shape live in the chat. See [`decode-notes.md`](decode-notes.md) for the
running engineering log if you want the chronological narrative.
