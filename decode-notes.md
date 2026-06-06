# VEVOR Dehumidifier Display-Bus — Decode Notes

Working notebook for protocol bytes as they're identified. See `CLAUDE.md` (when
added) for hardware/strategy. Captures live in `captures/`.

## Device under test
- Sniffer: ESPHome on ESP32-S3-DevKitC-1 at `192.168.1.210` (`dehumidifier-prototype.local:6053`)
- ESPHome version on-device: 2026.5.2, compiled 2026-06-04
- Sniffer YAML: `dehumidifier.yaml` — UART id `disp_uart`, RX=GPIO6, TX=GPIO8, 9600 8N1, `dummy_receiver: true`, both-direction hex dump via `UARTDebug::log_hex`.

## Capture log

| # | File                       | Date       | Label                   | Notes                                                                                                     |
|---|----------------------------|------------|-------------------------|-----------------------------------------------------------------------------------------------------------|
| 1 | `01_idle.log`              | 2026-06-05 | idle / single-RX / 60s  | Bus silent through tap. Listened on GPIO6 = wrong pin (wires are on board's RX/TX = GPIO44/43).           |
| 2 | `02_idle_dualrx.log`       | 2026-06-05 | idle / GPIO6+5 / 60s    | Silent. Same wrong-pin cause as #1.                                                                       |
| 3 | `03_buttonpresses.log`     | 2026-06-05 | button-press / GPIO6+5  | Silent. User pressed some buttons; same wrong-pin issue.                                                  |
| 4 | `04_idle_gpio43_44.log`    | 2026-06-05 | idle / GPIO43+44 / 60s  | **First good capture.** Modbus RTU traffic on both directions at 9600 8N1.                                |
| 5 | `05_press_up_to_45.log`    | 2026-06-05 | press ▲ from 38 → 45    | 7 write frames (fn `0x10`, addr `0x0001`, vals 39…45). Setpoint = slot 0 of read response; current RH = slot 1. |
| 6 | `06_mode_power_run.log`    | 2026-06-05 | mode cycle + power off/on + walk setpoint down past current RH | Confirmed write addrs 0/1/5. ACK fn byte = `0x0A` (not 0x10). Coil temp (slot 4) dropped 28→12°C during run. Partial slot-6 bit decode below. |
| 7 | `07_slow_mode_cycle.log`   | 2026-06-05 | slow MODE cycle 10–30s between presses, then walk setpoint 60→30 in auto | Settled mode value→name mapping (`1=SLEEP, 2=CONTINUOUS, 3=AUTO`). Confirmed slot 6 low byte = mode bit. Confirmed SLEEP auto-bumps setpoint to 60. Compressor did NOT turn on for 85s of demand → coil warmed 18→23°C. User clarified later: **FULL is not a mode but a water-full alarm state**; **DEFROST** is another alarm/state. |
| 8 | `08_long_compressor_wait.log` | 2026-06-05 | 8-min wait in auto with sp walked 30→24 | **Bit 9 = compressor running confirmed.** Coil stayed pinned at 9–10°C for full 8 min while s6 stayed `0x8A40`. Compare to capture #7 where bit 9 was clear and coil warmed 18→23°C. |
| 9 | `09_ch_high_temp.log`      | 2026-06-05 | Heat HUM sensor to 48°C → CH panel display | **CH overtemp captured.** `s7 = 0xC000` (high-byte bits 14+15) when alarm latches. `s6 bit 12 = 0x1000` sets (latched-alarm sentinel). `s6 bit 11` clears (compressor inhibit by safety) during protection. Alarm appears with delay — temp had to recover before code took. |
| 10 | (n/a)                    | 2026-06-05 | (no capture) — user observation only | User leak-tested by plugging drain; unit ran continuously, no FULL alarm. Confirmed: no internal water-full sensor on this SKU. |
| 11 | `11_hum_then_temp_pulls.log` | 2026-06-05 | Pull HUM connector then pull coil NTC | **E1 = `s7=0xC000`** (HUM pulled, both rh and Ta went haywire because HUM is a combined RH+T digital chip). **E2 = `s7=0x0080`** (coil NTC pulled). Both transient — `s6 bit 12` did NOT latch. |
| 12 | `12_flood_contact.log`     | 2026-06-05 | External flood dry-contact closure | **Flood = `s6 bit 0` (`0x0001`)** in low byte alongside the mode bit. Compressor inhibited via `s6 bit 11` clearing while flood active. `s7` unaffected. Auto-clears on contact open. |
| 13 | `13_cl_low_temp.log`       | 2026-06-05 | First CL attempt — ambient cooling | Ta dropped 28→26°C only — nowhere near the 2°C/36°F CL threshold. No alarm tripped. |
| 14 | `14_cl_attempt2.log`       | 2026-06-06 | Second CL attempt; also brushed HI | RH peaked at 95% but no alarm. Compressor running normally. |
| 15 | `15_hi_watch.log`          | 2026-06-06 | Sustained-high-RH watch | RH 95–97% sustained over multiple polls. **Confirmed: HI does NOT propagate to the bus** — panel-display only. Manual already said compressor and fan keep running during HI, so no operational reason to flag it. |
| 16 | `16_timer_test.log`        | 2026-06-06 | TIMER + ▲ once         | **TIMER write address found: `0x0004`.** Value `1` written. No countdown register populated; slot 6/7 unchanged. |
| 17 | `17_timer_set_3.log`       | 2026-06-06 | TIMER + ▲ + ▲ to set 3 | Confirmed each ▲ increments via absolute-value writes (saw values `2`, `3`). **Timer = hours, range 0–24**, per user. Countdown not on bus — entirely internal. |
| 18 | `18_timer_cancel.log`      | 2026-06-06 | First cancel attempt (no writes) | User attempted to cancel; nothing fired on the bus. Suggests "press TIMER while flashing" is panel-local. |
| 19 | `19_timer_cancel2.log`     | 2026-06-06 | TIMER+▲▲▲ then ▼▼▼ to 0 then TIMER+▲▲▲▲ then TIMER+TIMER | Confirmed: ▼ walks the absolute value down (3→2→1→0). **Writing `0` to addr `0x0004` cancels the timer.** Re-set after cancel still works. **Important panel UX gotcha:** pressing TIMER + TIMER (per the panel manual's "press TIMER when flashing to cancel") generates ZERO bus writes — it only exits edit mode without changing the value. So if you set timer=4 and then press TIMER + TIMER thinking you cancelled it, the mainboard is still counting down from 4. Real cancel = write `0`. No slot 6/7/8 change throughout. |

## Helpers

`captures/parse.py` — Python decoder that walks an esphome-logs file and prints labeled events (POLL/WRITE/ACK/RESP) chronologically, collapsing consecutive identical RESP frames. Usage: `python3 captures/parse.py captures/NN_label.log`. Drops the standalone POLLs with `| grep -v ' POLL$'`.

## Protocol summary (as of 2026-06-05)

**Physical:** TTL UART, 5V logic, point-to-point. Both data wires of the 4-pin live panel port tapped via ~4.7kΩ series R to ESP GPIO44 (mainboard TX side) and GPIO43 (panel TX side). ESP powered from the same 5V/GND.

**Link layer:** 9600 baud, 8N1.

**App layer:** Modbus RTU (loose interpretation — see byte-count quirk below).

**Master:** the real panel.
**Slave:** the mainboard, address `0x01`.
**Cadence:** panel polls every ~3.2s. Mainboard responds ~35–42 ms later.

### Telemetry poll (panel → mainboard, 8 B, every ~3.2s)
```
01 03 00 00 00 0C 45 CF
```
- `01` slave addr
- `03` function = Read Holding Registers
- `00 00` starting register address = 0
- `00 0C` quantity = **12 registers** (per Modbus spec — slave returns only 9, see below)
- `45 CF` Modbus CRC-16 (LSB first)

### Telemetry response (mainboard → panel, 23 B)
```
01 03 12 00 26 00 26 00 1D 00 00 00 1C 00 00 C0 40 00 00 00 00 0B 21
```
- `01` slave addr
- `03` function echo
- `12` byte count = **0x12 = 18 bytes = 9 registers** — non-strict Modbus (master asked for 12, slave returns 9). The slave's status block is 9 regs wide.
- 18 bytes of register data
- `0B 21` CRC

### Register map (confirmed via captures #5, #6, #9, #11)

**Read-response slot indexing is 0-based; write addressing is 1-based.** They refer to the same data — the slave is sloppy about address conventions. So slot 0 (read) ↔ write addr `0x0001`, slot 4 (read) ↔ write addr `0x0005`, etc. Slot N ↔ write addr N+1.

| Slot (read) | Write addr | Meaning                                                                                                  |
|-------------|------------|----------------------------------------------------------------------------------------------------------|
| 0           | `0x0001`   | **Target RH%** (setpoint). Range observed 30–60; real range likely 30–80.                                |
| 1           | `0x0002`   | **Current RH%** (from combined digital HUM sensor).                                                      |
| 2           | `0x0003`   | **Ambient temp °C** — also from the digital HUM sensor (chip provides both RH and temp channels).        |
| 3           | `0x0004`   | unused / reserved (always 0)                                                                             |
| 4           | `0x0005`   | **Evaporator coil temp °C** — from the 2-pin NTC. Dropped 28 → **12°C** during compressor run.           |
| 5           | `0x0006`   | unused / reserved (always 0)                                                                             |
| 6           | `0x0007`   | **Mode / status flag word.** See bit decode below.                                                       |
| 7           | `0x0008`   | **Error / alarm bitfield.** See error encoding below.                                                    |
| 8           | `0x0009`   | unused (always 0 in all observations so far)                                                             |

**Sensor identity** (corrected from CLAUDE.md, which had this swapped):
- The 4-pin "HUM" connector → combined digital humidity+temperature sensor (SHT-family-style). Supplies BOTH slot 1 (RH) AND slot 2 (ambient temp).
- The 2-pin "T1/PT1" NTC → measures the **coil**, not the ambient. Supplies slot 4.
- There is NO separate ambient-only temperature sensor on the mainboard.

NOTE: write addr `0x0005` is the **MODE** write but it overlaps with slot 4 = coil temp in the read response. That's because the slave's read and write address maps are disjoint — the slave doesn't treat read-addr-N as the same memory as write-addr-N+1 once you're past the user-controlled regs (0, 1, 5). Don't assume slot-N (read) ↔ write addr N+1 globally; the mapping holds only for the few overlapping ones we've observed:
- slot 0 ↔ write addr `0x0001` (target RH) — confirmed
- slot ? ↔ write addr `0x0000` (power) — unclear which slot reflects power
- slot ? ↔ write addr `0x0005` (mode) — flags appear in slot 6, not slot 4

### Slot 6 (mode/status flag word) — bit map after captures #6 + #7

**Low byte = mode indicator (one-hot):**

**Slot 6 low byte is multi-purposed** — mode bits share the byte with the flood alarm bit:

| Mode write value | Mode name   | Slot 6 low-byte bit | Source                                     |
|------------------|-------------|---------------------|--------------------------------------------|
| `1`              | SLEEP       | bit 4 (`0x10`)      | After sleep write, sp auto-jumped to 60    |
| `2`              | CONTINUOUS  | bit 5 (`0x20`)      | By elimination + ongoing cooling demand    |
| `3`              | AUTO        | bit 6 (`0x40`)      | User stated this was the final state       |
| —                | (flood)     | bit 0 (`0x01`)      | External flood-contact closed (capture #12). Sets independently of mode bits — when active, low byte reads e.g. `0x41` for auto + flood. |

**There are only 3 modes.** User clarified: "FULL" is not a mode; "DEFROST" status unclear / probably not exposed on this SKU. The mainboard does have a flood-alarm input — it's an external dry-contact input on the WF connector, not an internal float switch.

**High byte (bit map, current understanding through capture #15):**

| Bit | Mask     | Meaning                                                                                                                 | Confirmed by |
|-----|----------|-------------------------------------------------------------------------------------------------------------------------|--------------|
| 15  | `0x8000` | Powered-on sentinel — set in every powered-on state, clears with POWER OFF                                              | All captures |
| 14  | `0x4000` | **Demand satisfied** — 1 = no cooling needed; 0 = calling for cooling                                                   | #5, #7 transitions across sp/rh |
| 13  | `0x2000` | Untoggled                                                                                                               | — |
| 12  | `0x1000` | **Latched alarm** — set only when slot 7 holds a *sticky* alarm needing manual reset (e.g. sustained CH)                | #9 (set with CH); #11 (clear during transient LO/E2) |
| 11  | `0x0800` | **Compressor allowed by safety logic** — set = OK to run; clear = blocked by flood/overtemp/sensor fault                 | #9 (cleared during CH); #12 (cleared during flood) |
| 10  | `0x0400` | Untoggled                                                                                                               | — |
| 9   | `0x0200` | **Compressor running** — 1 = relay closed; 0 = relay open                                                               | #8 (coil pinned cold 8 min) |
| 8   | `0x0100` | Untoggled                                                                                                               | — |

**Derived states:**
- `powered_on = bit15`
- `calling_for_cooling = !bit14`
- `compressor_running = bit9`
- `compressor_allowed_safety = bit11`
- `lockout_safety = !bit11` (flood / overtemp / sensor-fault inhibit)
- `lockout_timer ≈ (just-stopped within 3 min) && !running && allowed_safety` (no dedicated bit on the bus — derive in firmware with a falling-edge detector on `bit9`)
- `compressor_blocked = lockout_safety OR lockout_timer` (headline "anything stopping the compressor right now")
- `alarm_latched = bit12` (manual reset / power cycle required)

### Observed slot 6 values (cross-capture)

| Value     | High bits set | Low / mode | Captured in    | Meaning                                                                 |
|-----------|---------------|------------|----------------|-------------------------------------------------------------------------|
| `0x0040`  | none          | auto       | #6             | POWER OFF                                                               |
| `0xC040`  | 15, 14        | auto       | #6             | Long-stable idle, demand satisfied, safety allowed                      |
| `0xC840`  | 15, 14, 11    | auto       | #6, #7         | Idle, satisfied, safety allowed (most common idle state)                |
| `0xC810`  | 15, 14, 11    | sleep      | #7             | Sleep mode at sp=60, satisfied                                          |
| `0x8840`  | 15, 11        | auto       | #7, #11, #12   | Calling for cooling, compressor allowed, not running → **lockout-timer or just-stopped** |
| `0x8A40`  | 15, 11, 9     | auto       | #6, #7, #8     | Calling for cooling, **compressor running** (running state)             |
| `0x8A20`  | 15, 11, 9     | continuous | #7             | Continuous mode, running                                                |
| `0x8040`  | 15            | auto       | #9             | Calling, **safety lockout** (bit 11 clear during CH overtemp)           |
| `0x8041`  | 15            | auto+flood | #12            | Calling, safety lockout, **flood contact closed** (bit 0 set)           |
| `0x8841`  | 15, 11        | auto+flood | #12 transition | Brief frame at flood-trip moment (before bit 11 clears)                 |
| `0x9840`  | 15, 12, 11    | auto       | #9             | **Latched alarm** active (bit 12 set; CH code in slot 7)                |

### What we still don't know about slot 6

- **Bits 8, 10, 13** (high byte) still untoggled in every capture. Candidates:
  - A dedicated lockout-timer bit (would be set during the 3-min anti-short-cycle window, independent of safety). Not yet seen; firmware-derived workaround is fine.
  - DEFROST status (manual doesn't list one for this SKU, so possibly never used).
  - CL distinct flag (if `s7` ends up sharing `0xC000` with E1/CH/LO, a slot 6 bit might distinguish CL specifically).
- The mainboard's anti-short-cycle countdown logic itself isn't exposed on the bus — we observe compressor state but not the timer.

## Documented error codes (from manual, page 24–25)

| Code | Trigger condition                              | Manual behavior                                              |
|------|------------------------------------------------|--------------------------------------------------------------|
| E1   | Temperature **and humidity** sensor failure    | "Check or replace" — implementation may emit E1 for either subsystem failing, or be combined |
| E2   | Coil sensor failure                            | "Check or replace"                                           |
| CL   | Ambient temp Tr < 36°F (≈2°C)                  | Compressor stops, fan delay (F1 default 0 → 5s); resumes at 39°F |
| CH   | Ambient temp Tr > 113°F (45°C)                 | Compressor stops, fan delay; resumes at 108°F                |
| LO   | Ambient RH Hr < 20%                            | Compressor stops, fan delay; resumes >20%                    |
| HI   | Ambient RH Hr > 95%                            | Compressor and fan **continue running normally**; display "H" until RH <95% |

**Important: no FULL / water-full code exists.** User leak-tested by plugging the drain; unit ran continuously with no alarm. The WF connector noted in CLAUDE.md is unpopulated or unused on this 125-pint commercial SKU (designed for continuous-drain operation only).

### HI alarm — panel-display-only, NOT in protocol (confirmed capture #15)

Watched RH at 95–97% for sustained periods (capture #15) — slot 7 and slot 6 both stayed unchanged (`0x0000` and `0x8A40`). The mainboard does not expose HI on the Modbus status word. Consistent with the manual: HI is informational ("compressor and fan run normally") so there's no operational reason to surface it on the bus.

**For HA:** no dedicated `binary_sensor.error_hi_high_humidity` is needed. Build any "high humidity" alert in HA off `sensor.current_humidity > 95` directly.

### Error-frame encoding (confirmed via captures #09 + #11)

Slot 7 is a **bit-field error word**, split into halves:

- **High byte = ambient-sensor / HUM-side faults** (the 4-pin combined RH+temp digital sensor)
  - Bit 15 (`0x8000`) = HUM (RH) channel fault
  - Bit 14 (`0x4000`) = HUM (temp) channel fault
  - Both bits fire together when the combined HUM sensor fails or reads invalid (since one chip supplies both channels). Hence `0xC000` seen in:
    - Capture #09 CH (HUM heated → temp >45°C, panel showed "CH")
    - Capture #11 (HUM unplugged, panel showed "LO" then "E1")
  - The panel applies its own context-aware logic to display CH vs LO vs E1 vs HI from this same word. At the bus level we can only differentiate them by examining slots 1 (RH) and 2 (Ta) alongside slot 7.
- **Low byte = coil-side / T1-PT1 NTC faults**
  - Bit 7 (`0x0080`) = coil sensor fault. Captured in #11 when the 2-pin coil NTC was unplugged → coil reading went haywire → `s7 = 0x0080`.
- Other bits in slot 7 not yet seen; reserved for capture.

### Slot 6 bit 12 — refined ("latched alarm")

| Capture | s7 | s6 bit 12 | Behavior |
|---------|------|------------|---------|
| #09 CH  | `0xC000` lingered after temp recovered | **set** (`0x9840`) | Alarm latched — needs power-cycle / manual reset |
| #11 LO + E2 | `0xC000` and `0x0080` cleared the instant sensor was plugged back | **clear** (`0x8840`) | Transient — auto-clears with the underlying condition |

So bit 12 ≠ "slot 7 nonzero". It's specifically **alarm latched** — a hard-fault marker that survives the underlying condition returning to normal. Useful for HA: surface latched alarms prominently (need attention), transient ones can be quieter.

### Write commands (panel → main) — generalized

```
01 10 [ADDR_HI] [ADDR_LO] 00 01 02 [VAL_HI] [VAL_LO] [CRC_LO] [CRC_HI]
```
- `01` slave addr
- `10` function = Write Multiple Registers (`0x10` = 16)
- `[ADDR_HI][ADDR_LO]` start register address (see table)
- `00 01` quantity = 1 register
- `02` byte count = 2
- `[VAL_HI][VAL_LO]` new value, big-endian
- `[CRC_LO][CRC_HI]` Modbus CRC-16 (poly `0xA001`, init `0xFFFF`)

#### Confirmed write registers

| Addr     | Function   | Observed values                   | Notes                                                                   |
|----------|------------|-----------------------------------|-------------------------------------------------------------------------|
| `0x0000` | POWER      | `0` = off, `1` = on               |                                                                          |
| `0x0001` | SETPOINT   | Raw RH% (saw 30…60)               | Real range likely 30–80                                                  |
| `0x0004` | TIMER      | `0`…`24` (hours)                   | Hours of run time. `0` = timer disabled (confirmed via ▼ walk in capture #19). Captures #16, #17, #19. **No countdown readback exposed on the bus** — panel/mainboard tracks remaining time internally. HA must track set time + clock locally if it wants remaining-time UX. |
| `0x0005` | MODE       | `1`=sleep, `2`=continuous, `3`=auto | Sleep auto-bumps setpoint to 60                                          |

### Write response (main → panel) — non-standard

```
01 0A [ADDR_HI] [ADDR_LO] 00 01 [CRC_LO] [CRC_HI]
```

**ACK function byte = `0x0A`, not `0x10`.** The slave does not echo the request function code; it always replies with `0x0A`. This is non-compliant Modbus but consistent across all observed ACKs (writes to addresses 0, 1, and 5). When the ESP issues its own write, expect `0x0A` in the reply.

Observed ACKs:
- Write addr 0 ACK: `01 0A 00 00 00 01 58 0B`
- Write addr 1 ACK: `01 0A 00 01 00 01 09 CB`
- Write addr 5 ACK: `01 0A 00 05 00 01 48 0A`

## Open questions
- **Decode reg 6 (`0xC040`) bit layout.** Currently sits at `1100 0000 0100 0000`. Bits 14, 15, and 6 are set. Likely encodes mode (dehumidify / dry-clothes / continuous), power-on, compressor on/off, pump on/off, water-full, defrost. Capture mode-button cycle (POWER off→on, then MODE cycle) to map bits.
- **Confirm temperature units / sensor identity for slots 2 and 4** (29 and 28 — both plausible °C but 1° apart). Power off + cold-down or DMM at sensor wires could confirm.
- **Find the MODE / TIMER / POWER write addresses.** Capture each of K1 (POWER), K2 (MODE), K3 (TIMER) presses — write addr will reveal which register controls each function.
- **Quirk: master asks for 12 regs, slave returns 9.** Still unconfirmed whether the slave silently truncates *all* read responses to 9 regs regardless of requested quantity, or whether the master's quantity field is being misinterpreted. Try asking for a different quantity once we can drive the bus ourselves.

## Findings vs CLAUDE.md
- **Architecture A (panel owns setpoint) vs B** — leans **A**: panel sends Modbus writes (TBD captures will confirm). Setpoint will live in a register the panel writes to via fn 06/10.
- **Push vs poll** — **POLL**. Panel-master, mainboard-slave. Mainboard sends no unsolicited traffic.
- **Heartbeat** — the poll IS the heartbeat. If we run a fully emulated panel, we must keep the polling cadence so the mainboard sees liveness.
- **Tuya 0x55AA framing** — confirmed absent. Modbus RTU instead.
- **CLAUDE.md called the IC6/CN11 RS485 footprint a dead factory option.** Still true — but only the physical RS485 transceiver layer was killed. The protocol itself IS Modbus RTU; this SKU runs Modbus RTU over plain 5V TTL UART instead of over RS485. So the dehumidifier mainboard firmware is full Modbus-capable; only the line-driver chip is missing.

## Open issues / next steps

### 2026-06-05 — wrong-pin silence (resolved)
First 3 captures silent because YAML rx_pin was GPIO6 (and later GPIO5) — actual physical wires are on the board's RX/TX pins = **GPIO44 (U0RXD) / GPIO43 (U0TXD)**. Move from #4 forward.

### Next captures
1. ~~Setpoint, sensor perturbation, mode cycle, power off/on, compressor turn-on:~~ all done.
2. ~~FULL alarm:~~ N/A — no FULL code exists on this SKU.
3. **E1 capture: unplug HUM sensor.** Long capture (≥5 min). Mid-capture, unplug the 4-pin HUM connector on the mainboard. Watch slot 7/8 populate and slot 6 high byte for a new bit. Plug back in and capture the recovery transition.
4. **E2 capture: unplug coil NTC.** Same approach. Compare slot 7/8 to E1 — confirms encoding scheme (numeric vs ASCII-packed, single slot vs split).
5. **HI capture: breathe on / mist HUM sensor.** Get RH > 95%. HI is special — compressor keeps running per manual. Useful to distinguish "display-only" error bits from "compressor-stop" error bits.
6. **LO/CL/CH captures:** harder to trigger (need ambient temperature extremes for CL/CH, dry air for LO). Skip unless straightforward — extrapolate the encoding from E1/E2/HI.
7. **DEFROST capture (lower priority).** Manual makes no mention of a defrost mode visible to user — may be silent firmware behavior with no panel indication. Long-running compressor session will still be informative.
8. **TIMER capture.** Press K3 (TIMER) and adjust with ▲/▼. Expect a new write address (not 0/1/5) — likely `0x0002`, `0x0003`, `0x0004`, or `0x0006`. Watch slots 3, 5, 7, 8 in the response to see if any of them holds the countdown.
9. **POWER OFF while compressor running.** Captured power-off was while compressor was already off. Power-off mid-run might show a richer state diff (does bit 9 clear before bit 15? interesting ordering signal).

### Control path notes (forward-looking)
- To set the setpoint from the ESP, send the same frame as the panel: `01 10 00 01 00 01 02 [HH] [LL] [CRC]`. CRC is standard Modbus CRC-16 (poly `0xA001`, init `0xFFFF`).
- **Three strategies on the table (in increasing cleanliness):**
  - **Time-multiplexed co-talk** on the shared panel-port bus. Real panel stays plugged; ESP squeezes its writes into the ~3-second quiet window between panel polls. Needs a BSS138 level shifter to drive the 5V bus, and retry logic for the small simultaneous-write collision window.
  - **Full emulation** — panel disconnected, ESP becomes sole master. No bus contention, but loses the panel as a backup UI.
  - **Separate-channel co-talk (preferred if available):** ESP on the alternate 4-pin / WiFi-module header, panel stays on its own port. Each has its own UART channel to IC4; the mainboard arbitrates writes from both internally. No timing dance needed. **Depends on confirming via continuity test that the alternate port is a separate UART, not a parallel of the panel UART.**

### Alternate ports — partially characterised (2026-06-06)

CLAUDE.md identified one live panel port + multiple unpopulated alt-panel footprints. User clarified 2026-06-05 that there are **two distinct 4-pin headers**, plus a separate **WiFi-module header** of TBD pin count.

**Continuity + Saleae test results (2026-06-06):**

- **Second 4-pin header (task #15):** paralleled to the live panel port. TX/RX rings directly through. Same UART, same Modbus channel. Just two physical mounting positions for the same wires. Moving ESP here gains nothing functional.
- **6-pin header CN10 (originally assumed to be a WiFi header):** **NOT a WiFi UART.** Saleae capture across all 6 pins of CN10 showed zero signal activity in idle. Per CLAUDE.md, CN10 is the **main-MCU ISP/debug header** — quiescent unless a programmer is actively driving it. Re-interpreting prior readings:
  - The DMM "jumping at 5V" on pin 1 was capacitive coupling, not transmission.
  - Pin 2's ~1.5V was a programming-mode bias (NRST/BOOT0 pull network), not a UART signal.
  - The "frames" we saw on ESP UART when tapped to pin 2 were spurious start-bit mis-detections from a noisy biased input — not real data.
- User also confirmed: **no "WiFi" silkscreen anywhere on the board.** The only other unpopulated connectors are DISP1 + DISP2 — both alt-panel footprints. Corrected pin counts (overrides CLAUDE.md):
  - **CN5 / DISP2 = 6-pin** (CLAUDE.md had this as 7-pin)
  - **CN6 / DISP1 = 7-pin** (CLAUDE.md had this as 6-pin)
- **CN6 was further characterised via Saleae 2026-06-06 (task #26): it is SPI**, not paralleled to the panel UART. Pin trace shows top 3 pins routed through 100Ω resistors (= MOSI / MISO / SCK), 4th pin through 10kΩ (= CS), remaining pins are 5V / 12V / GND. SPI decoder confirms continuous chip-presence polls from MCU with MISO reading `0x00` everywhere — slave (the touch-LCD module) is not installed on this segment-LCD SKU. CN6 has no useful data flow and cannot be used for control on this SKU. (Even on the touch-LCD SKU, the touch-controller protocol would need reverse engineering with no benefit over Modbus.)
- CN5 still untested but presumed paralleled to panel UART (same alt-panel footprint family as the second 4-pin which was confirmed paralleled).

CLAUDE.md was right all along: "No WiFi-module silicon found on the mainboard." This SKU only has one UART for control: the panel UART.

### Phase 2 architecture decision (final): 2a — time-multiplexed co-talk on the panel UART

Phase 2c is unavailable because the dedicated UART doesn't exist on this hardware.

- **2a — time-multiplexed co-talk on the panel UART** ✅ *selected*
  - 74HCT1G125W as TX level shifter, OE gated by a free ESP GPIO
  - ESP listens continuously, injects writes during the ~3-second quiet window between panel polls
  - Retry logic for the rare collision when the user is pressing a panel button at the same moment the ESP transmits
  - Panel stays installed as a backup UI
- **2b — full panel emulation** (panel removed) — still viable as a backup if 2a runs into bus-arbitration issues in practice. Loses the panel as backup UI.
- ~~2c — dedicated WiFi-UART channel~~ — **does not exist on this SKU.**

## Recommended Home Assistant entity surface

Based on what we've decoded, here's what the ESPHome component should expose. Anything marked **(TBD)** depends on captures still pending (FULL, DEFROST, TIMER).

### Read-only sensors
| Platform | ID | Source | Unit | Purpose |
|----------|----|--------|------|---------|
| `sensor` | `current_humidity` | slot 1 | % | Live ambient RH measured by mainboard HUM sensor |
| `sensor` | `target_humidity` | slot 0 | % | Current setpoint (panel-driven; reflects user/panel changes) |
| `sensor` | `ambient_temperature` | slot 2 | °C | Room temp (mainboard T1/PT1 sensor). Add a template sensor for °F if desired |
| `sensor` | `coil_temperature` | slot 4 | °C | Evaporator coil temp — drops sharply when refrigerating. Best at-a-glance diagnostic |
| `sensor` | `status_word` | slot 6 raw | — | Debug/diagnostic (hex). Hide from default dashboards |
| `text_sensor` | `mode` | slot 6 low byte decode | — | "Auto" / "Continuous" / "Sleep" |
| `text_sensor` | `status` | derived from bits | — | "Off" / "Idle" / "Running" / "Locked out" / "E1" / "E2" / "CL" / "CH" / "LO" / "HI" — single-string status for cards |
| `text_sensor` | `error_code` | slot 7 / 8 decode | — | Two-char error code: "" when none, otherwise "E1" / "E2" / "CL" / "CH" / "LO" / "HI" |

### Binary sensors
| Platform | ID | Source | Purpose |
|----------|----|--------|---------|
| `binary_sensor` | `power` | slot 6 bit 15 | Is the unit powered on (logical, not mains) |
| `binary_sensor` | `compressor_running` | slot 6 bit 9 | Is the compressor relay currently closed |
| `binary_sensor` | `compressor_allowed` | slot 6 bit 11 | False = unit has inhibited compressor (flood, overtemp protection, etc.) |
| `binary_sensor` | `calling_for_cooling` | !bit 14 | Is the unit demanding dehumidification |
| `binary_sensor` | `lockout` | `calling && !running && compressor_allowed` | Anti-short-cycle (3-min) blocking compressor start |
| `binary_sensor` | `flood` | slot 6 bit 0 | External flood / leak contact closed. While true, compressor is inhibited. |
| `binary_sensor` | `alarm_latched` | slot 6 bit 12 | A latched safety alarm requires manual reset / power-cycle (e.g. sustained CH) |
| `binary_sensor` | `hum_sensor_fault` | slot 7 high byte != 0 | HUM-side fault present (covers CH/LO/E1 — disambiguate via Ta/RH values) |
| `binary_sensor` | `coil_sensor_fault` | slot 7 & 0x0080 | Coil NTC failure (E2) |

(`water_full` and `defrost` removed — neither exists on this SKU. `flood` is a separate external-contact safety input.)

### Controls (writes)
| Platform | ID | Target | Notes |
|----------|----|--------|-------|
| `switch` | `power` | write addr `0x0000` (vals 0/1) | Master on/off |
| `number` | `target_humidity` | write addr `0x0001` (raw RH%) | Min 30, max 80 (range to confirm); step 1; unit % |
| `select` | `mode` | write addr `0x0005` (vals 1/2/3) | Options: "Sleep", "Continuous", "Auto". Selecting "Sleep" will auto-bump setpoint to 60 on the mainboard side — UI should reflect that the setpoint number entity will change after a sleep selection |
| ~~`number` | `timer_hours`~~ | ~~addr `0x0004`~~ | **Decision: skip.** HA can do "run for N hours then shut off" trivially with an automation calling `switch.power`. The unit-side timer has no countdown register on the bus (we can't show remaining), and the panel UX has a misleading "cancel" gesture that doesn't actually clear it. Net: more confusion than value. The write address is documented above for reference if a future user wants to re-add it. |
| `button` | `panel_button_power/mode/timer/up/down` | (optional) — emulate panel button presses if we keep the panel disconnected | Useful for testing equivalence to the real panel UX |

### HA events (logbook / automation triggers, no entity)

When the user pushes buttons on the physical panel, the firmware sees the write frames and can emit corresponding HA events. These show up in the logbook automatically and can drive automations without requiring dedicated entities.

| Event | Trigger (bus frame) | Payload |
|-------|---------------------|---------|
| `esphome.dehumidifier_panel_timer_set` | Write to addr `0x0004`, value > 0 | `{hours: N}` — e.g. "Control panel set timeout 3 hours" |
| `esphome.dehumidifier_panel_timer_cancel` | Write to addr `0x0004`, value 0 | `{}` — "Control panel cancelled timeout" |
| `esphome.dehumidifier_panel_mode_change` | Write to addr `0x0005` | `{mode: "auto" | "continuous" | "sleep"}` |
| `esphome.dehumidifier_panel_setpoint_change` | Write to addr `0x0001` | `{value: N}` — track manual setpoint changes |
| `esphome.dehumidifier_panel_power` | Write to addr `0x0000` | `{state: "on" | "off"}` |
| `esphome.dehumidifier_alarm_set` | Slot 7 transitions from 0 → non-zero | `{code: "E1" | "E2" | "CH" | "CL" | "LO", raw: 0xXXXX}` |
| `esphome.dehumidifier_alarm_clear` | Slot 7 transitions from non-zero → 0 | `{previous_code: "..."}` |
| `esphome.dehumidifier_compressor_start` | Slot 6 bit 9 rising edge | `{}` |
| `esphome.dehumidifier_compressor_stop` | Slot 6 bit 9 falling edge | `{run_duration_s: N}` (computed in firmware) |
| `esphome.dehumidifier_flood_active` | Slot 6 bit 0 rising edge | `{}` |
| `esphome.dehumidifier_flood_clear` | Slot 6 bit 0 falling edge | `{}` |

**Why events instead of entities for timer:** the entity would be write-only with no readback, and the panel UX already has a misleading "cancel" gesture. An event is the right shape: "this happened at this time, here's what was set." Users see it in the logbook ("Control Panel: Set timeout 3 hours" at 10:34 PM), no entity sprawl.

**ESPHome syntax** (when we build the component):

```yaml
on_modbus_write:
  - if:
      condition:
        lambda: 'return frame.address == 0x0004 && frame.value > 0;'
      then:
        - homeassistant.event:
            event: esphome.dehumidifier_panel_timer_set
            data:
              hours: !lambda 'return frame.value;'
```

(`on_modbus_write` is a trigger the custom component will provide.)

### Climate / humidifier composite (optional)

The clean way to surface this in HA is a single **`humidifier`** entity (HA platform, not ESPHome) wrapping the `switch.power` + `number.target_humidity` + `select.mode` + `sensor.current_humidity`. ESPHome doesn't have a built-in humidifier platform, so two options:
- Expose the underlying entities and let HA build the humidifier via `humidifier:` integration in `configuration.yaml`.
- Write a custom `climate` component in C++ that pretends humidity is "temperature" — works but Mr. abuses the climate model. Stick with option 1.

### Diagnostic / housekeeping (cosmetic but nice)
| Platform | ID | Purpose |
|----------|----|---------|
| `sensor` | `wifi_signal_db` | builtin, debugging |
| `sensor` | `uptime` | builtin, restart detection |
| `binary_sensor` | `panel_present` | derived from "have we seen a poll in the last N seconds?" |
| `sensor` | `bus_polls_per_minute` | rate counter — sanity check that the panel is alive and responding |
| `sensor` | `last_bus_error_count` | runt frames, CRC mismatches — health signal |
| `button` | `safe_mode_reboot` | builtin |

## Open issues / next steps

### 2026-06-05 — empty idle capture
First 60s capture at 9600 8N1 on GPIO6 produced no UART bytes through the debug hook. Boot banner + WiFi + API normal; the `sequence: lambda` never fires, which means no bytes arrived in the RX buffer.

Most likely cause = the physical tap isn't in place yet (CLAUDE.md gates captures behind the continuity test). Before retrying capture, confirm:

1. **Continuity test done?** Has the live-port `5V/GND/TX/RX` been buzzed against the unpopulated DISP1/DISP2 port pads to confirm a shared UART? Without that map, the tap point is a guess.
2. **Physical wiring:** mainboard panel-port-TX → ESP GPIO6 through a level shifter / series resistor, GNDs commoned, ESP TX left floating, ESP 5V left floating (don't back-feed).
3. **Pin verification:** GPIO6 is the configured RX. Multimeter the wire end-to-end.

If the tap *is* in place and we still see nothing, the next experiments are:
- Drop baud to 2400/4800 in case 9600 is wrong AND framing breaks our `after:` trigger (timeout still 50ms — should still fire even on garbage, but worth ruling out).
- Temporarily remove `after.delimiter: "\n"` so only the 50ms idle timeout flushes (eliminates any chance the delimiter logic is suppressing flushes).
- Add a `uart.write` echo from a known source (loopback ESP TX→ESP RX) to confirm the debug hook is functional end-to-end.
