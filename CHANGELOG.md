# Changelog

All notable changes to WineASIO will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added

- **Loopback test** (`tests/loopback`): measures the round trip through WineASIO against a native
  JACK client over a JACK-internal loop, without audio hardware, for 64- and 32-bit hosts and each
  Output buffering setting; also reports `GetLatencies()`, swallowed cycles and `systemTime`.
  With PipeWire's JACK the loop goes through a relay client, since PipeWire passes no data over a
  link from a client to itself.

---

## [1.6.3] - 2026-10-08

### Changed

- **Settings panel**: sets `Output buffering` and lists the `WINEASIO_*` environment variables
  that override its values. Opened from the host, it saves through the `regedit` of the Wine
  installation that runs the host. The driver is unchanged from 1.6.2.

---

## [1.6.2] - 2026-10-08

### Fixed

- **Environment overrides**: the `WINEASIO_*` variables documented in the README are read after
  the registry and take precedence (before, only `WINEASIO_OUTPUT_BUFFERING` was read).
- **Autostart server**: read from the registry and the environment; when on, libjack starts a
  server that is not running.
- **Preferred buffer size**: a value that is not a power of two between 16 and 8192 becomes 1024,
  as upstream.

---

## [1.6.1] - 2026-10-08

### Fixed

- **`Fixed buffersize` defaults to on** when the registry has no value, so JACK owns the buffer
  size. Since 1.4.0 a host could pick its own size, and WineASIO asked JACK to switch the whole
  server to it.

---

## [1.6.0] - 2026-10-08

### Fixed

- **Synchronous buffer switch**: the host's output leaves in the same JACK cycle as its
  `bufferSwitch()`. Since 1.4.0 it reached JACK two periods later, and `GetLatencies()` did not
  include those periods. Round trip through a JACK-internal loop: 1 period instead of 3, the same
  as a native JACK client.
- **Time info mode**: `CreateBuffers()` asks `kAsioSupportsTimeInfo` and `kAsioSupportsTimeCode`,
  so hosts that support it get `bufferSwitchTimeInfo()`; the `ASIOTime` layout follows the SDK.
- **`systemTime`** is read from `CLOCK_BOOTTIME`, the clock behind Wine's `timeGetTime()`.

### Added

- **Output buffering** (`HKCU\Software\Wine\WineASIO\Output buffering` or
  `WINEASIO_OUTPUT_BUFFERING`): 0 (default) sends the output in the same cycle, 1 and 2 give the
  host one or two periods of headroom and report them in `GetLatencies()`.

---

## [1.5.0] - 2026-04-05

### Fixed

- Lock-free atomics instead of a mutex in the JACK realtime thread, eventfd signaling instead of
  `Sleep(1)` polling, a JACK shutdown callback, memory leaks in `DisposeBuffers()` and in the
  init error paths, null-pointer guards after `jack_port_register()`.
- `fork()` instead of `vfork()` for the settings panel, no `mlockall(MCL_FUTURE)` for the host
  process, 64-byte aligned buffers.
- `GetSamplePosition()` returns the high word (it was 0, so the position wrapped after about
  25 hours at 48 kHz); no double buffer switch priming in `Start()`.

---

## [1.4.4] - 2026-01-31

### Removed

- **JACK MIDI ports** - Removed unused MIDI infrastructure from WineASIO
  - The `WineASIO:midi_in` and `WineASIO:midi_out` JACK ports were non-functional placeholders
  - They were not connected to the Windows MIDI API, so DAWs could not use them
  - Wine's built-in `winealsa.drv` already provides full MIDI support via ALSA
  - Use `a2jmidid -e` to bridge ALSA MIDI devices to JACK for MIDI routing

### Changed

- **README.md**: Updated MIDI section to explain proper MIDI usage with Wine
  - Documented that Wine's `winealsa.drv` handles MIDI (not WineASIO)
  - Added instructions for using `a2jmidid` to bridge MIDI to JACK

---

## [1.4.3] - 2026-01-28

### Fixed

- Debug traces removed from the JACK realtime callback and the callback polling thread, where
  they caused xruns; debug output only with `-DWINEASIO_DEBUG`.

---

## [1.4.2] - 2026-01-21

### Fixed

- **CRITICAL: Wine 11 WoW64 32-bit support now fully working**
  - Discovered that in Wine 11 WoW64, 32-bit PE DLLs use 64-bit Unix libraries
  - Unix `.so` for 32-bit PE must be built with `-m64` (not `-m32`)
  - Unix `.so` for 32-bit PE must be installed to `x86_64-unix/` (not `i386-unix/`)
  - Audio buffers now allocated on PE side to solve address space mismatch

### Changed

- **Makefile.wine11**: Corrected build flags for WoW64 architecture
  - 32-bit PE DLL (`wineasio.dll`) built with `-m32`
  - Unix library for 32-bit PE (`wineasio.so`) built with `-m64`
  - Installation now correctly places `wineasio.so` in `x86_64-unix/`

- **asio_pe.c**: Audio buffer allocation moved to PE side
  - Buffers allocated using `HeapAlloc()` on Windows side
  - Fixes address space incompatibility between 32-bit PE and 64-bit Unix

- **asio_unix.c**: JACK callback updated to use PE-allocated buffers
  - Now uses `pe_buffer[]` instead of `audio_buffer`
  - Proper memory access across WoW64 boundary

### Added

- **Documentation**:
  - `docs/WINE11_WOW64_ARCHITECTURE.md` - Detailed architecture explanation
  - `docs/WINE11_WOW64_32BIT_SOLUTION.md` - Problem analysis and solution
  - `.rules` - Project rules and quick reference for developers

- **Test Programs**:
  - `test_asio_interactive.c` - Interactive test keeping JACK connection open
  - `test_asio_start.c` - Full ASIO pipeline test
  - `test_asio_thiscall.c` - Thiscall convention verification
  - `test_asio_extended.c` - Extended API testing
  - `TEST_PROGRAMS_README.md` - Documentation for test programs

### Tested

Successfully tested with:
- REAPER 32-bit
- REAPER 64-bit
- Garritan CFX Lite (32-bit)
- FL Studio 2025
- Custom test programs

### Technical Details

**Wine 11 WoW64 Architecture Discovery:**

```
┌─────────────────────────────────────────────────────────────┐
│ 32-bit PE DLL (wineasio.dll)                                │
│   - Runs in emulated 32-bit address space                   │
│   - Addresses: 0x00000000 - 0x7FFFFFFF                      │
└─────────────────────┬───────────────────────────────────────┘
                      │ __wine_unix_call() with WoW64 thunking
                      ▼
┌─────────────────────────────────────────────────────────────┐
│ 64-bit Unix .so (wineasio.so in x86_64-unix/)               │
│   - Runs in native 64-bit address space                     │
│   - MUST be built with -m64, NOT -m32!                      │
└─────────────────────────────────────────────────────────────┘
```

**Key insight:** In Wine 11 WoW64, there is NO `i386-unix/` directory for system libraries. All Unix libraries are 64-bit, even those serving 32-bit PE DLLs.

---

## [1.4.1] - 2026-01-20

### Added

- **test_asio_minimal.c** - Diagnostic tool to verify WineASIO works independently

### Changed

- Documentation cleanup - Removed temporary debug files

### Fixed

- Documentation accuracy - Updated all dates to 2026

---

## [1.4.0] - 2026-01-20

### Added

#### Wine 11 Support
- **Complete Wine 11 compatibility** with new PE/Unix split architecture
- New build system (`Makefile.wine11`) for Wine 10.2+ and Wine 11
- Separate PE (Windows) and Unix (Linux) code modules
  - `asio_pe.c` - PE-side COM/ASIO interface
  - `asio_unix.c` - Unix-side JACK integration
  - `unixlib.h` - Interface definitions
- Modern `__wine_unix_call` interface for PE↔Unix communication

#### JACK MIDI (removed in 1.4.3)
- ~~JACK MIDI port creation: `WineASIO:midi_in` and `WineASIO:midi_out`~~
- ~~Automatic MIDI port registration when connecting to JACK~~
- Note: Removed in 1.4.3 - use Wine's `winealsa.drv` + `a2jmidid` for MIDI

#### Settings GUI
- Native Linux settings panel with PyQt5/PyQt6
- Launch from DAW via "Show ASIO Panel" button
- Windows launcher executables

### Changed

- Build system completely rewritten for Wine 11+ compatibility
- PE DLLs built with MinGW-w64 cross-compiler
- Unix libraries built as separate ELF shared objects
- Installation paths updated for Wine 11 directory structure

### Fixed

- Buffer handling race conditions in audio callback
- Memory leaks in JACK port management
- Proper cleanup on driver shutdown

---

## [1.3.0] - 2023

- Make GUI settings panel compatible with PyQt6 or PyQt5
- Load libjack.so.0 dynamically at runtime
- Remove useless -mnocygwin flag
- Remove dependency on asio headers

---

## [1.2.0] - 2023

- Fix compatibility with Wine > 8
- Add wineasio-register script

---

## [1.1.0] - 2022

- Various bug fixes
- Fix compatibility with Wine > 6.5

---

## [1.0.0] - 2020

- Add packaging script
- Fix control panel startup
- Fix code to work with latest Wine
- Add custom GUI for WineASIO settings

---

[Unreleased]: https://github.com/giang17/wineasio/compare/v1.6.3...HEAD
[1.6.3]: https://github.com/giang17/wineasio/releases/tag/v1.6.3
[1.6.2]: https://github.com/giang17/wineasio/releases/tag/v1.6.2
[1.6.1]: https://github.com/giang17/wineasio/releases/tag/v1.6.1
[1.6.0]: https://github.com/giang17/wineasio/releases/tag/v1.6.0
[1.5.0]: https://github.com/giang17/wineasio/releases/tag/v1.5.0
[1.4.4]: https://github.com/giang17/wineasio/releases/tag/v1.4.4
[1.4.3]: https://github.com/giang17/wineasio/releases/tag/v1.4.3
[1.4.2]: https://github.com/giang17/wineasio/releases/tag/v1.4.2
[1.4.1]: https://github.com/giang17/wineasio/releases/tag/v1.4.1
[1.4.0]: https://github.com/giang17/wineasio/releases/tag/v1.4.0
[1.3.0]: https://github.com/wineasio/wineasio/releases/tag/v1.3.0