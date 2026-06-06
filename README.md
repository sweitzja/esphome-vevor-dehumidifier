# ESPHome VEVOR Dehumidifier

ESPHome external component that talks to the display-bus of VEVOR's OL-series
commercial dehumidifiers (OL60D / OL80 / OL100 / 125-pint family) over a tapped
5V TTL UART, decoding the mainboard's Modbus RTU traffic into Home Assistant
sensors, binary_sensors, and text_sensors.

## Status

- **Phase 1 (this rev): read-only.** Listens to the mainboard→panel response
  wire, decodes Modbus 0x03 responses, publishes everything the panel sees.
- **Phase 2 (next board rev): writes.** Adds Modbus master writes for power /
  setpoint / mode, with proper bus arbitration using the 74HCT1G125's OE#
  control.

## Hardware

- ESP32-C3 module (ESP32-C3-WROOM-02-N4 in the reference design)
- 10kΩ + 20kΩ resistor divider on the mainboard→panel data wire → ESP GPIO20
- 74HCT1G125 single-gate buffer (TX-side, lifted Y pin in rev 1 = no
  transmission yet; OE# tied to GND for now)
- Powered from the panel-port 5V rail through a 3.3V regulator

The board sits inline between the panel and the mainboard. See the
hardware/ directory of the schematic (or the project notes) for the
schematic and BOM.

## Protocol

The dehumidifier's display bus runs Modbus RTU at 9600 8N1. The panel is the
master (slave addr `0x01` = mainboard), polling every ~3.2 seconds with a
`Read Holding Registers` for 12 registers starting at address 0. The mainboard
returns 9 registers of status (non-strict; master asked for 12). Writes use
`Write Multiple Registers` (`0x10`) with non-standard ACK function code `0x0A`.

### Register map

| Slot | Meaning                                  | Notes                                |
|------|------------------------------------------|--------------------------------------|
| 0    | Target RH%                                | Written via addr `0x0001`             |
| 1    | Current RH%                               | From the combined HUM digital sensor |
| 2    | Ambient temp °C                           | Same HUM sensor                       |
| 3    | reserved                                  |                                      |
| 4    | Coil temp °C                              | NTC on evap coil                      |
| 5    | reserved                                  |                                      |
| 6    | Status word — mode, compressor, flood     | See [status word bits](#status-word) |
| 7    | Error word — sensor faults / overtemp     | See [error word bits](#error-word)   |
| 8    | reserved                                  |                                      |

### Status word (slot 6)

| Bit / mask | Meaning                                                            |
|------------|--------------------------------------------------------------------|
| `0x8000`   | Powered on                                                         |
| `0x4000`   | Demand satisfied (1 = no cooling needed, 0 = calling for cooling)  |
| `0x1000`   | Latched alarm (needs power-cycle to clear)                         |
| `0x0800`   | Compressor allowed by safety (1 = OK to run, 0 = blocked)          |
| `0x0200`   | Compressor relay closed (actually running)                         |
| `0x0040`   | Mode: AUTO                                                         |
| `0x0020`   | Mode: CONTINUOUS                                                   |
| `0x0010`   | Mode: SLEEP                                                        |
| `0x0001`   | External flood contact closed                                      |

### Error word (slot 7)

| Bit / mask | Meaning                                                                       |
|------------|-------------------------------------------------------------------------------|
| `0xC000`   | HUM-side fault — covers **CH** (overtemp), **LO** (low RH), **E1** (HUM fail) |
| `0x0080`   | Coil sensor fault (**E2**)                                                    |

The panel uses context (ambient °C / RH value) to display CH vs LO vs E1.
The component does the same disambiguation when publishing `error_code`.

## Configuration

Drop this repo into your ESPHome configuration directory (or reference it
via `external_components` from a remote git URL — see below).

```yaml
external_components:
  - source:
      type: local
      path: components
    # or, once published to GitHub:
    # type: git
    # url: https://github.com/jjsweitzer/esphome-vevor-dehumidifier
    # ref: main

uart:
  - id: bus_uart
    rx_pin: GPIO20
    tx_pin: GPIO21
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
    # ... etc.

text_sensor:
  - platform: vevor_dehumidifier
    mode:
      name: "Mode"
    status:
      name: "Status"
    error_code:
      name: "Error Code"
```

See `dehumidifier-c3.yaml` for the full reference configuration with every
entity wired up.

## Available entities

### Numeric sensors

- `target_humidity` (%)
- `current_humidity` (%)
- `ambient_temperature` (°C)
- `coil_temperature` (°C, diagnostic)
- `status_word` (raw uint16, diagnostic)
- `error_word` (raw uint16, diagnostic)
- `bus_polls_per_minute` (rate, diagnostic; "is the bus alive")

### Binary sensors

- `power` (unit logically on)
- `compressor_running`
- `compressor_allowed` (safety logic permits compressor)
- `calling_for_cooling`
- `lockout_safety` (= !compressor_allowed; problem device class)
- `flood` (external dry-contact input closed)
- `alarm_latched` (sticky alarm needs reset)
- `hum_sensor_fault` (HUM sensor failure / CH / LO)
- `coil_sensor_fault` (E2)
- `panel_present` (bus traffic seen in last 10 s)

### Text sensors

- `mode` — "auto" / "continuous" / "sleep" / "unknown"
- `error_code` — "" / "E1" / "E2" / "CH" / "CL" / "LO"
- `status` — composite single string for a dashboard tile

## Wiring (rev 1, read-only)

```
   Mainboard-TX wire ─┬──[10kΩ]──┬── ESP GPIO20  (RX)
                      │          │
                      │       [20kΩ]
                      │          │
                      │         GND
                      │
                      └── continues to Panel-RX
```

The divider's high-impedance tap (30kΩ total) doesn't load the bus.
The 5V bus signal becomes ~3.3V at the divider midpoint — safe for
the C3's input.

The 74HCT1G125 sits on the *other* wire (panel→mainboard) for future writes,
but in rev 1 its Y output is lifted so it can't drive yet.

## Phase 2 roadmap

To add control (HA writes setpoint / power / mode):

1. **Add a second divider** on the panel→mainboard wire feeding a free GPIO.
   Lets the component see panel polls + button-press writes.
2. **Reconnect the 74HCT1G125 Y pin** and route its OE# pin to a free ESP GPIO
   (with a 10kΩ pull-up to 3.3V for safe boot defaults).
3. **Add a Modbus master state machine** in the C++ component: listen for the
   3.2-second quiet window between panel polls, assert OE# LOW, send the write
   frame, watch for the `0x0A` ACK on the response wire, deassert OE#.
4. **Expose write entities** — `switch.power`, `number.target_humidity`,
   `select.mode`.

Alternative (simpler): full panel emulation — disconnect the panel entirely,
have the ESP become sole master with OE# tied to GND. No bus arbitration
needed, but the unit loses its physical UI.

## License

MIT — see `LICENSE`.
