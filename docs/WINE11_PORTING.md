# WineASIO Wine 11 Porting Guide

This document describes the technical changes required to port WineASIO from the legacy Wine architecture to Wine 11's new PE/Unix split architecture.

**Last Updated:** 2026-01-22 (v1.4.2)

> **Key Discovery:** All Wine-specific ntdll symbols (`__wine_unix_call_dispatcher`, `NtQueryVirtualMemory`) must be loaded **dynamically** via `GetProcAddress()` to work correctly on 32-bit WoW64. Static imports cause crashes due to DATA export resolution issues and stdcall decoration mismatches.

> **Important:** See also `WINE11_WOW64_ARCHITECTURE.md` and `WINE11_WOW64_32BIT_SOLUTION.md` for the critical WoW64 architecture discovery.

## Background

### Wine's New DLL Architecture (Wine 10.2+/11)

Starting with Wine 10.2, Wine moved to a new builtin DLL architecture:

**Old Architecture (Wine ≤ 10.1):**
- Single `.dll.so` file containing both PE and Unix code
- Mixed Windows and Linux code in one binary
- Built with `winegcc`

**New Architecture (Wine 10.2+/11):**
- Separate PE DLL (pure Windows binary) built with `mingw-w64`
- Separate Unix `.so` (pure Linux binary) built with `gcc`
- Communication via `__wine_unix_call` interface

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                     Windows Application                          │
│                    (FL Studio, Reaper, etc.)                    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ ASIO API calls
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    wineasio64.dll (PE)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ • COM/ASIO Interface implementation                       │  │
│  │ • Registry configuration reading                          │  │
│  │ • Host callback management                                │  │
│  │ • Callback polling thread                                 │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ __wine_unix_call()
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    wineasio64.so (Unix)                         │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ • JACK client connection                                  │  │
│  │ • Audio buffer management                                 │  │
│  │ • JACK callbacks (process, buffer size, sample rate)     │  │
│  │ • Real-time audio processing                              │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ libjack API
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        JACK Server                               │
└─────────────────────────────────────────────────────────────────┘
```

## File Structure

```
wineasio-fork/
├── asio_pe.c           # PE-side implementation (Windows code)
├── asio_unix.c         # Unix-side implementation (JACK code)
├── unixlib.h           # Shared interface between PE and Unix
├── wineasio.def        # DLL export definitions (for 32-bit)
├── ntdll_wine.def      # ntdll placeholder (64-bit) - empty, dynamic loading
├── ntdll_wine32.def    # ntdll placeholder (32-bit) - empty, dynamic loading
├── Makefile.wine11     # New build system for Wine 11
├── asio.c              # Legacy combined code (Wine ≤ 10.1)
├── tests/              # Test programs for debugging
└── gui/
    ├── settings.py                 # PyQt5/6 Settings GUI
    ├── settings.ui                 # Qt Designer UI file
    ├── ui_settings.py              # Generated UI code
    ├── wineasio-settings           # Linux launcher script
    ├── wineasio-settings-launcher.c # Windows launcher source
    ├── wineasio-settings.exe       # 32-bit Windows launcher
    └── wineasio-settings64.exe     # 64-bit Windows launcher
```

## Key Components

### 1. unixlib.h - Shared Interface

This header defines:
- Parameter structures for each Unix function call
- Function ID enumeration (`enum unix_funcs`)
- Shared type definitions
- ASIO error codes and constants

```c
enum unix_funcs {
    unix_asio_init,
    unix_asio_exit,
    unix_asio_start,
    unix_asio_stop,
    unix_asio_get_channels,
    // ... etc
    unix_funcs_count
};

struct asio_init_params {
    struct asio_config config;
    HRESULT result;
    asio_handle handle;
    // ... etc
};
```

### 2. asio_pe.c - PE Side (Windows)

Responsibilities:
- Implements the IASIO COM interface
- Reads configuration from Windows registry
- Manages the ASIO host callbacks
- Runs a polling thread to check for buffer switch notifications
- Calls Unix functions via `__wine_unix_call()`

Key functions:
- `DllMain()` - Initializes Wine Unix call interface
- `Init()` - Calls Unix side to connect to JACK
- `Start()` / `Stop()` - Controls audio streaming
- `CreateBuffers()` - Sets up audio buffers
- `callback_thread_proc()` - Polls Unix side for callbacks

### 3. asio_unix.c - Unix Side (Linux)

Responsibilities:
- Loads libjack dynamically
- Creates JACK client and ports
- Handles JACK callbacks (process, buffer size, sample rate)
- Manages audio buffers
- Signals PE side when buffer switch is needed

Key functions:
- `asio_init()` - Connects to JACK server
- `asio_start()` / `asio_stop()` - Activates/deactivates JACK client
- `jack_process_callback()` - Real-time audio processing
- `asio_get_callback()` - Returns pending callback information to PE side

## Wine Unix Call Interface

### Initialization (PE Side)

**Important:** All Wine-specific symbols are loaded **dynamically** via `GetProcAddress()` to avoid import resolution issues on 32-bit WoW64. This is a critical fix discovered during the Wine 11 porting process.

The original approach using `extern` imports caused crashes on 32-bit WoW64 because:
1. `__wine_unix_call_dispatcher` is exported as DATA (a pointer), not a function
2. Static imports resolved it incorrectly as a function call to NULL
3. stdcall decoration mismatches on 32-bit (`@N` suffix issues)

```c
// Type definitions for dynamically loaded functions
typedef NTSTATUS (*wine_unix_call_dispatcher_t)(UINT64 handle, unsigned int code, void *args);
typedef NTSTATUS (*NtQueryVirtualMemory_t)(HANDLE, LPCVOID, ULONG, PVOID, SIZE_T, SIZE_T*);

// Function pointers - loaded at runtime
static wine_unix_call_dispatcher_t p__wine_unix_call_dispatcher = NULL;
static NtQueryVirtualMemory_t pNtQueryVirtualMemory = NULL;
static UINT64 wineasio_unix_handle = 0;

// Initialize in DllMain - ALL symbols loaded dynamically
static BOOL init_wine_unix_call(HINSTANCE hInstDLL)
{
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    
    // Load __wine_unix_call_dispatcher - it's exported as DATA (a pointer)
    // GetProcAddress returns the address of the exported variable
    wine_unix_call_dispatcher_t *dispatcher_ptr = 
        (wine_unix_call_dispatcher_t *)GetProcAddress(hNtdll, "__wine_unix_call_dispatcher");
    p__wine_unix_call_dispatcher = *dispatcher_ptr;  // Dereference to get function pointer
    
    // Load NtQueryVirtualMemory dynamically (avoids stdcall decoration issues on 32-bit)
    pNtQueryVirtualMemory = (NtQueryVirtualMemory_t)GetProcAddress(hNtdll, "NtQueryVirtualMemory");
    
    // Query for Unix library handle (MemoryWineUnixFuncs = 1000)
    pNtQueryVirtualMemory(GetCurrentProcess(), hInstDLL, 
                          1000 /* MemoryWineUnixFuncs */, 
                          &wineasio_unix_handle, sizeof(wineasio_unix_handle), NULL);
    return TRUE;
}

// Call Unix function
#define UNIX_CALL(func, params) \
    p__wine_unix_call_dispatcher(wineasio_unix_handle, unix_##func, params)
```

**Note:** The `ntdll_wine.def` and `ntdll_wine32.def` files are now essentially empty (no EXPORTS) because all Wine-specific symbols are loaded dynamically. They exist only to create empty import libraries for the linker.

### Unix Function Table (Unix Side)

```c
// Each function takes a void* args parameter
NTSTATUS asio_init(void *args)
{
    struct asio_init_params *params = args;
    // ... implementation
    return STATUS_SUCCESS;
}

// Export the function table
const unixlib_entry_t __wine_unix_call_funcs[] =
{
    asio_init,
    asio_exit,
    asio_start,
    // ... etc
};
```

## Callback Handling

ASIO requires the host to be called back from the audio thread. Since we can't call Windows code directly from Unix threads, we use a polling mechanism:

1. **Unix side** (in JACK process callback):
   - Copies audio data to shared buffers
   - Sets `callback_pending` flag
   - Stores timing information

2. **PE side** (in polling thread):
   - Regularly calls `UNIX_CALL(asio_get_callback, &params)`
   - If callback is pending, calls host's `bufferSwitch()` or `bufferSwitchTimeInfo()`
   - Acknowledges callback with `UNIX_CALL(asio_callback_done, &params)`

```c
// PE side polling thread
DWORD callback_thread_proc(LPVOID param)
{
    while (!This->stop_callback_thread) {
        UNIX_CALL(asio_get_callback, &cb_params);
        
        if (cb_params.buffer_switch_ready) {
            if (This->time_info_mode)
                This->callbacks->bufferSwitchTimeInfo(...);
            else
                This->callbacks->bufferSwitch(...);
            
            UNIX_CALL(asio_callback_done, &done_params);
        }
        Sleep(1);  // Polling interval
    }
}
```

## Build System

### Makefile.wine11

Key targets:
- `64` - Builds 64-bit PE DLL and Unix SO
- `32` - Builds 32-bit PE DLL and Unix SO
- `install` - Installs to Wine directories
- `register` - Registers with Wine

### 32-bit Specific Issues

1. **stdcall decoration**: mingw-w64 uses `@N` suffix for stdcall functions, but Wine expects undecorated names
   - Solution: Use a `.def` file to export undecorated names

2. **NtQueryVirtualMemory**: Same decoration issue
   - Solution: Load dynamically via `GetProcAddress()`

3. **__wine_unix_call_dispatcher DATA export**: This symbol is exported as DATA (a pointer to a function), not as a function itself. Static imports resolve it incorrectly.
   - Solution: Load dynamically via `GetProcAddress()` and dereference

```c
// wineasio.def - exports without decoration
EXPORTS
    DllCanUnloadNow
    DllGetClassObject
    DllRegisterServer
    DllUnregisterServer
```

```c
// ntdll_wine.def / ntdll_wine32.def - empty, all loaded dynamically
LIBRARY ntdll.dll
EXPORTS
    ; No static imports needed - all Wine-specific functions loaded dynamically
```

### Naming Convention

| Architecture | PE DLL | Unix SO |
|--------------|--------|---------|
| 64-bit | `wineasio64.dll` | `wineasio64.so` |
| 32-bit | `wineasio.dll` | `wineasio.so` |

Note: 32-bit uses `wineasio` (not `wineasio32`) to match Wine's internal expectations.

## Installation Paths

Wine 11 WoW64 uses separate directories for PE and Unix libraries.

**⚠️ CRITICAL:** In Wine 11 WoW64, there is NO `i386-unix/` directory! 32-bit PE DLLs use 64-bit Unix libraries!

```
/opt/wine-stable/lib/wine/
├── x86_64-windows/     # 64-bit PE DLLs
│   └── wineasio64.dll
├── x86_64-unix/        # ALL Unix SOs (both 32-bit and 64-bit PE!)
│   ├── wineasio64.so   # For 64-bit PE
│   └── wineasio.so     # For 32-bit PE (but this is a 64-bit binary!)
└── i386-windows/       # 32-bit PE DLLs
    └── wineasio.dll
```

### WoW64 Architecture Summary

| Component | Build Flag | Install Location |
|-----------|-----------|------------------|
| `wineasio.dll` (32-bit PE) | `-m32` | `i386-windows/` |
| `wineasio.so` (for 32-bit PE) | **`-m64`** | **`x86_64-unix/`** |
| `wineasio64.dll` (64-bit PE) | `-m64` | `x86_64-windows/` |
| `wineasio64.so` (for 64-bit PE) | `-m64` | `x86_64-unix/` |

## Debugging

### WineASIO Debug Output

By default, WineASIO produces minimal output to avoid causing audio xruns:

```
[WineASIO] Initialized: 16 in, 16 out, 48000 Hz, 256 samples
```

To enable verbose tracing, rebuild with `-DWINEASIO_DEBUG`:

```bash
# Edit Makefile.wine11, add to UNIX_CFLAGS:
UNIX_CFLAGS += -DWINEASIO_DEBUG

# Rebuild and reinstall
make -f Makefile.wine11 clean all
sudo make -f Makefile.wine11 install
```

This enables `wineasio:trace:` messages for all ASIO function calls.

**Warning:** Verbose tracing can cause xruns during audio playback due to the overhead of fprintf calls in the audio callback path. Only enable for debugging, not production use.

### Enable Wine Debug Output

```bash
WINEDEBUG=+asio,+module wine your_app.exe
```

### Check Library Loading

```bash
WINEDEBUG=+loaddll wine regsvr32 wineasio64.dll 2>&1 | grep wineasio
```

### Verify Exports

```bash
# 64-bit
x86_64-w64-mingw32-objdump -p wineasio64.dll | grep -i dllregister

# 32-bit (should NOT have @0 suffix)
i686-w64-mingw32-objdump -p wineasio.dll | grep -i dllregister
```

### Check Unix Library Symbols

```bash
nm -D wineasio64.so | grep wine
# Should show: __wine_unix_call_funcs
```

### Verify Debug Build

```bash
# Check if WINEASIO_DEBUG is enabled in the installed .so
strings /opt/wine-stable/lib/wine/x86_64-unix/wineasio.so | grep "wineasio:trace"
# If output appears, debug tracing is compiled in
```

## 32-bit Registration (WoW64)

### Important: Use the Correct regsvr32

On a 64-bit Wine prefix, there are two `regsvr32.exe` binaries:

| Binary | Location | Purpose |
|--------|----------|---------|
| 64-bit | `C:\windows\system32\regsvr32.exe` | Registers 64-bit DLLs |
| 32-bit | `C:\windows\syswow64\regsvr32.exe` | Registers 32-bit DLLs |

**The default `wine regsvr32` uses the 64-bit version!**

To register the 32-bit WineASIO DLL, you must explicitly use the 32-bit regsvr32:

```bash
# 64-bit registration (works with default regsvr32)
wine regsvr32 wineasio64.dll

# 32-bit registration (MUST use syswow64 regsvr32)
wine ~/.wine/drive_c/windows/syswow64/regsvr32.exe wineasio.dll
```

### WoW64 Registry Entries

After successful 32-bit registration, you should see these registry entries:

```
HKCR\WOW6432Node\CLSID\{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}
HKLM\Software\WOW6432Node\ASIO\WineASIO
```

Verify with:
```bash
wine reg query "HKCR\WOW6432Node\CLSID\{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}"
wine reg query "HKLM\Software\WOW6432Node\ASIO\WineASIO"
```

## MIDI Support

**WineASIO handles audio only (ASIO).** For MIDI with Windows applications under Wine, use Wine's built-in MIDI support via `winealsa.drv`.

### How Wine MIDI Works

Wine's `winealsa.drv` provides the Windows MIDI API by bridging to ALSA MIDI. This is completely separate from WineASIO and works automatically.

To use MIDI devices with JACK-based workflows:

```bash
# Start the ALSA-to-JACK MIDI bridge
a2jmidid -e &

# List available MIDI ports
jack_lsp | grep -i midi
```

You should see your MIDI devices as `a2j:` ports:
```
a2j:Kurzweil KM88 [16] (capture): [0] Kurzweil KM88 (USB MIDI)
a2j:Kurzweil KM88 [16] (playback): [0] Kurzweil KM88 (USB MIDI)
a2j:WINE midi driver [129] (playback): [0] WINE ALSA Input
```

### Connecting MIDI Devices to Wine Applications

Use `jack_connect`, QjackCtl, or Carla to route MIDI from your hardware to Wine:

```bash
# Connect a hardware MIDI keyboard to Wine
jack_connect "a2j:Your Keyboard (capture)" "a2j:WINE midi driver (playback)"
```

The MIDI devices will then appear in your Windows DAW's MIDI settings (e.g., FL Studio's MIDI settings dialog).

### Why WineASIO Doesn't Include MIDI

ASIO is an **audio-only** interface. While some ASIO drivers bundle MIDI functionality, WineASIO focuses purely on low-latency audio. Wine's existing MIDI infrastructure (`winealsa.drv`) already provides full Windows MIDI API support, making additional MIDI code in WineASIO redundant.

### Troubleshooting MIDI

**MIDI devices not appearing in DAW:**
- Ensure `a2jmidid -e` is running
- Check connections with `jack_lsp` and `jack_connect`
- Verify the device appears in `aconnect -l` (ALSA MIDI)

**"device already open" error in Wine:**
- Close other applications using the MIDI device
- Try using the device through `a2jmidid` instead of direct ALSA access

## Control Panel / Settings GUI

WineASIO includes a settings GUI that can be launched from DAWs like FL Studio.

### How It Works

1. **ASIO Control Panel Button**: When you click "Show ASIO Panel" in FL Studio (or similar in other DAWs), the ASIO driver's `ControlPanel()` function is called.

2. **Unix-side Implementation**: The `asio_control_panel()` function in `asio_unix.c` uses `fork()/exec()` to launch the native Linux `wineasio-settings` tool:

```c
static NTSTATUS asio_control_panel(void *args)
{
    pid_t pid = fork();
    if (pid == 0) {
        execvp("wineasio-settings", arg_list);
        execl("/usr/bin/wineasio-settings", "wineasio-settings", NULL);
        _exit(1);
    }
    return STATUS_SUCCESS;
}
```

3. **Native Linux GUI**: The `wineasio-settings` Python/PyQt application reads and writes WineASIO settings to the Wine registry.

### Installation

The GUI is installed automatically with `make -f Makefile.wine11 install`:

- **Linux launcher**: `/usr/bin/wineasio-settings`
- **Python code**: `/usr/share/wineasio/settings.py`
- **Windows launchers**: `/usr/share/wineasio/wineasio-settings*.exe`

### Manual Launch

```bash
# From Linux terminal
wineasio-settings

# From Wine (using Windows launcher)
wine /usr/share/wineasio/wineasio-settings64.exe
```

### Settings Stored

The GUI modifies these registry values in `HKCU\Software\Wine\WineASIO`:

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| Number of inputs | DWORD | 16 | JACK input ports |
| Number of outputs | DWORD | 16 | JACK output ports |
| Connect to hardware | DWORD | 1 | Auto-connect to physical ports |
| Autostart server | DWORD | 0 | Start JACK if not running |
| Fixed buffersize | DWORD | 1 | Prevent apps from changing buffer |
| Preferred buffersize | DWORD | 1024 | Default buffer size |

## Common Issues

### "Unix library not found"

- Ensure `.so` file is in the correct `*-unix/` directory
- Ensure `.so` exports `__wine_unix_call_funcs`
- Check for unresolved symbols: `nm -D wineasio64.so | grep " U "`

### "DllRegisterServer not found" (32-bit)

- Ensure `wineasio.def` is included in the build
- Verify exports don't have stdcall decoration

### "NtQueryVirtualMemory not found" (32-bit)

- Load via `GetProcAddress()` instead of static import
- Don't use `@24` decoration in def file

### "cannot find builtin library" (32-bit registration)

- You're using the wrong regsvr32! Use the 32-bit version:
  ```bash
  wine ~/.wine/drive_c/windows/syswow64/regsvr32.exe wineasio.dll
  ```

### Control Panel doesn't open

- Ensure `wineasio-settings` is in PATH (`/usr/bin/`)
- Check if PyQt5 or PyQt6 is installed: `python3 -c "from PyQt5.QtWidgets import QApplication"`
- Try launching manually: `wineasio-settings`

## References

- Wine GitLab: https://gitlab.winehq.org/wine/wine
- Wine Unix Call Interface: `include/wine/unixlib.h`
- Example builtin DLLs: `dlls/winealsa.drv/`, `dlls/winepulse.drv/`

## Quick Reference

### Build Commands

```bash
make -f Makefile.wine11 64          # Build 64-bit only
make -f Makefile.wine11 32          # Build 32-bit only
make -f Makefile.wine11 all         # Build both
make -f Makefile.wine11 install     # Install everything (requires sudo)
make -f Makefile.wine11 register    # Register with Wine
```

### Verify Installation

```bash
# Check files
ls -la /opt/wine-stable/lib/wine/x86_64-windows/wineasio64.dll
ls -la /opt/wine-stable/lib/wine/x86_64-unix/wineasio64.so
ls -la /opt/wine-stable/lib/wine/i386-windows/wineasio.dll
ls -la /opt/wine-stable/lib/wine/i386-unix/wineasio.so

# Check registration
wine reg query "HKLM\Software\ASIO\WineASIO"
wine reg query "HKLM\Software\WOW6432Node\ASIO\WineASIO"

# Check GUI
which wineasio-settings
```

## Authors

Wine 11 port implemented January 2026, one day after Wine 11's official release (January 13, 2026).

Control Panel GUI integration added January 2026.

JACK MIDI support added January 2026.
