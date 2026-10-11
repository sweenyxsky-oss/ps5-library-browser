# PS5 Library Browser (`ps5-library-browser`)

[![CI Build & Test](https://github.com/sweenyxsky-oss/ps5-library-browser/actions/workflows/ci.yml/badge.svg)](https://github.com/sweenyxsky-oss/ps5-library-browser/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-PlayStation%205-003791.svg)](https://github.com/sweenyxsky-oss/ps5-payload-sdk)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

A clean, modern, native PS5 Game Library Browser & Launcher inspired by Itemzflow's frontend presentation, engineered from the ground up in modern C++20 for speed, stability, and clean architecture.

Designed specifically for remote network storage (NFS) and local drives, featuring instantaneous cached indexing, granular Game/DLC/Update inspectors, bulk package installers, and companion integration with the **ShadowMountPlus** daemon.

---

## 🚀 Key Features

### 1. Dual Mode Library View
- **Installed Games**: Fast view of games installed on internal/extended storage.
- **Library Browser**: Remote NFS / hostapp repository browser.
- Instant toggle between Installed and Remote modes with DualSense Triangle (`△`).
- Gated behind ShadowMountPlus health verification for remote browsing.

### 2. Dual Presentation Layouts
- **Scrolling Covers (Coverflow Carousel)**: Horizontal carousel with focused card enlargement, depth layering, and perspective falloff.
- **Blocks (Grid View)**: 5-column 2D grid layout with multi-axis DualSense navigation and viewport scroll tracking.
- Toggle layout instantly with Touchpad or Options button.

### 3. DualSense Controller Navigation & On-Screen Prompts
- Native mapping for DualSense D-Pad and analog stick navigation with deadzone and auto-repeat support.
- Dynamic contextual button prompt bar (`△ Toggle Mode`, `Touchpad View`, `L1/R1 Filter`, `□ Inspect`, `✕ Select`, `○ Back`).
- Modal dialogs for connection recovery and lock explanations.

### 4. High-Speed Scanner & Persistent Cached Indexing
- Scans `/hostapp`, local folders, and network mounts.
- Decodes `sce_sys/param.json` categories (`gd` base game, `gp` patch, `ac` DLC) and title ID tags (`PPSA` / `CUSA`).
- Atomic JSON cache engine ensures zero-latency library startup.

---

## 📂 Architecture

```
ps5-library-browser/
├── .github/workflows/ci.yml      # Automated host build & unit test CI workflow
├── CMakeLists.txt                # Cross-platform CMake configuration
├── include/ps5_library/
│   ├── daemon_client.hpp         # ShadowMountPlus daemon probe & feature gate
│   ├── game_metadata.hpp         # Metadata parser, categories, and models
│   ├── scanner.hpp               # Recursive multi-source scanner
│   ├── index_cache.hpp           # Atomic persistent cache & query filters
│   ├── input.hpp                 # DualSense pad mapping & prompt model
│   ├── views.hpp                 # Coverflow & Block Grid layout calculations
│   └── ui_shell.hpp              # Native UI shell, state machine & dialogs
├── src/
│   ├── daemon_client.cpp         # Socket client & lock manager
│   ├── game_metadata.cpp         # Param parser & Title ID heuristics
│   ├── scanner.cpp               # Directory scanner & grouping
│   ├── index_cache.cpp           # Atomic cache implementation
│   ├── input.cpp                 # Input processing & repeat timer
│   ├── views.cpp                 # Coverflow & Grid card positioning
│   └── ui_shell.cpp              # UI shell flow & input routing
├── tests/
│   ├── daemon_client_test.cpp    # Milestone 1 tests
│   ├── scanner_cache_test.cpp    # Milestone 2 tests
│   └── ui_shell_test.cpp         # Milestone 3 tests
└── README.md
```

---

## 🛣️ Development Roadmap

- [x] **Milestone 1: Daemon Health Probe & Feature Lock Engine**
- [x] **Milestone 2: High-Speed Game Scanner, Metadata Classifier & Atomic Index Cache**
- [x] **Milestone 3: Native Dual-Mode UI Engine & DualSense Navigation**
  - Mode toggle: Installed Games vs Library Browser
  - Gated remote browser requiring ShadowMountPlus daemon
  - Selectable views: Coverflow carousel and 5-column Block Grid
  - Format filters (PKG, exFAT, FFPKG, Folder) and live search
  - Contextual DualSense button prompt bar and input repeat handling
- [ ] **Milestone 4: Game Inspector & Multi-Installer**
- [ ] **Milestone 5: PS5 Payload Packaging & Hardware CI**

---

## 📜 License

This project is licensed under the **GNU General Public License v3.0**.
