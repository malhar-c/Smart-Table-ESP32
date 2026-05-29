# Smart Table ESP32

Converts a manual hand-crank standing desk into an automatic desk controlled via a physical button panel and Apple HomeKit (Siri / Home app).

---

## Hardware

| Component | Part |
|-----------|------|
| Microcontroller | ESP32 DoIt DevKit v1 |
| Stepper driver | TMC2208 |
| Stepper motor | NEMA (400 steps/rev, half-step) with 19.2:1 gearbox |
| Lower limit | Mechanical limit switch |
| Obstruction detection | Vibration sensor module |
| Controller display | SSD1306 128×64 OLED (I2C) |
| Buttons | 4× momentary push button (Up, Down, Sit, Stand) |

---

## Pin Reference

### TMC2208 Stepper Driver

| TMC2208 pin | ESP32 GPIO | Notes |
|-------------|-----------|-------|
| STEP | GPIO 2 | Step pulse |
| DIR | GPIO 4 | Direction |
| EN | GPIO 15 | Enable (active LOW) |
| VCC / LOGIC | 3.3 V | Logic supply |
| GND | GND | |
| VM | Motor PSU (32 V) | Motor power — do NOT connect to ESP32 |
| 1A / 1B / 2A / 2B | Stepper coils | Connect motor windings |

> UART configuration pins (MS1/MS2) on the TMC2208 should be set for half-step mode (check your driver's datasheet/jumpers).

---

### OLED Display (SSD1306, I2C)

| OLED pin | ESP32 GPIO |
|----------|-----------|
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| VCC | 3.3 V |
| GND | GND |

I2C address: `0x3C`

---

### Limit Switch (lower limit / home position)

| Wire | Connect to |
|------|-----------|
| Terminal A | GPIO 23 |
| Terminal B | GND |

Wired as normally-open (NO). Internal pull-up is enabled in firmware — switch pulls the pin LOW when triggered.

---

### Vibration Sensor (obstruction detection)

| Sensor pin | ESP32 GPIO |
|-----------|-----------|
| Signal (OUT) | GPIO 26 |
| VCC | 3.3 V |
| GND | GND |

---

### Push Buttons (controller panel)

All buttons are wired between the GPIO pin and GND. Internal pull-ups are enabled in firmware (active LOW).

| Button | ESP32 GPIO | Notes |
|--------|-----------|-------|
| Up ▲ | GPIO 27 | |
| Down ▼ | GPIO 25 | |
| Sit (Memory 1) | GPIO 33 | |
| Stand (Memory 2) | GPIO 32 | Avoid GPIO 16 — RX2 has boot-time quirks on ESP32 |

---

## Wiring Diagram (text)

```
ESP32 DevKit v1
┌─────────────────────────────┐
│ GPIO 2  ──────────────────── STEP  ┐
│ GPIO 4  ──────────────────── DIR   ├─ TMC2208
│ GPIO 15 ──────────────────── EN    ┘
│                                      ↕ (motor coils)
│ GPIO 21 (SDA) ────────────── SDA  ┐
│ GPIO 22 (SCL) ────────────── SCL  ├─ SSD1306 OLED
│ 3.3V ─────────────────────── VCC  ┘
│
│ GPIO 23 ──[LIMIT SWITCH]── GND
│ GPIO 26 ──── Signal ──── Vibration sensor
│
│ GPIO 27 ──[BTN UP   ]── GND
│ GPIO 25 ──[BTN DOWN ]── GND
│ GPIO 33 ──[BTN SIT  ]── GND
│ GPIO 32 ──[BTN STAND]── GND
└─────────────────────────────┘
```

---

## Button Behaviour

| Button | Short press | Long press (5 s) |
|--------|-------------|-----------------|
| Up ▲ | Desk moves up while held, stops on release | — |
| Down ▼ | Desk moves down while held, stops on release | — |
| Sit | Move to saved sit position | **Save** current position as sit memory |
| Stand | Move to saved stand position | **Save** current position as stand memory |
| Up + Down (both) | — | Trigger **automated homing** (desk drives down to limit switch) |

Memory positions survive power cycles (stored in ESP32 NVS flash).

**Display wake**: if the display is asleep, the first button press only wakes the screen — it does not execute the action. Press again to act.

---

## OLED Display

### Normal display

```
┌────────────────────────────┐
│ ▼                          │  ← direction arrow (only shown while moving)
│                            │
│                   1 1 0 . 4│  ← current height in cm, large 7-segment digits
│                            │
└────────────────────────────┘
```

The height is rendered as large programmatic 7-segment digits, right-aligned. The direction arrow (▲/▼) appears in the top-left corner only while the motor is running.

The display turns off automatically after **15 seconds** of inactivity to reduce burn-in.

### Homing countdown (hold Up + Down)

```
┌────────────────────────────┐
│   HOME?                    │
│                            │
│  [████████░░░░░░░░░░░░░]   │  ← progress bar fills over 5 s
└────────────────────────────┘
```

### Homing in progress

```
┌────────────────────────────┐
│        HOMING              │  ← centered
│         ...                │  ← animated dots cycle (. → .. → ...)
│       110.4 cm             │  ← current height, centered, updates live
└────────────────────────────┘
```

Desk drives down until the limit switch triggers, then position resets to 0. Press either Up or Down (after fully releasing both) to abort homing at any time.

### Save countdown (hold Sit or Stand)

```
┌────────────────────────────┐
│   SAVE SIT                 │  ← or SAVE STAND, centered
│                            │
│  [████████░░░░░░░░░░░░░]   │  ← progress bar fills over 5 s
└────────────────────────────┘
```

Release before the bar fills = cancel. Hold to completion = position saved, "SAVED!" flashes briefly.

### Obstruction detected

```
┌────────────────────────────┐
│        DESK                │  ← centered, blinking border
│       BLOCKED              │
└────────────────────────────┘
```

Border blinks for 3 seconds, then normal display resumes. If obstruction occurs during homing, homing is aborted and normal button control is restored immediately.

---

## Safety Features

- **Mechanical limit switch** — immediately cuts motor power when the desk reaches the lowest position, resets the step counter to zero.
- **Vibration / obstruction detection** — counts vibration pulses while the motor runs; if more than 10 pulses occur in one move (indicating a skip, jam, or obstruction) the motor is stopped and HomeKit is notified. Also aborts homing if triggered mid-sequence.
- **Hard upper limit** — firmware clamps all move targets to `STEPPER_MAX_STEPS` (221,760 steps = 29 crank turns). The desk physically cannot be commanded beyond this.
- **Button debounce** — releases shorter than 50 ms are treated as bounce and ignored.

---

## HomeKit

The desk appears in the Apple Home app as a **Window Covering** accessory named "Smart Desk". Default pairing code: **466-37-726**.

You can:

- Set an exact height via the slider (0–100 %)
- Ask Siri: *"Hey Siri, set the desk to 80%"*
- Include it in automations (e.g. raise at 9 am)

Position is kept in sync between physical buttons and the Home app. Moving the desk via HomeKit also wakes the OLED display.

---

## Firmware Configuration

All tunable values are `#define` constants at the top of [src/Standing_Desk.h](src/Standing_Desk.h).

| Constant | Default | Description |
|----------|---------|-------------|
| `DESK_HEIGHT_MIN_CM` | 62.0 | Tabletop height at home position (cm) |
| `DESK_HEIGHT_MAX_CM` | 127.0 | Tabletop height at max position (cm) |
| `TURNS_MAX_HEIGHT` | 29 | Crank turns from home to max height |
| `STEPPER_GEAR_RATIO` | 19.2 | Motor gearbox ratio |
| `MOTOR_STEPS_FULL` | 400 | Steps per revolution (half-step mode) |
| `STEPPER_SPEED_HZ` | 1600 | Normal running speed (Hz) |
| `STEPPER_ACCEL` | 1200 | Acceleration (Hz/s) |
| `OBSTACLE_SENSITIVITY` | 10 | Vibration pulses before obstruction stop |
| `LONG_PRESS_MS` | 5000 | Hold time (ms) to save a memory position |
| `HOMING_HOLD_MS` | 5000 | Hold time (ms) for Up+Down to trigger homing |
| `DISPLAY_SLEEP_MS` | 15000 | Inactivity timeout before display turns off (ms) |
| `OLED_ADDR` | 0x3C | I2C address of OLED module |

> **Important:** Measure your desk's actual tabletop-to-floor height at the lowest and highest positions and update `DESK_HEIGHT_MIN_CM` / `DESK_HEIGHT_MAX_CM` before flashing.

---

## First-Time Setup

1. Wire everything according to the pin reference above.
2. Update height constants in [src/Standing_Desk.h](src/Standing_Desk.h) to match your desk.
3. Flash firmware via PlatformIO (`upload_port = COM7` set in `platformio.ini` — change if needed).
4. On first power-on, manually push the desk **down** until the limit switch triggers — this homes the motor and sets position zero. Alternatively, hold **Up + Down** for 5 seconds to trigger automated homing.
5. In the Apple Home app, pair the accessory (default code: `466-37-726`).
6. Move desk to your preferred sitting height, then **long-press Sit** (5 s) to save it.
7. Move desk to your preferred standing height, then **long-press Stand** (5 s) to save it.

---

## Dependencies

| Library | Purpose |
|---------|---------|
| [HomeSpan](https://github.com/HomeSpan/HomeSpan) ^1.7.0 | Apple HomeKit HAP stack |
| [FastAccelStepper](https://github.com/gin66/FastAccelStepper) | Smooth stepper acceleration |
| Adafruit SSD1306 ^2.5.3 | OLED driver |
| Adafruit GFX ^1.11.1 | OLED graphics |
| Adafruit BusIO ^1.11.6 | I2C/SPI bus support |
