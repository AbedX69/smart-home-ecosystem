# ESP32 Smart Home Ecosystem

A complete smart home automation system built from scratch using ESP32 microcontrollers. Features distributed room controllers, intelligent lighting, garage automation, and multi-room audio - all communicating through a custom protocol.

## Project Vision

Building a fully integrated smart home wireless ecosystem with centralized and distributed control interfaces, combining hardware and software to create a seamless automation experience.

### Software Components
- **Web Interface** - React-based control dashboard(planned)
- **Mobile App** - Native mobile control (planned)
- **Voice Integration** - Siri/Alexa/Google Assistant support (planned)

### Hardware Devices

**Controllers:**
1. **Main Controller** - Central hub with a touch screens, multiple knobs/buttons/mini screens for whole-house control and device configuration
2. **Room Sub-Controllers** - Compact per-room interfaces with same UI concept, scaled down for single-room control

**Smart Devices:**
3. **Smart Lights** - WiFi-controlled dimmable RGB lighting
4. **Garage Door Controller** - Remote garage door operation with status monitoring
5. **Smart Sockets** - Remote control outlets
6. **Intercom/Doorbell Camera** - Video doorbell with two-way communication
7. **Multi-Room Audio System** - Sonos-style distributed audio

## Current Focus - Phase 1

🎯 **Active Development:**
- Room Sub-Controller (hardware + firmware)
- Smart Lights
- Garage Door Controller
- React web interface
- Communication protocol between devices

**Why this order?** The sub-controller serves as both a standalone product AND a milestone toward the main controller. Once it works with lights and garage door, the core concept system is proven.

## Current Status

🟢 **Complete:**
- ESP32 development environment (PlatformIO + VS Code)
- Multi-machine workflow (main PC → GitHub → test PC)
- Hardware components received (ESP32s, sensors, displays, LEDs, relays)
- Basic ESP32 test (LED blink verified)

🟡 **In Progress:**
- Sub-controller 
- Communication protocol definition
- Repository documentation
- Smart Lights
- Garage Door Controller
- React web interface


⚪ **Planned:**
- Main controller (after sub-controller works)
- Smart sockets
- Intercom/camera system  
- Audio system
- Mobile app
- Voice assistant integration

## Hardware Components

### Room Sub-Controller
- ESP32
- Rotary encoders with push buttons
- Small OLED/LCD displays
- RGB status LEDs
- Minimalist and Compact enclosure design

### Main Controller (Future)
- ESP32
- Touch screen display
- Small OLED/LCD displays
- More knobs/buttons for whole-house control
- Configuration interface for adding/removing devices
- Room assignment and management features

### Smart Lights
- ESP32-C3 (compact form factor)
- WS2812B addressable LED strips
- Power supply circuits
- Dimming and color control

### Garage Door Controller
- ESP32
- Relay module for motor control
- Hall effect sensors for position detection
- Safety interlock circuits
- Manual override support

### Smart Sockets (Planned)
- ESP32-C3
- Relay for power switching
- Power monitoring 

### Intercom/Doorbell Camera (Planned)
- ESP32
- Video streaming capability
- Two-way audio
- Motion detection
- Storage options

### Multi-Room Audio System (Planned)
- ESP32 with I2S audio
- Amplifier modules
- Synchronized playback across rooms
- Streaming protocol (Airplay/Spotify Connect style)

## System Architecture

**Communication Plan:**
- WiFi network for all devices
- Custom protocol for device-to-device messaging (MQTT/HTTP/WebSocket - TBD)
- React web dashboard for control and monitoring
- REST API for configuration
- WebSocket for real-time updates

**Controller Hierarchy:**
- Main Controller: Whole-house view, configuration, device management
- Sub-Controllers: Room-specific control, same codebase as main (scaled down)
- Smart Devices: Receive commands, report status

*[Architecture diagram coming soon]*

## Development Setup

### Prerequisites
- **PlatformIO** (VS Code extension)
- **Node.js** and npm (for React web interface)
- **Python 3.14.2**
- **Git**
-**Esp32 drivers(CH340/CP2102)**

### Hardware Setup
- ESP32 development boards
- USB cables for flashing
- Breadboards for prototyping
- Component kit (resistors, LEDs, sensors)

### Getting Started

```bash
# Clone the repository
git clone https://github.com/AbedX69/smart-home-ecosystem.git
cd smart-home-ecosystem

# Flash firmware to ESP32
cd firmware/test-blink
pio run -t upload

# Run web interface (when ready)
cd web-interface
npm install
npm start
```

## Tech Stack

**Firmware:**
- Framework: Arduino/ESP-IDF
- Language: C++
- IDE: VS Code + PlatformIO
- Version Control: Git/GitHub

**Web Interface:**
- Framework: React
- Language: JavaScript/TypeScript (TBD)
- Build Tool: Vite/Create React App (TBD)

**Communication:**
- Protocols: WiFi, MQTT/HTTP/WebSocket (evaluating options)
- APIs: REST for configuration, WebSocket for real-time

## Project Structure

```
smart-home-ecosystem/
├── firmware/
│   ├── testing/               # Active development and experiments
│   │   └── test-blink/        # Basic ESP32 tests
│   ├── production/            # Stable, working firmware
│   │   ├── sub-controller/    # Room controller firmware
│   │   ├── main-controller/   # House hub firmware
│   │   ├── smart-light/       # Light bulb firmware
│   │   ├── garage-controller/ # Garage door firmware
│   │   ├── smart-socket/      # Socket firmware
│   │   ├── intercom/          # Doorbell/camera firmware
│   │   └── audio-system/      # Multi-room audio firmware
│   └── shared/                # Common libraries used across devices
│       ├── wifi/              # WiFi connection handling
│       ├── communication/     # Device-to-device messaging
│       ├── display/           # Screen rendering functions
│       └── config/            # Settings storage (EEPROM/SPIFFS)
├── web-interface/             # React control dashboard (future)
│   ├── src/                   # React source code
│   └── public/                # Static assets
├── mobile-app/                # Mobile app (future)
├── docs/
│   ├── diagrams/              # Architecture & wiring diagrams
│   ├── datasheets/            # Component datasheets
│   └── protocols/             # Communication protocol specs
├── hardware/
│   ├── photos/                # Build progress photos
│   ├── schematics/            # Circuit diagrams
│   └── 3d-models/             # Enclosure designs (future)
├── enclosure/                 # 3D printing files for cases
└── youtube-recordings/        # Video content (not in git)
```

## Development Philosophy

**Code Reusability:**
- Main and sub-controllers share the same codebase/classes
- Sub-controller is essentially main controller with fewer features enabled
- Build modular, scalable components from the start

**Iterative Development:**
- Get sub-controller working first (proves the concept)
- Scale up to main controller (add configuration features)
- Add remaining devices once core system is stable

**Learning-Focused:**
- This is a portfolio project demonstrating embedded systems skills
- Documentation is as important as working code





**Development Environment:**
- ESP32 PlatformIO setup requires specific USB drivers (CH340/CP2102)
- Automated workflow between machines saves significant time
- Git-based deployment allows code-on-main-PC, flash-on-test-PC workflow

**Hardware:**
- AliExpress components arrived in good condition
- Multiple ESP32 variants needed for different use cases

*(More to come as I build)*

## Roadmap

**Phase 1** (Current - Q1 2025)
- ✅ Development environment setup
- ✅ Hardware procurement
- 🔄 Sub-controller prototype
- 🔄 Smart light firmware
- 🔄 Garage controller firmware
- 🔄 Basic communication protocol
- 🔄 React web interface MVP

**Phase 2** (Future)
- Smart sockets
- Intercom/camera system
- Audio system
- Advanced automation rules
- Mobile app development

**Phase 3** (Future)
- Main controller development
- Multi-room coordination
- Configuration interface
- Voice assistant integration

## Why This Project?

**Technical Goals:**
- Learn embedded systems development (ESP32/C++)
- Understand IoT communication protocols
- Build real-time systems with hardware constraints
- Practice full-stack development (firmware + web + mobile)

**Portfolio Value:**
- Demonstrates end-to-end system design
- Shows ability to document technical work
- Proves I can ship working hardware products
- Relevant for embedded systems and IoT career opportunities

## Contributing

This is a personal learning project, but feedback and suggestions are welcome. Feel free to:
- Open issues for bugs or suggestions
- Share ideas for improvements
- Ask questions about the implementation

## License

MIT License - See LICENSE file for details

---

**Project Status:** Active development | Last updated: 17/01/2026

**Contact:** abedx69@gmail.com

**Note:** This project is actively under development. Documentation is updated regularly as features are implemented.
