# PS5 Library Browser

A high-performance, native PS5 library manager and game browser inspired by **Itemzflow**, engineered in modern **C++20**.

`ps5-library-browser` provides an intuitive interface for managing local and network-mounted game libraries, inspecting title packages (Base Games, Add-on DLCs, Title Updates), and managing multi-item installation queues with full **ShadowMountPlus** integration.

---

## 🌟 Key Features

### 1. Game, DLC & Update Package Inspector
- **Header & Outside Info**: Displays game title, Title ID (`PPSA` / `CUSA`), cover art, base game size, primary format (`PKG`, `exFAT`, `FFPKG`), and total package footprint.
- **Three Dedicated Section Tabs**: Quick navigation between **Game**, **DLC**, and **Update** package sections.
- **Formatted & Highlighted Package Details**: Displays filename, human-readable size, highlighted file format badges (`[.PKG]`, `[.EXFAT]`, `[.FFPKG]`), version numbers, and live installation status.
- **Per-Item Install Actions**: Install individual base games, specific DLC packages, or individual title updates directly.
- **DLC "Install All" Pipeline**: One-touch batch installation of all DLC add-ons with cumulative progress, item-level status tracking, cancellation, and granular failure reporting.

### 2. Dual-Mode Library Architecture
- **Installed Games Mode**: Instant local launcher for installed applications on the PS5.
- **Library Browser Mode**: Browses local storage (`/hostapp`), custom directories, and remote NFS mounts.
- **ShadowMountPlus Feature Lock**: Browsing remote libraries is gated behind a live health check to the local **ShadowMountPlus** daemon (`127.0.0.1:10101`). If the daemon is absent, the user is presented with a clear lock dialog explaining that ShadowMount Plus is required.

### 3. Native Dual-View Carousel & Block Grid
- **Coverflow Carousel**: 3D-styled cover art carousel with real-time scaling, depth ordering, and smooth scrolling.
- **5-Column Block Grid**: High-density library grid view optimized for large game collections.
- **Format Filters & Live Search**: Filter entries instantly by format (`PKG`, `exFAT`, `FFPKG`, `Folder`) or search by title and Title ID.

### 4. DualSense Input & Prompts
- Native mapping for DualSense D-Pad and analog stick navigation with deadzone and auto-repeat support.
- Dynamic contextual button prompt bar (`△ Toggle Mode / Install All DLCs`, `Touchpad View`, `L1/R1 Filter / Tabs`, `□ Inspect`, `✕ Select / Install`, `○ Back`).
- Modal dialogs for connection recovery and lock explanations.

### 5. High-Speed Scanner & Persistent Cached Indexing
- Scans `/hostapp`, local folders, and network mounts.
- Decodes `sce_sys/param.json` categories (`gd` base game, `gp` patch, `ac` DLC) and title ID tags (`PPSA` / `CUSA`).
- Atomic JSON cache engine ensures zero-latency library startup.

---

## 📂 Architecture

```
ps5-library-browser/
├── .github/workflows/ci.yml         # Automated host build & unit test CI workflow
├── CMakeLists.txt                   # Cross-platform CMake configuration
├── include/ps5_library/
│   ├── daemon_client.hpp            # ShadowMountPlus daemon probe & feature gate
│   ├── game_metadata.hpp            # Metadata parser, categories, and models
│   ├── scanner.hpp                  # Recursive multi-source scanner
│   ├── index_cache.hpp              # Atomic persistent cache & query filters
│   ├── input.hpp                    # DualSense pad mapping & prompt model
│   ├── views.hpp                    # Coverflow & Block Grid layout calculations
│   ├── installer.hpp                # Install pipeline, delegates & batch engine
│   ├── inspector.hpp                # Package inspector state, formatting & tabs
│   └── ui_shell.hpp                 # Native UI shell, state machine & dialogs
├── src/
│   ├── daemon_client.cpp            # Socket client & lock manager
│   ├── game_metadata.cpp            # Param parser & Title ID heuristics
│   ├── scanner.cpp                  # Directory scanner & grouping
│   ├── index_cache.cpp              # Atomic cache implementation
│   ├── input.cpp                    # Input processing & repeat timer
│   ├── views.cpp                    # Coverflow & Grid card positioning
│   ├── installer.cpp                # Install task queue & DLC batch execution
│   ├── inspector.cpp                # Package formatting & inspector navigation
│   └── ui_shell.cpp                 # UI shell flow & input routing
├── tests/
│   ├── daemon_client_test.cpp       # Milestone 1 tests
│   ├── scanner_cache_test.cpp       # Milestone 2 tests
│   ├── ui_shell_test.cpp            # Milestone 3 tests
│   ├── inspector_installer_test.cpp # Milestone 4 tests
│   ├── ps5_installer_persistence_test.cpp # PS5 install delegate, persistence & recovery tests
│   └── install_retry_preflight_history_test.cpp # Retry, preflight & searchable history tests
└── README.md
```

---

## 🛠️ Building & Testing

### Prerequisites
- Modern C++20 compiler (`gcc` 10+, `clang` 11+, or MSVC 2019+)
- CMake 3.16+
- POSIX-compatible host or PS5 homebrew SDK environment

### Build Commands
```bash
# Clone the repository
git clone https://github.com/sweenyxsky-oss/ps5-library-browser.git
cd ps5-library-browser

# Configure and compile
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Execute test suite
ctest --test-dir build --output-on-failure
```

---

## 🛣️ Development Roadmap

- [x] **Milestone 1: Daemon Health Probe & Feature Lock Engine**
  - Socket HTTP client with non-blocking polling and timeout controls
  - Feature gating with mandatory prompt: *"ShadowMount Plus is required to unlock these features"*
  - Automated mock daemon test suite
- [x] **Milestone 2: High-Speed Game Scanner, Metadata Classifier & Atomic Index Cache**
  - Fast traversal of `/hostapp` and mounted paths
  - `sce_sys/param.json` parser classifying Base Games (`gd`), Updates (`gp`), and DLCs (`ac`)
  - Title ID validation (`PPSA`, `CUSA`, `LAPY`, `FAKE`)
  - Atomic JSON v2 cache with PID/timestamp temporary file write and atomic rename
- [x] **Milestone 3: Native Dual-Mode UI Engine & DualSense Navigation**
  - Mode toggle: Installed Games vs Library Browser
  - Gated remote browser requiring ShadowMountPlus daemon
  - Selectable views: Coverflow carousel and 5-column Block Grid
  - Format filters (PKG, exFAT, FFPKG, Folder) and live search
  - Contextual DualSense button prompt bar and input repeat handling
- [x] **Milestone 4: Game/DLC/Update Package Inspector & Multi-Installer**
  - Package Inspector with Game, DLC, and Update section tabs
  - Formatted package metadata with highlighted file extensions (`[.PKG]`, `[.EXFAT]`, `[.FFPKG]`) and human-readable sizes
  - Per-item install triggers for games, DLCs, and updates
  - "Install All DLCs" pipeline with progress callbacks, cancellation, and granular failure recovery
  - **PS5 Package Install Delegate & Capability Model**: Safe runtime probe checking kernel privileges and system service availability (`libSceAppInstUtil`); returns descriptive `UnsupportedPlatform` diagnostics and guidance on non-PS5 hosts
  - **Persistent Install Jobs Across Restarts**: Atomic JSON state tracking (`/tmp/ps5_library_install_jobs.json`) persisting queued/completed jobs; automatically restores `InProgress` tasks as `Interrupted` on restart
  - **Post-Install Library & Inspector Refresh**: Real-time promotion of item confidence and inspector state upon package installation completion
  - **Actionable Recovery Guidance**: Granular troubleshooting steps for every error code (unsupported platform, missing privileges, missing system services, out of storage, corrupted header, interrupted install)
  - **Install Preflight Validation**: Multi-stage verification inspecting file existence, readability, storage capacity against package size (with configurable safety margin), and header signature format (`.pkg` magic `\x7FPKG`/`PKG\0`, `.ffpkg`/`.exfat` header sizes); prevents corrupt package dispatches before calling target install delegates.
  - **Duplicate Prevention & Retry Workflow**: Preflight duplicate detection prevents queueing conflicting installs for already active/queued jobs, while dedicated retry actions preserve original task IDs, prior error codes/messages, retry counters, and timestamps.
  - **Searchable Install History & Diagnostics**: Full job diagnostic history recording submission/completion timestamps, package identifiers, failure reasons, retry provenance, and step-by-step remediation guidance; accessible via the UI Shell history dialog.
  - **CSV & JSON Install History Export**: Export complete diagnostic logs with timestamps, Title IDs, filenames, format, size/byte counts, outcomes, error details, recovery steps, and retry ancestry (`original_task_id`, `original_error_code`, `retry_count`) directly to file or string buffers.
  - **Granular History Filtering & Multi-Key Sorting**: Multi-criteria filtering by install outcome (`Completed`, `Failed`, `Interrupted`, `Cancelled`, `Queued`, `InProgress`), package format (`pkg`, `exfat`, `ffpkg`, `folder`), and ISO 8601 date ranges; sortable ascending or descending by timestamp or package filename.
  - **Retry Confirmation Modal with Live Preflight Verification**: Interactive UI modal surfacing prior failure diagnostics and re-executing live preflight checks to ensure errors are resolved before requeueing.
  - **Per-Check Preflight Summary**: Clear per-check diagnostic breakdown across File Readability, Package Metadata/Header, and Available Storage with safety margins before dispatching to the installer.
  - **Immediate Preflight Re-Check & Changed Results Diffing**: Automatically verifies package readability, format/metadata signatures, and storage capacity immediately before dispatch, detecting and reporting deviations from initial summary snapshots.
  - **Export File Picker & Configurable Filenames**: Interactive modal for selecting destination directories and filenames for CSV/JSON diagnostic logs with real-time feedback and remediation on failure.
  - **Cross-Session Filter & Sort Persistence**: Atomically preserves install history filter options (outcome, format, date ranges, query) and sort preferences across application restarts.
- [x] **Milestone 5: PS5 Payload Packaging & Cross-Compilation Workflow**
  - Dedicated CMake PS5 target (`ps5_library_browser_elf`) producing `ps5-library-browser.elf`
  - Target entrypoint (`src/main.cpp`) with DualSense event loop, headless verification, `/hostapp` default scanning, and daemon health probe
  - Homebrew metadata package manifest (`sce_sys/param.json`)
  - Automated PS5 cross-compilation CI workflow with `pacbrew-repo` and `ps5-payload-sdk` toolchain packaging
  - **Structured Startup Diagnostics**: Comprehensive boot-time health checks covering root scan mount availability, ShadowMountPlus daemon connectivity, network subsystem initialization, and persistence storage writeability.
  - **On-Screen Recovery Guidance**: Active diagnostic issues automatically trigger an interactive UI dialog with step-by-step remediation procedures and live diagnostics re-run capabilities.
  - **Automated Package & ELF Architecture CI Validation**: Dedicated release validator (`tools/validate_release_package.py`) enforcing archive integrity, required metadata (`sce_sys/param.json`), and 64-bit Little-Endian x86_64 ELF execution attributes.
  - **Tag-Triggered GitHub Release Publishing**: Automated release pipeline generating versioned GitHub Releases with verified release archives when a `v*` tag is pushed.

---

## 🚀 Installation & Setup on PS5

### Prerequisites
1. **PlayStation 5 Console**: Running compatible homebrew firmware (e.g. 3.xx–4.xx) with an active ELF loader or payload environment.
2. **[ShadowMountPlus](https://github.com/sweenyxsky-oss/ShadowMountPlus)**: Installed and running in the background. PS5 Library Browser requires the ShadowMountPlus local daemon (`127.0.0.1:10101`) to unlock remote NFS browsing and virtual mounting features.
3. **Network Connection**: PS5 connected to your local network with access to your NFS storage server (e.g., TrueNAS).

---

### Installation Methods

#### Method 1: Payload Injection via Netcat / ELF Loader
1. Download the latest release package (`ps5-library-browser_v*.zip`) from the [Releases](https://github.com/sweenyxsky-oss/ps5-library-browser/releases) page and extract it.
2. Launch the ELF loader on your PS5 (listening on port `9020` or `9021`).
3. Send `ps5-library-browser.elf` from your computer using `nc` (netcat) or any PS5 payload sender tool:
   ```bash
   # Linux / macOS
   nc -w 3 <PS5_IP_ADDRESS> 9020 < ps5-library-browser.elf

   # Windows (PowerShell with ncat or GUI payload sender)
   ncat -w 3 <PS5_IP_ADDRESS> 9020 < ps5-library-browser.elf
   ```

#### Method 2: Homebrew Directory Installation (FTP / Itemzflow)
1. Extract the release ZIP package containing `ps5-library-browser.elf` and the `sce_sys/` folder.
2. Connect to your PS5 via FTP (e.g., FTPS5 or Itemzflow built-in FTP server).
3. Transfer the extracted files to `/data/homebrew/ps5-library-browser/` or `/hostapp/`.
4. Launch the application from your PS5 homebrew menu (e.g., [Itemzflow](https://github.com/sweenyxsky-oss/Itemzflow) or [ProsperoMgr](https://github.com/sweenyxsky-oss/ProsperoMgr)).

---

### Startup & Controls Guide

On startup, the built-in **Diagnostics Engine** verifies the scan mount, network stack, cache files, and the ShadowMountPlus daemon. If ShadowMountPlus is offline, an on-screen recovery guidance dialog will appear explaining how to launch the daemon to unlock remote features.

| Button | Action |
| :--- | :--- |
| **Square** | Switch Mode: **Installed Games** ⇄ **Library Browser** |
| **Triangle** | Toggle View: **Coverflow Carousel** ⇄ **5-Column Block Grid** |
| **D-Pad / Left Stick** | Navigate Game Cards & Packages |
| **Cross** | Open Game / DLC / Update Inspector & Execute Install |
| **Circle** | Back / Close Dialog / Dismiss Guidance |
| **Options** | Open Filter & Sort History Modal |

---

## 🌟 Credits & Acknowledgments

This project is **inspired by [Itemzflow](https://github.com/LightningMods/Itemzflow) & [PS5 Library](https://github.com/victor-s-santos/ps5-library)**. We gratefully acknowledge and credit the creators and maintainers of the following open-source repositories and tools:

- **[Itemzflow](https://github.com/LightningMods/Itemzflow)** ([fork: sweenyxsky-oss/Itemzflow](https://github.com/sweenyxsky-oss/Itemzflow)) — by LightningMods & contributors. The iconic PS4/PS5 homebrew game manager and launcher that inspired this project's browsing, metadata organization, and package installation experience.
- **[ps5-library](https://github.com/victor-s-santos/ps5-library)** ([fork: sweenyxsky-oss/ps5-library](https://github.com/sweenyxsky-oss/ps5-library)) — by victor-s-santos. The foundation for game library scanning concepts, title ID resolution, and TrueNAS/NFS game repository browsing.
- **[ps5-homebrew-ui](https://github.com/victor-s-santos/ps5-homebrew-ui)** ([fork: sweenyxsky-oss/ps5-homebrew-ui](https://github.com/sweenyxsky-oss/ps5-homebrew-ui)) — by victor-s-santos. Modern native C++20 UI framework for PlayStation 5 homebrew, providing the architectural foundation for carousel, block-grid views, and controller navigation.
- **[ShadowMountPlus](https://github.com/Shadow-M0unt/ShadowMountPlus)** ([fork: sweenyxsky-oss/ShadowMountPlus](https://github.com/sweenyxsky-oss/ShadowMountPlus)) — Virtual mounting subsystem and background daemon (`127.0.0.1:10101`) providing remote NFS/network storage mounting and `sce_sys/param.json` metadata parsing.
- **[RemotePFS](https://github.com/flatz/RemotePFS)** ([fork: sweenyxsky-oss/RemotePFS](https://github.com/sweenyxsky-oss/RemotePFS)) — by flatz & contributors. Remote PFS mounting and network filesystem utilities for PlayStation 5.
- **[ps5-ezremote-client](https://github.com/G991/ps5-ezremote-client)** ([fork: sweenyxsky-oss/ps5-ezremote-client](https://github.com/sweenyxsky-oss/ps5-ezremote-client)) — by G991. Remote client utilities and network mounting integrations.
- **[libnfs](https://github.com/sahlberg/libnfs)** ([fork: sweenyxsky-oss/libnfs](https://github.com/sweenyxsky-oss/libnfs)) — by Ronnie Sahlberg. Robust userland client library for NFS network shares.
- **[fuse-nfs](https://github.com/sahlberg/fuse-nfs)** ([fork: sweenyxsky-oss/fuse-nfs](https://github.com/sweenyxsky-oss/fuse-nfs)) — by Ronnie Sahlberg. FUSE filesystem module for mounting NFSv3/4 shares.
- **[ProsperoMgr](https://github.com/idlesauce/ProsperoMgr)** ([fork: sweenyxsky-oss/ProsperoMgr](https://github.com/sweenyxsky-oss/ProsperoMgr)) — All-in-one manager for PlayStation 5 homebrew.
- **[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk)** & **[pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)** — Official PS5 homebrew SDK, toolchain wrappers (`prospero-clang++`), and runtime libraries for native ELF execution.
- **[JSON for Modern C++](https://github.com/nlohmann/json)** — by Niels Lohmann. Header-only JSON library used for atomic caching, param parsing, diagnostics reporting, and job persistence.

---

## 📜 License

This project is licensed under the **GNU General Public License v3.0**.

### Building & Cross-Compiling for PS5

#### Host Simulation & Testing (Linux / macOS)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/ps5-library-browser.elf --headless
```

#### PS5 Cross-Compilation (PS5 Payload SDK / Prospero)
```bash
cmake -B build-ps5 \
  -DCMAKE_TOOLCHAIN_FILE=/opt/ps5-payload-sdk/toolchain/prospero.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DPS5=ON
cmake --build build-ps5 --target ps5_library_browser_elf
```
