# WineASIO Development Guide

## Overview

This document provides technical information for developers working on WineASIO, particularly regarding the Wine 11 port and 32-bit support.

**Last Updated:** 2026-01-21 (v1.4.2)

---

## Building WineASIO

### Prerequisites

**For Wine 11+ (PE/Unix split architecture):**
- GCC (for Unix shared library)
- MinGW-w64 (for PE DLL): `i686-w64-mingw32-gcc` and `x86_64-w64-mingw32-gcc`
- Wine development headers: `wine-devel` or from Wine source
- JACK development libraries: `libjack-jackd2-dev` or `libjack-dev`
- winebuild (from Wine)

**System packages (Ubuntu/Debian):**
```bash
sudo apt install gcc-mingw-w64 wine-stable wine-devel libjack-jackd2-dev
```

### Build Commands

**Wine 11+ (current):**
```bash
# Build 32-bit
make -f Makefile.wine11 32

# Build 64-bit
make -f Makefile.wine11 64

# Build both
make -f Makefile.wine11 all

# Install (requires sudo)
sudo make -f Makefile.wine11 install
```

**Legacy (Wine 6-10):**
```bash
make 32
make 64
sudo make install
```

### Build Output

Wine 11 builds create:
- `build_wine11/wineasio.dll` (32-bit PE)
- `build_wine11/wineasio.so` (64-bit Unix for 32-bit PE!)
- `build_wine11/wineasio64.dll` (64-bit PE)
- `build_wine11/wineasio64.so` (64-bit Unix)

Files are marked as Wine builtins with `winebuild --builtin`.

---

## Wine 11 WoW64 Architecture

### Critical Discovery (v1.4.2)

**In Wine 11 WoW64, 32-bit PE DLLs use 64-bit Unix libraries!**

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
│   - Installed to x86_64-unix/, NOT i386-unix/               │
└─────────────────────────────────────────────────────────────┘
```

### Build Flags Summary

| Component | Build Flag | Install Location |
|-----------|-----------|------------------|
| `wineasio.dll` (32-bit PE) | `-m32` | `i386-windows/` |
| `wineasio.so` (for 32-bit PE) | **`-m64`** | **`x86_64-unix/`** |
| `wineasio64.dll` (64-bit PE) | `-m64` | `x86_64-windows/` |
| `wineasio64.so` (for 64-bit PE) | `-m64` | `x86_64-unix/` |

**Note:** There is NO `i386-unix/` directory in Wine 11 WoW64 builds!

### Address Space Implications

PE (32-bit) and Unix (64-bit) have DIFFERENT address spaces:
- A 32-bit pointer (0x00F85760) is INVALID in 64-bit Unix space
- A 64-bit pointer (0x7fd15ad29cc0) does NOT fit in 32-bit
- **Audio buffers MUST be allocated on PE side** to be accessible by Windows apps

For detailed architecture information, see `docs/WINE11_WOW64_ARCHITECTURE.md`.

### PE/Unix Split

Wine 11 introduced a new architecture separating Windows (PE) and Unix code:

**PE Side (Windows):**
- `asio_pe.c` - COM interface, vtable, Windows API
- Compiled with MinGW to native Windows DLL
- Handles ASIO host communication
- Allocates audio buffers (important for WoW64!)
- Calls Unix side via `__wine_unix_call`

**Unix Side (Linux):**
- `asio_unix.c` - JACK integration, audio processing
- Compiled with GCC to ELF shared library (always 64-bit in WoW64!)
- Loaded by Wine's Unix loader
- Receives calls from PE side via dispatch table
- Uses PE-allocated buffers for audio

**Interface:**
- `unixlib.h` - Defines function enum and parameter structs
- Functions enumerated: `asio_init`, `asio_start`, `asio_get_channels`, etc.
- Parameters passed as structs between PE and Unix

---

## 32-bit Support Status

### Current Status: ✅ FULLY WORKING (v1.4.2)

As of 2026-01-21, WineASIO 32-bit support for Wine 11 is **complete and functional**.

### Tested Applications

| Application | Architecture | Status |
|-------------|--------------|--------|
| REAPER 32-bit | 32-bit PE | ✅ Working |
| REAPER 64-bit | 64-bit PE | ✅ Working |
| Garritan CFX Lite | 32-bit PE | ✅ Working |
| FL Studio 2025 | 64-bit PE | ✅ Working |
| test_asio_interactive.exe | 32-bit PE | ✅ Working |

---

## Testing

### Test Programs

Test programs are located in the `tests/` directory:

- `test_asio_minimal.c` - Basic ASIO functionality test
- `test_asio_interactive.c` - Keeps JACK connection open for inspection
- `test_asio_start.c` - Full ASIO pipeline test
- `test_asio_thiscall.c` - Thiscall convention verification
- `test_asio_extended.c` - Extended API testing

### Building Test Programs

```bash
# Build 32-bit test
i686-w64-mingw32-gcc -o tests/test_asio_interactive.exe tests/test_asio_interactive.c -lole32 -luuid

# Run test
WINEDEBUG=-all wine tests/test_asio_interactive.exe
```

### Testing with DAWs

**64-bit:**
```bash
wine "$HOME/.wine/drive_c/Program Files/REAPER (x64)/reaper.exe"
```

**32-bit:**
```bash
wine "$HOME/.wine/drive_c/Program Files (x86)/REAPER/reaper.exe"
```

### Debug Output

Enable WineASIO traces:
```bash
WINEDEBUG=+wineasio wine app.exe          # All traces
WINEDEBUG=warn+wineasio wine app.exe      # Warnings only
WINEDEBUG=-all wine app.exe               # Suppress Wine debug output
```

### Verification

When correctly configured, you should see Unix-side debug output:
```
[WineASIO-Unix] *** Library loaded (constructor called) ***
[WineASIO-Unix] >>> asio_init() called
```

And PE buffer usage in JACK callback:
```
wineasio:trace: Copying input 0, pe_buffer[0]=0x1355cc0
wineasio:trace: Copying output 0, pe_buffer[0]=0x13564c0
```

If you see `audio_buffer=(nil)` warnings, the fix is not installed correctly.

---

## Code Structure

### Key Files

**Source:**
- `asio_pe.c` - PE (Windows) side implementation
- `asio_unix.c` - Unix (Linux) side implementation
- `unixlib.h` - PE↔Unix interface definitions

**Build:**
- `Makefile.wine11` - Wine 11+ build system
- `Makefile` - Legacy (Wine 6-10) build system
- `wineasio.def` - PE DLL export definitions

**Documentation:**
- `README.md` - User documentation
- `docs/WINE11_PORTING.md` - Technical porting details
- `docs/WINE11_WOW64_ARCHITECTURE.md` - WoW64 architecture
- `docs/WINE11_WOW64_32BIT_SOLUTION.md` - 32-bit fix explanation

**Tests:**
- `tests/test_asio_*.c` - Test programs

### PE Side Functions

Main ASIO interface functions (in `asio_pe.c`):
- `Init()` - Initialize ASIO driver
- `Start()` / `Stop()` - Start/stop audio processing
- `GetChannels()` - Query input/output channel count
- `GetBufferSize()` - Query buffer size constraints
- `CreateBuffers()` - Allocate audio buffers (on PE side!)
- `DisposeBuffers()` - Free audio buffers

COM infrastructure:
- `WineASIOCreateInstance()` - Factory function
- `DllGetClassObject()` - COM entry point
- `DllRegisterServer()` - Registry installation

### Unix Side Functions

JACK integration (in `asio_unix.c`):
- `asio_init()` - Connect to JACK, create ports
- `asio_start()` / `asio_stop()` - Activate/deactivate JACK client
- `asio_get_channels()` - Return configured channel count
- `asio_create_buffers()` - Register JACK ports, receive PE buffer pointers
- `jack_process_callback()` - Audio processing callback (uses `pe_buffer[]`)

---

## Adding New Features

### Example: Adding a New ASIO Function

1. **Add to PE side (`asio_pe.c`):**
```c
static LONG STDMETHODCALLTYPE MyNewFunction(LPWINEASIO iface, LONG param)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct my_new_function_params params = { .handle = This->handle, .param = param };
    
    UNIX_CALL(my_new_function, &params);
    return params.result;
}
```

2. **Add to vtable:**
```c
static const IWineASIOVtbl WineASIO_Vtbl = {
    ...
    MyNewFunction,
    ...
};
```

3. **Add to Unix side (`asio_unix.c`):**
```c
static NTSTATUS asio_my_new_function(void *args)
{
    struct my_new_function_params *params = args;
    // Implementation here
    params->result = ASE_OK;
    return STATUS_SUCCESS;
}
```

4. **Add to interface (`unixlib.h`):**
```c
enum wineasio_funcs
{
    ...
    unix_my_new_function,
    ...
};

struct my_new_function_params
{
    asio_handle handle;
    LONG param;
    LONG result;
};
```

5. **Add to dispatch table (`asio_unix.c`):**
```c
const unixlib_entry_t __wine_unix_call_funcs[] =
{
    ...
    asio_my_new_function,
    ...
};
```

---

## Debugging Tips

### Debug Builds

Enable debug symbols and tracing:
```bash
# Edit Makefile.wine11, change -O2 to -O0 -g
make -f Makefile.wine11 clean
make -f Makefile.wine11 32 64
```

### Common Issues

**"Cannot find wineasio.so" or no Unix debug output:**
- Check that `wineasio.so` is in `x86_64-unix/` (NOT `i386-unix/`)
- Verify it's 64-bit: `file /path/to/wine/x86_64-unix/wineasio.so`
- Ensure `winebuild --builtin` was run

**"Class not registered":**
- Run `wine regsvr32 wineasio.dll` (32-bit)
- Check registry: `wine regedit` → HKEY_CLASSES_ROOT\CLSID\{48D0C522-...}

**No audio but JACK ports visible:**
- Check for `audio_buffer=(nil)` warnings - old code still using Unix-side buffers
- Rebuild and reinstall with latest code

### GDB Debugging

Debug Unix side:
```bash
# Start wineserver
wineserver -p

# Run with gdb
WINEDEBUG=+wineasio gdb --args wine app.exe

# Break in WineASIO
(gdb) break asio_init
(gdb) run
```

---

## Contributing

See `CONTRIBUTING.md` for:
- Code style guidelines
- Commit message format
- Pull request process
- Testing requirements

---

## References

- **ASIO SDK**: https://www.steinberg.net/developers/
- **Wine Documentation**: https://wiki.winehq.org/
- **Wine PE/Unix Split**: https://wiki.winehq.org/Dll_Separation
- **JACK Audio**: https://jackaudio.org/
- **Original WineASIO**: https://github.com/wineasio/wineasio

---

## License

WineASIO is released under the LGPL v2.1 or later.

See LICENSE file for full text.