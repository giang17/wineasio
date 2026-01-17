# WineASIO 1.4.0 Release Notes

**Release Date:** January 2026  
**Codename:** Wine 11 Edition

---

## 🎉 Overview

WineASIO 1.4.0 is a major release that brings full compatibility with **Wine 11** and its new DLL architecture. This release represents a complete rewrite of the build system and internal architecture to support Wine's separation of PE (Windows) and Unix code.

---

## ✨ New Features

### Wine 11 Support
- **New PE/Unix split architecture** - Complete rewrite to support Wine 11's builtin DLL system
- **Separate PE DLL and Unix SO builds** - Clean separation between Windows and Linux code
- **`__wine_unix_call` interface** - Modern Wine communication between PE and Unix layers
- **Full 32-bit and 64-bit support** - Proper WoW64 compatibility for legacy DAWs

### Settings GUI Integration
- **Launch from DAW** - Click "Show ASIO Panel" in FL Studio, Reaper, etc. to open settings
- **Native Linux GUI** - PyQt5/PyQt6 settings panel runs natively for best performance
- **Windows launcher executables** - `wineasio-settings.exe` and `wineasio-settings64.exe` included
- **Real-time configuration** - Modify settings without restarting your DAW

### JACK MIDI Support
- **JACK MIDI ports** - `WineASIO:midi_in` and `WineASIO:midi_out` ports
- **Flexible routing** - Connect hardware MIDI or software synths via JACK
- **a2jmidid compatible** - Works with ALSA-to-JACK MIDI bridge

---

## 📦 What's Included

### Core Files (Wine 11)
| File | Description |
|------|-------------|
| `asio_pe.c` | PE-side ASIO/COM implementation |
| `asio_unix.c` | Unix-side JACK/MIDI implementation |
| `unixlib.h` | Shared interface definitions |
| `Makefile.wine11` | Wine 11+ build system |

### Built Libraries
| File | Architecture | Description |
|------|--------------|-------------|
| `wineasio64.dll` | 64-bit | PE DLL for Wine x86_64 |
| `wineasio64.so` | 64-bit | Unix library for JACK interface |
| `wineasio.dll` | 32-bit | PE DLL for Wine i386 (WoW64) |
| `wineasio.so` | 32-bit | Unix library for JACK interface |

### GUI Components
| File | Description |
|------|-------------|
| `wineasio-settings` | Linux launcher script |
| `settings.py` | PyQt settings application |
| `wineasio-settings.exe` | 32-bit Windows launcher |
| `wineasio-settings64.exe` | 64-bit Windows launcher |

---

## 🔧 Installation

### Quick Install (Wine 11+)

```bash
# Build
make -f Makefile.wine11 all

# Install (requires sudo)
sudo make -f Makefile.wine11 install

# Register
make -f Makefile.wine11 register

# Verify
make -f Makefile.wine11 verify
```

### Manual Installation

```bash
# 64-bit
sudo cp build_wine11/wineasio64.dll /opt/wine-stable/lib/wine/x86_64-windows/
sudo cp build_wine11/wineasio64.so /opt/wine-stable/lib/wine/x86_64-unix/

# 32-bit
sudo cp build_wine11/wineasio.dll /opt/wine-stable/lib/wine/i386-windows/
sudo cp build_wine11/wineasio.so /opt/wine-stable/lib/wine/i386-unix/

# Register
wine regsvr32 wineasio64.dll
wine ~/.wine/drive_c/windows/syswow64/regsvr32.exe wineasio.dll
```

---

## 💻 System Requirements

### Build Requirements
- GCC with 32-bit and 64-bit support
- MinGW-w64 cross-compiler (i686 and x86_64)
- Wine 10.2+ or Wine 11 development headers
- JACK Audio Connection Kit development files
- Python 3 with PyQt5 or PyQt6 (for GUI)

### Runtime Requirements
- Wine 10.2+ or Wine 11
- JACK Audio Connection Kit (jack2 recommended)
- Python 3 with PyQt5 or PyQt6 (for settings GUI)

---

## 🐛 Known Issues

1. **32-bit registration** - Must use the WoW64 regsvr32 (`syswow64/regsvr32.exe`)
2. **JACK must be running** - Start JACK before launching your DAW
3. **PyQt dependency** - Settings GUI requires PyQt5 or PyQt6

---

## 🔄 Migration from 1.3.0

If you're upgrading from WineASIO 1.3.0:

1. **Remove old libraries** - Delete old `wineasio*.dll.so` files
2. **Use new Makefile** - Switch to `Makefile.wine11` for Wine 11+
3. **Re-register** - Run `make -f Makefile.wine11 register`

The registry settings are compatible and will be preserved.

---

## 🙏 Acknowledgments

This release was made possible by:
- The Wine development team for the excellent documentation on the new DLL architecture
- All WineASIO contributors and maintainers over the years
- The JACK and Linux audio community

---

## 📄 License

- **WineASIO library**: LGPL v2.1 (see COPYING.LIB)
- **Settings GUI**: GPL v2+ (see COPYING.GUI)

---

## 🔗 Links

- **GitHub**: https://github.com/giang17/wineasio
- **Original Project**: https://github.com/wineasio/wineasio
- **Wine**: https://www.winehq.org/
- **JACK**: https://jackaudio.org/

---

## 📊 Compatibility Matrix

| DAW | 32-bit | 64-bit | Control Panel | Notes |
|-----|--------|--------|---------------|-------|
| FL Studio | ✅ | ✅ | ✅ | Fully tested |
| Reaper | ✅ | ✅ | ✅ | Fully tested |
| Ableton Live | ⚠️ | ✅ | ✅ | 64-bit recommended |
| Bitwig Studio | - | ✅ | ✅ | Linux native preferred |
| Cubase | ⚠️ | ✅ | ✅ | Some versions may need tweaks |

Legend: ✅ Working | ⚠️ Partially tested | - Not applicable

---

**Happy music making with Wine 11! 🎵🍷**
