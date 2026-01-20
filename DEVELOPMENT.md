# WineASIO Development Guide

## Overview

This document provides technical information for developers working on WineASIO, particularly regarding the Wine 11 port and 32-bit support.

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
- `build_wine11/wineasio.dll` (32-bit PE) or `wineasio64.dll` (64-bit PE)
- `build_wine11/wineasio.so` (32-bit Unix) or `wineasio64.so` (64-bit Unix)

Files are marked as Wine builtins with `winebuild --builtin`.

---

## Wine 11 Architecture

### PE/Unix Split

Wine 11 introduced a new architecture separating Windows (PE) and Unix code:

**PE Side (Windows):**
- `asio_pe.c` - COM interface, vtable, Windows API
- Compiled with MinGW to native Windows DLL
- Handles ASIO host communication
- Calls Unix side via `__wine_unix_call`

**Unix Side (Linux):**
- `asio_unix.c` - JACK integration, audio processing
- Compiled with GCC to ELF shared library
- Loaded by Wine's Unix loader
- Receives calls from PE side via dispatch table

**Interface:**
- `unixlib.h` - Defines function enum and parameter structs
- Functions enumerated: `asio_init`, `asio_start`, `asio_get_channels`, etc.
- Parameters passed as structs between PE and Unix

### Why This Architecture?

1. **Security**: PE code runs in restricted environment
2. **Stability**: Unix code crashes don't affect Wine process
3. **Performance**: Direct syscalls from Unix side
4. **Compatibility**: Native PE matches Windows behavior

For detailed technical information, see `WINE11_PORTING.md`.

---

## 32-bit Support Status

### Current Status: ✅ WORKING

As of 2026-01-20, WineASIO 32-bit support for Wine 11 is **complete and functional**.

### Testing Results

**Minimal Test Program (`test_asio_minimal.exe`):**
- ✅ COM interface works
- ✅ Init() succeeds
- ✅ GetChannels() returns correct values
- ✅ No NULL pointer crashes
- ✅ Clean shutdown

**Real-world Applications:**
- ⚠️ Some apps (REAPER, CFX Lite) crash with Berkeley DB errors
- This is a Wine/libdb compatibility issue, **NOT a WineASIO bug**
- WineASIO functions correctly when called

### Known Issues

**Berkeley DB Crashes:**
```
BDB1539 Build signature doesn't match environment
Cannot open DB environment: BDB0091 DB_VERSION_MISMATCH
```

**Workaround:**
- Use 64-bit applications when possible
- Remove `.db` files from Wine prefix
- Update system libdb to match Wine's version

See `TEST-RESULTS.md` for detailed analysis.

---

## Testing

### Quick Test

```bash
# Test WineASIO directly
cd wineasio-fork
i686-w64-mingw32-gcc -o test_asio_minimal.exe test_asio_minimal.c -lole32 -luuid
WINEDEBUG=warn+wineasio wine test_asio_minimal.exe
```

Expected output:
- WineASIO instance created
- Init() succeeds
- GetChannels() returns 16/16
- No crashes

### Testing with DAWs

**64-bit (recommended):**
```bash
WINEDEBUG=warn+wineasio wine64 /path/to/app64.exe
```

**32-bit:**
```bash
WINEDEBUG=warn+wineasio wine /path/to/app32.exe
```

### Debug Output

Enable WineASIO traces:
```bash
WINEDEBUG=+wineasio wine app.exe          # All traces
WINEDEBUG=warn+wineasio wine app.exe      # Warnings only
WINEDEBUG=+all wine app.exe 2>&1 | less   # Everything
```

---

## Code Structure

### Key Files

**Source:**
- `asio_pe.c` - PE (Windows) side implementation
- `asio_unix.c` - Unix (Linux) side implementation
- `unixlib.h` - PE↔Unix interface definitions
- `asio.h` - ASIO SDK interface (from Steinberg)

**Build:**
- `Makefile.wine11` - Wine 11+ build system
- `Makefile` - Legacy (Wine 6-10) build system
- `wineasio.def` - PE DLL export definitions
- `ntdll_wine32.def` / `ntdll_wine64.def` - Import libraries

**Documentation:**
- `README.md` - User documentation
- `WINE11_PORTING.md` - Technical porting details
- `RELEASE_NOTES.md` - Version history
- `TEST-RESULTS.md` - 32-bit testing analysis

### PE Side Functions

Main ASIO interface functions (in `asio_pe.c`):
- `Init()` - Initialize ASIO driver
- `Start()` / `Stop()` - Start/stop audio processing
- `GetChannels()` - Query input/output channel count
- `GetBufferSize()` - Query buffer size constraints
- `CreateBuffers()` - Allocate audio buffers
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
- `asio_create_buffers()` - Register JACK ports
- `jack_process_callback()` - Audio processing callback

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

**"Cannot find wineasio.so":**
- Check installation path matches Wine's library directory
- For Wine 11: Use `NtQueryVirtualMemory` to find Unix library
- Ensure `winebuild --builtin` was run

**"Class not registered":**
- Run `wine regsvr32 wineasio.dll` (32-bit)
- Or `wine64 regsvr32 wineasio.dll` (64-bit)
- Check registry: `wine regedit` → HKEY_CLASSES_ROOT\CLSID\{48D0C522-...}

**Crashes in real apps but test works:**
- Not a WineASIO bug (see TEST-RESULTS.md)
- Check for libdb, GTK+, or other library conflicts
- Try different Wine version or application

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
- **JACK Audio**: https://jackaudio.org/
- **Original WineASIO**: https://github.com/wineasio/wineasio

---

## License

WineASIO is released under the LGPL v2.1 or later.

See LICENSE file for full text.