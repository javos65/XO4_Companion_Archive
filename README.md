# MachXO4 Companion: Lattice Prompt Build Projects

## Introduction

This workspace contains a set of projects for the Lattice MachXO4 Companion Board Rev A (device LFMXO4-110HC-6BBG256I, speed grade 6, industrial). 

All projects were created with the Lattice Prompt approach:

- A written project prompt describes the target hardware, pin map, architecture and rules.
- Claude Code builds the design through the Lattice Prompt MCP tools (Propel SoC and IP configuration, firmware build, Radiant build, timing checks, programming).
- Each project records its decisions and deviations (design plan) and delivers a report. Where a prompt file exists, it can be used to recreate the project from scratch.

Common characteristics:

- Toolchain: Lattice Propel 2026.1.1 and Lattice Radiant 2026.1.1.
- Most projects use a RISC-V SoC (riscv_sm) with custom Verilog-2001 IP blocks and C firmware; one project is RTL-only.
- All I/O is LVCMOS33. The 12 MHz board clock (ball T9), the reset button PB1 (D10) and the FTDI UART (B11/A12) are shared by the projects.
- The programming step always requires explicit approval before the board is touched.

## Project overview

External hardware means anything beyond the Companion Board and its USB cable. On-board parts (accelerometer, serial flash, INA236 monitors, LEDs, FTDI bridge) are not counted as external.

| Project | Description | Design type | External hardware | Interface | What external hardware | Python result reader | Custom IP cores |
|---|---|---|---|---|---|---|---|
| MachXO4C_28RPI | DPI LCD driver, bouncing block (RTL only) | RTL only | Yes | HAT40 | Waveshare 2.8" DPI LCD (ST7701S) | No | None (RTL modules dpi666_axis, lcd_test) |
| MachXO4C_ACCEL | Accelerometer read-out with live 3D vector | RISC-V | No | - | - | Yes | fxls8971_i2c |
| MachXO4C_ARDRGB | Rolling ball on LED matrix, steered by accelerometer | RISC-V | Yes | Arduino MKR | Arduino MKR RGB Shield (7 x 12 APA102) | Yes | fxls8971_i2c, mkr_rgb_spi |
| MachXO4C_FLS | Serial flash read, write and erase over EFB SPI | RISC-V | No | - | - | No | IS25LP064A_efb |
| MachXO4C_INA236 | Four-rail power monitor with live PC plot | RISC-V | No | - | - | Yes | INA236_efb, timer_4test |
| MachXO4C_SENSHAT | Sense HAT sensors and joystick over I2C | RISC-V | Yes | HAT40 | Raspberry Pi Sense HAT V2 | Yes | SenseHat_efb |
| MachXO4C_TTS | Text-to-speech with PWM audio output | RISC-V | Yes | Arduino MKR | Adafruit speaker shield | No | tts_apb |
| MachXO4C_TTS_FFT | Speech with FFT spectrum on LEDs, tilt phrases, logo | RISC-V | Yes | Arduino MKR, PMOD | Arduino MKR RGB Shield, Adafruit speaker shield | No | fxls8971_i2c, mkr_rgb_spi, pwm_fft, tts_apb |

## Notes

- Each project folder is self-contained (constraints, custom IP, firmware, Radiant project, release or benchmark folders where applicable).
- Open items such as exact external part lists are given in the individual prompts and reports; this overview only summarises them.
- The overview was compiled from the prompts and reports present in the project folders. The DOCX and PDF reports of MachXO4C_ACCEL, MachXO4C_FLS, MachXO4C_INA236 and MachXO4C_TTS_FFT were not opened; their entries rely on the prompt files and project contents.
