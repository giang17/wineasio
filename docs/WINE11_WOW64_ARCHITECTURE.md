# Wine 11 WoW64 Architecture for WineASIO

**Last Updated**: 2026-01-21

## Critical Discovery: Wine 11 WoW64 Architecture

### The Key Insight

In Wine 11 WoW64, **32-bit PE DLLs use 64-bit Unix libraries**!

```
Wine 11 WoW64 Architecture:
┌─────────────────────────────────────────────────────────────┐
│ 32-bit PE DLL (wineasio.dll)                                │
│   - Runs in emulated 32-bit address space                   │
│   - Addresses: 0x00000000 - 0x7FFFFFFF                      │
└─────────────────────┬───────────────────────────────────────┘
                      │ __wine_unix_call()
                      │ (WoW64 thunking)
                      ▼
┌─────────────────────────────────────────────────────────────┐
│ 64-bit Unix .so (wineasio.so in x86_64-unix/)               │
│   - Runs in native 64-bit address space                     │
│   - Addresses: Full 64-bit range                            │
│   - Talks to JACK/PipeWire                                  │
└─────────────────────────────────────────────────────────────┘
```

### Why This Matters

**WRONG** (what we did before):
- Built 32-bit `wineasio.so` with `-m32`
- Installed to `i386-unix/`
- Unix library never loaded!

**CORRECT** (what we do now):
- Build 64-bit `wineasio.so` with `-m64`  
- Install to `x86_64-unix/`
- Wine WoW64 loads it for 32-bit PE DLLs

### File Locations

| PE DLL | Unix .so | Install Location |
|--------|----------|------------------|
| `wineasio.dll` (32-bit) | `wineasio.so` (64-bit!) | `x86_64-unix/` |
| `wineasio64.dll` (64-bit) | `wineasio64.so` (64-bit) | `x86_64-unix/` |

**Note**: In Wine 11 WoW64, there is NO `i386-unix/` for system libraries!

### Build Configuration

```makefile
# 32-bit PE DLL
$(MINGW32) -m32 -o wineasio.dll ...

# Unix .so for 32-bit PE - MUST BE 64-bit!
$(GCC) -m64 -o wineasio.so ...
```

### Address Space Implications

Because PE and Unix run in different address spaces:

1. **Pointers cannot be shared directly**
   - A 32-bit PE pointer (0x00F85760) is invalid in 64-bit Unix space
   - A 64-bit Unix pointer (0x7fd15ad29cc0) cannot fit in 32-bit PE

2. **Buffer allocation strategy**
   - Audio buffers MUST be allocated on PE side (HeapAlloc)
   - Unix side receives these as UINT64 and uses them via WoW64 mapping
   - OR: Copy audio data through UNIX_CALL parameter structures

3. **Handle values**
   - `unix_handle` is always 64-bit (e.g., `0x7fd15ad29cc0`)
   - Stored as `UINT64` on PE side

## Current Status (2026-01-21)

### ✅ Working
- 64-bit WineASIO (all applications)
- 32-bit Unix library loading (now 64-bit binary)
- JACK client creation for test programs
- Init(), CreateBuffers() succeed

### ⚠️ In Progress
- Audio buffer pointer sharing between PE and Unix
- REAPER 32-bit audio playback

### Known Issues
- Buffer pointers from Unix side are 64-bit, truncated to 32-bit = crash
- Solution: Allocate buffers on PE side, pass to Unix

## Debug Verification

When correctly configured, you should see:

```
[WineASIO-Unix] *** Library loaded (constructor called) ***
[WineASIO-Unix] >>> asio_init() called
[WineASIO-Unix] Step 2: Opening JACK client 'WineASIO'...
[WineASIO-Unix] <<< asio_init() SUCCESS
```

If you DON'T see `[WineASIO-Unix]` messages, the 64-bit Unix library is not being loaded!

## Quick Reference

### Build Commands
```bash
# Clean and build 32-bit (creates 64-bit Unix .so)
make -f Makefile.wine11 clean
make -f Makefile.wine11 32

# Install
sudo make -f Makefile.wine11 install32
```

### Verify Installation
```bash
# Check Unix library is 64-bit
file /usr/local/lib/wine/x86_64-unix/wineasio.so
# Should say: ELF 64-bit LSB shared object, x86-64

# Check PE DLL is 32-bit
file /usr/local/lib/wine/i386-windows/wineasio.dll
# Should say: PE32 executable
```

### Test
```bash
WINEDEBUG=-all wine test_asio_interactive.exe 2>&1 | grep WineASIO-Unix
# Should show Unix-side debug output
```
