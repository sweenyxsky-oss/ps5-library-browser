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
- **Installed Games**: Fast view of games already installed on internal/extended storage.
- **Library Browser**: Remote NFS / local network repository browser.
- One-button instant toggle between Installed and Remote library modes.

### 2. Dual Presentation Layouts
- **Scrolling Covers (Coverflow Carousel)**: Horizontal fluid carousel with game reflections and centered focus.
- **Blocks (Grid View)**: High-density cover grid for fast browsing across large game catalogs.
- Responsive DualSense D-Pad and analog stick navigation.

### 3. Deep Game Inspector & Package Breakdown
- High-resolution game cover and title banner.
- Title ID detection (`PPSAxxxxx` for PS5, `CUSAxxxxx` for PS4) and total calculated storage footprint.
- Highlighted format indicator (`.pkg`, `.exfat`, `.ffpkg`, etc.).
- Categorized action tabs:
  - **Game**: Base game package info, size, file name, and install action.
  - **DLC**: Individual DLC package list with per-item install buttons and a bulk **"Install All"** button.
  - **Update**: Title updates with patch version, release metadata, and update install button.

### 4. Storage & Path Engine
- **Default Path**: Pre-configured to `/hostapp`.
- **PS5 Local Folder Browser**: Built-in path picker to navigate and select directories on console storage.
- **NFS Network Mount**: Direct connection by providing IP address, remote export path, username, and password credentials.
- **Persistent Cached Indexing**: Generates an atomic, high-performance local index cache so large remote shares load instantaneously on startup without rescanning.

### 5. ShadowMountPlus Companion Lock
- Probes local daemon instance at `127.0.0.1:10101` (`/api/v1/version`).
- Features that depend on mounting, network installation, or daemon services are safely gated when ShadowMountPlus is not running.
- Displays the clear requirement prompt:
  > **"ShadowMount Plus is required to unlock these features"**
- Dynamically unlocks the moment a working ShadowMountPlus instance is detected in the background.

---

## 📂 Architecture

```
ps5-library-browser/
├── .github/
│   └── workflows/
│       └── ci.yml               # Automated host build & unit test CI workflow
├── CMakeLists.txt               # Cross-platform CMake build configuration
├── include/
│   └── ps5_library/
│       └── daemon_client.hpp    # ShadowMountPlus daemon probe, health check & feature lock
├── src/
│   └── daemon_client.cpp        # POSIX non-blocking socket client & lock engine
├── tests/
│   └── daemon_client_test.cpp   # Comprehensive Milestone 1 unit tests with mock HTTP server
├── external/
│   └── nlohmann/
│       └── json.hpp             # Modern JSON parser for API payloads and cache
├── LICENSE                      # GNU General Public License v3.0
└── README.md
```

---

## 🛣️ Development Roadmap

- [x] **Milestone 1: Daemon Health Probe & Feature Lock Engine**
  - Cross-platform non-blocking socket client for `127.0.0.1:10101`
  - Version & capability negotiation with ShadowMountPlus (`/api/v1/version`)
  - Feature gating engine with mandatory lock banner (`"ShadowMount Plus is required to unlock these features"`)
  - Background asynchronous monitor thread with state change callbacks
  - Host unit tests with simulated mock HTTP daemon and 100% test pass
  - Automated GitHub Actions CI workflow

- [ ] **Milestone 2: Multi-Source Scanner & Metadata Cache**
  - Portable C++ filesystem scanner supporting `/hostapp`, local folders, and NFS mounts
  - Classification engine: `sce_sys/param.json` parser distinguishing `gd` (Game), `gp` (Patch/Update), and `ac` (Add-on/DLC)
  - Title ID extraction (`PPSA` / `CUSA`) and cover art (`icon0.png` / `pic0.png`) locator
  - Atomic persistent JSON library cache for zero-delay startup

- [ ] **Milestone 3: Native Dual-Mode UI Engine**
  - Installed Games vs Library Browser toggle
  - Coverflow Carousel vs Block Grid view modes
  - DualSense controller input handling and smooth scrolling navigation

- [ ] **Milestone 4: Game Inspector & Multi-Installer**
  - Game, DLC, and Update detail breakdown with formatted file attributes
  - Single-DLC install and "Install All" batch installation pipeline
  - Storage path configuration (Local directory picker & NFS credentials)

- [ ] **Milestone 5: PS5 Payload Packaging & Hardware CI**
  - Integration with PS5 Payload SDK toolchain
  - Native ELF artifact build and deployment testing

---

## 🛠️ Building & Testing

### Host Linux Build & Verification
```bash
# Clone the repository
git clone https://github.com/sweenyxsky-oss/ps5-library-browser.git
cd ps5-library-browser

# Configure with CMake & Ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build core library and test suites
cmake --build build

# Run unit tests
ctest --test-dir build --output-on-failure
```

---

## 🤝 Credits & Acknowledgments

- **[Itemzflow](https://github.com/LightningMods/Itemzflow)** by [LightningMods](https://twitter.com/LightningMods_) & contributors for frontend inspiration and pioneering open-source PS4/PS5 homebrew menus.
- **[ShadowMountPlus](https://github.com/sweenyxsky-oss/ShadowMountPlus)** for the daemon API, mounting utilities, and background services.
- **[ps5-payload-sdk](https://github.com/ps5-payload-dev/pacbrew-repo)** & OpenOrbis for the PS5 toolchain and homebrew headers.
- **[nlohmann/json](https://github.com/nlohmann/json)** for C++ JSON serialization.

---

## 📜 License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.
