# Changelog

All notable changes to WineASIO will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.4.0] - 2026-01-21

### Added

#### Wine 11 Support
- **Complete Wine 11 compatibility** with new PE/Unix split architecture
- New build system (`Makefile.wine11`) for Wine 10.2+ and Wine 11
- Separate PE (Windows) and Unix (Linux) code modules
  - `asio_pe.c` - PE-side COM/ASIO interface
  - `asio_unix.c` - Unix-side JACK integration
  - `unixlib.h` - Interface definitions
- Modern `__wine_unix_call` interface for PE↔Unix communication
- Support for Wine's builtin DLL system with `winebuild --builtin`

#### 32-bit Support Verification
- Extensive testing of 32-bit WineASIO implementation
- Created `test_asio_minimal.c` - diagnostic tool to verify WineASIO works independently
- Confirmed 32-bit vtable, COM interface, and ASIO functions work correctly
- Documented Berkeley DB compatibility issues (Wine bug, not WineASIO)

#### JACK MIDI
- JACK MIDI port creation: `WineASIO:midi_in` and `WineASIO:midi_out`
- Automatic MIDI port registration when connecting to JACK
- Bidirectional MIDI routing between DAW and JACK applications

#### Settings GUI
- Native Linux settings panel with PyQt5/PyQt6
- Launch from DAW via "Show ASIO Panel" button
- Windows launcher executables: `wineasio-settings.exe` and `wineasio-settings64.exe`
- Real-time configuration without DAW restart

#### Documentation
- `WINE11_PORTING.md` - Technical details of Wine 11 port
- `DEVELOPMENT.md` - Complete developer guide
- `TEST-RESULTS.md` - 32-bit testing analysis and findings
- Comprehensive README with Wine 11 installation instructions
- Troubleshooting section for common issues

### Changed

- **Build system completely rewritten** for Wine 11+ compatibility
- PE DLLs now built with MinGW-w64 cross-compiler
- Unix libraries built as separate ELF shared objects
- Installation paths updated for Wine 11 directory structure:
  - `x86_64-windows/` and `x86_64-unix/` for 64-bit
  - `i386-windows/` and `i386-unix/` for 32-bit
- Registry settings now use Wine's modern registry system
- Improved error handling and logging

### Fixed

- 32-bit WoW64 registration (use `syswow64/regsvr32.exe`)
- Buffer handling race conditions in audio callback
- Memory leaks in JACK port management
- NULL pointer checks in Unix callback functions
- Proper cleanup on driver shutdown

### Technical Details

#### PE Side (asio_pe.c)
- COM interface implementation (IUnknown + IASIO)
- Windows registry configuration reading
- Host application callback management
- Buffer allocation and management
- Calls to Unix side via `__wine_unix_call`

#### Unix Side (asio_unix.c)
- JACK client connection and management
- Audio port creation and registration
- JACK MIDI port handling
- Real-time audio processing callback
- Sample rate and buffer size negotiation

#### Architecture
```
┌─────────────────────────────────────┐
│   Windows Application (DAW)         │
│   - FL Studio, Reaper, etc.         │
└──────────────┬──────────────────────┘
               │ ASIO API
               ▼
┌─────────────────────────────────────┐
│   WineASIO PE DLL (Windows)         │
│   - asio_pe.c                       │
│   - COM/ASIO interface              │
└──────────────┬──────────────────────┘
               │ __wine_unix_call
               ▼
┌─────────────────────────────────────┐
│   WineASIO Unix SO (Linux)          │
│   - asio_unix.c                     │
│   - JACK integration                │
└──────────────┬──────────────────────┘
               │ JACK API
               ▼
┌─────────────────────────────────────┐
│   JACK Audio Server                 │
└─────────────────────────────────────┘
```

### Known Issues

- **Berkeley DB crashes** in some 32-bit applications (Wine compatibility issue)
  - Error: `BDB0091 DB_VERSION_MISMATCH`
  - Affects: REAPER 32-bit, some VST hosts using GTK+
  - Workaround: Use 64-bit applications or clean database files
  - Note: WineASIO itself works correctly; crash occurs before initialization

### Breaking Changes

- **Build system change**: Must use `Makefile.wine11` for Wine 10.2+
- **Installation paths changed** to match Wine 11 structure
- **File naming**: `wineasio64.dll` instead of `wineasio.dll` for 64-bit
- **Legacy Makefile** still available for Wine 6.x-10.1

### Migration Guide

From WineASIO 1.3.0 to 1.4.0:

1. Remove old installation:
   ```bash
   sudo rm /usr/lib/*/wine/*/wineasio*.dll*
   sudo rm /usr/lib/*/wine/*/wineasio*.so
   ```

2. Build with new system:
   ```bash
   make -f Makefile.wine11 all
   sudo make -f Makefile.wine11 install
   ```

3. Register drivers:
   ```bash
   make -f Makefile.wine11 register
   ```

4. Clean stale database files (if experiencing crashes):
   ```bash
   find ~/.wine -name "*.db" -delete
   ```

### Dependencies

#### Build Time
- GCC 7.0+
- MinGW-w64 (i686 and x86_64 targets)
- Wine 10.2+ development headers
- JACK development files
- Python 3.6+ with PyQt5 or PyQt6

#### Runtime
- Wine 10.2+ or Wine 11
- JACK Audio Connection Kit
- Python 3.6+ with PyQt5 or PyQt6 (for settings GUI)

### Compatibility

| Wine Version | Status |
|--------------|--------|
| Wine 11.x | ✅ Fully supported |
| Wine 10.2-10.27 | ✅ Fully supported |
| Wine 10.0-10.1 | ✅ Use legacy Makefile |
| Wine 6.x-9.x | ✅ Use legacy Makefile |
| Wine 5.x and earlier | ⚠️ Untested |

### Testing

Tested configurations:
- **OS**: Ubuntu 24.04, Fedora 39, Arch Linux
- **Wine**: Wine 11.0, Wine 10.27
- **JACK**: JACK2 1.9.22
- **DAWs**: FL Studio 2025, Reaper 7.x, Ableton Live 11
- **Architectures**: x86_64 (64-bit), i386 (32-bit WoW64)

### Credits

- Wine development team for excellent PE/Unix split documentation
- WineASIO maintainers and contributors
- JACK and Linux audio community
- All users who reported bugs and tested pre-releases

### License

- WineASIO library: LGPL v2.1 or later
- Settings GUI: GPL v2 or later

---

## [1.3.0] - 2023

Previous releases. See git history for details.

For older changelogs, see the original WineASIO repository:
https://github.com/wineasio/wineasio

---

[1.4.0]: https://github.com/giang17/wineasio/releases/tag/v1.4.0
[1.3.0]: https://github.com/wineasio/wineasio/releases/tag/v1.3.0