# Raspberry Pi Pico USB Data Reader (`libusb`)

A C application to stream, parse, and log data from a Raspberry Pi Pico (or compatible USB CDC ACM device) directly to a CSV file on macOS and Linux using `libusb-1.0`.

## Features

- **Direct Hardware Bulk Transfers:** Utilizes `libusb-1.0` synchronous bulk endpoints (`0x02` OUT / `0x82` IN) for low-latency streaming.
- **CDC Line Control:** Configures DTR (Data Terminal Ready) and RTS (Ready to Send) control signals required by the Pico CDC ACM USB stack.
- **Cross-Platform:** Out-of-the-box support for both macOS (Apple Silicon & Intel) and Linux.
- **CSV Data Output:** Parses comma-separated floating-point data streams and saves formatted output to `data.csv`.
- **Robust Cleanup:** Handles graceful termination on `Ctrl+C` (`SIGINT`), issuing `stop\r` commands and resetting endpoints to ensure the device re-initializes cleanly without requiring physical re-plugging.

---

## Hardware & USB Endpoint Specs

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Vendor ID (VID)** | `0x2E8A` | Raspberry Pi Foundation |
| **Product ID (PID)** | `0x0009` | Raspberry Pi Pico (CDC Serial) |
| **Control Interface** | `0` | CDC Management / Control Line State |
| **Data Interface** | `1` | CDC Data |
| **Bulk OUT Endpoint** | `0x02` | Command transfers (Host → Pico) |
| **Bulk IN Endpoint** | `0x82` | Streaming data transfers (Pico → Host) |
| **Command Line Ending** | `\r` | Carriage Return (`CR`) |

---

## Requirements & Prerequisites

- **C Compiler:** `gcc` or `clang`
- **Build Tools:** `make`, `pkg-config`
- **Library:** `libusb-1.0`

### macOS Installation
Using Homebrew:
```bash
brew install libusb pkg-config
