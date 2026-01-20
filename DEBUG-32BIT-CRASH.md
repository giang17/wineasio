# WineASIO 32-bit NULL Pointer Crash - Debug Progress

## Status: CRITICAL BUG IDENTIFIED

**Last Updated:** 2025-01-20  
**Branch:** `wine11-32bit-test`  
**Severity:** High - Affects all 32-bit ASIO hosts

---

## Problem Summary

WineASIO 32-bit loads and initializes successfully but **crashes immediately** after `asio_init` completes with a NULL pointer dereference.

### Crash Signature
```
Unhandled exception: page fault on execute access to 0x00000000
EIP:00000000  <- Attempting to execute code at NULL!
EAX:00000000  <- NULL pointer in register
```

### Affected Applications
- ✅ **64-bit apps**: Work perfectly (CFX Lite 64-bit tested)
- ❌ **REAPER 32-bit**: Crashes at NULL (0x60)
- ❌ **CFX Lite 32-bit**: Crashes at NULL (0x00)

### Wine Versions Tested
- ❌ Custom Wine 11 (new WoW64): Crashes
- ❌ wine-stable 11 (old WoW64): Crashes
- **Conclusion**: NOT a WoW64 architecture issue - likely WineASIO bug

---

## What We Know

### ✅ Working Parts
1. **Unix library loading**: Both 32-bit and 64-bit SOs load correctly
2. **Registration**: `regsvr32` succeeds for both architectures
3. **JACK initialization**: Connects successfully, MIDI ports registered
4. **asio_init**: Completes successfully with all parameters correct
   - 16 inputs, 16 outputs configured
   - Sample rate: 48000 Hz
   - Buffer size: 128 samples
   - MIDI enabled

### ❌ Crash Point
- **When**: Immediately after `asio_init` returns
- **What**: Host application attempts to call an ASIO function
- **Why**: Function pointer in vtable is NULL or points to invalid address
- **Observation**: NO other ASIO functions are called before crash
  - No `CreateBuffers`
  - No `Start`
  - No `GetChannels`
  - Nothing!

### Debug Output Before Crash
```
wineasio:trace: asio_init called
wineasio:trace: Initializing WineASIO
wineasio:trace: JACK MIDI functions loaded
wineasio:trace: JACK library loaded successfully
wineasio:trace: JACK client opened as 'WineASIO'
wineasio:trace: JACK MIDI ports registered: midi_in, midi_out
wineasio:trace: Sample rate changed to 48000
wineasio:trace: Buffer size changed to 128
wineasio:trace: WineASIO initialized: 16 inputs, 16 outputs, 48000 Hz, 128 samples, MIDI=enabled
[CRASH HERE - NO MORE FUNCTION CALLS]
```

---

## Hypothesis

### Primary Suspect: ASIO COM Interface vtable Problem

The crash pattern suggests the host application is calling an ASIO function immediately after `Init`, but the function pointer is NULL.

**Possible causes:**
1. **vtable layout mismatch** - 32-bit vs 64-bit structure alignment
2. **Calling convention issue** - `STDMETHODCALLTYPE` may be wrong for 32-bit
3. **Function signature mismatch** - Return type or parameters incorrect
4. **Missing function implementation** - One vtable entry is NULL

### Functions Called After Init (typical ASIO sequence)
According to ASIO SDK documentation, hosts typically call in this order:
1. `Init()` ✅ Works
2. `GetChannels()` ❌ Never reached
3. `GetBufferSize()` ❌ Never reached
4. `CanSampleRate()` ❌ Never reached
5. `GetSampleRate()` ❌ Never reached
6. `CreateBuffers()` ❌ Never reached

**But the crash is at 0x00 or 0x60** - this is a NULL function pointer call!

### vtable Structure (from asio_pe.c)
```c
static const IWineASIOVtbl WineASIO_Vtbl = {
    QueryInterface,        // 0
    AddRef,                // 4
    Release,               // 8
    Init,                  // 12  ✅ Works
    GetDriverName,         // 16  ❓ void return (unusual!)
    GetDriverVersion,      // 20
    GetErrorMessage,       // 24  ❓ void return (unusual!)
    Start,                 // 28
    Stop,                  // 32
    GetChannels,           // 36
    GetLatencies,          // 40
    GetBufferSize,         // 44
    CanSampleRate,         // 48
    GetSampleRate,         // 52
    SetSampleRate,         // 56
    GetClockSources,       // 60  ← Crash at 0x60?
    SetClockSource,        // 64
    GetSamplePosition,     // 68
    GetChannelInfo,        // 72
    CreateBuffers,         // 76
    DisposeBuffers,        // 80
    ControlPanel,          // 84
    Future,                // 88
    OutputReady            // 92
};
```

**CRITICAL OBSERVATION**: Crash at offset 0x60 = 96 decimal = **possible GetClockSources offset!**

---

## Investigation Steps Completed

### 1. Installation Verification ✅
- [x] 32-bit PE DLL in `/opt/wine-stable/lib/wine/i386-windows/`
- [x] 32-bit Unix SO (ELF 32-bit) in `/opt/wine-stable/lib/wine/i386-unix/`
- [x] Both marked as Wine builtins with `winebuild --builtin`
- [x] Registered with 32-bit regsvr32

### 2. Debug Builds ✅
- [x] Compiled with `-DWINEASIO_DEBUG`
- [x] Compiled with `-O0 -g` (no optimization, debug symbols)
- [x] Added TRACE to all asio_* functions
- [x] Added NULL pointer checks in jack_process_callback

### 3. Comparison Tests ✅
- [x] Tested with custom Wine 11 (new WoW64) - Same crash
- [x] Tested with wine-stable 11 (old WoW64) - Same crash
- [x] Tested 64-bit WineASIO - Works perfectly
- [x] Tested multiple 32-bit hosts - All crash identically

---

## Next Steps for Debugging

### Priority 1: Test Legacy WineASIO
**Goal**: Determine if bug is in Wine 11 port or pre-existing

The legacy WineASIO (without PE/Unix split) worked with Wine 6-10. We should:
1. Checkout legacy branch or old tag
2. Build with old Makefile (not Makefile.wine11)
3. Test with wine-stable on 32-bit apps
4. **If legacy works**: Bug is in Wine 11 port (PE/Unix architecture)
5. **If legacy crashes**: Bug was pre-existing (deeper ASIO issue)

**Command to test legacy:**
```bash
cd wineasio-fork
git checkout v1.3.0  # Last pre-Wine-11 version
make 32
sudo cp build32/wineasio32.dll /opt/wine-stable/lib/wine/i386-windows/
sudo cp build32/wineasio32.dll.so /opt/wine-stable/lib/wine/i386-unix/
wine regsvr32 wineasio32.dll
# Test with REAPER 32-bit
```

### Priority 2: vtable Deep Inspection
1. **Dump vtable at runtime** - Add debug code to print all function pointers
2. **Compare with ASIO SDK** - Verify vtable layout matches standard
3. **Check calling conventions** - Ensure STDMETHODCALLTYPE is correct for 32-bit
4. **Verify void returns** - `GetDriverName` and `GetErrorMessage` return void (unusual for COM)

### Priority 3: Minimal Test Case
Create a minimal 32-bit ASIO host that:
1. Calls `CoCreateInstance` to instantiate WineASIO
2. Calls `Init(NULL)`
3. Attempts to call next function in sequence
4. Prints vtable addresses before each call

This would isolate the exact failing function.

---

## Code Locations

### Key Files
- **PE side (Windows)**: `asio_pe.c` - Lines 800-826 (vtable definition)
- **Unix side (Linux)**: `asio_unix.c` - All asio_* functions
- **Interface definition**: `unixlib.h` - Function enum and params
- **Build system**: `Makefile.wine11`

### Modified Files (Uncommitted)
- None currently - all changes committed to `wine11-32bit-test` branch

---

## Test Logs Location
- REAPER 32-bit crash: `/home/gng/docker-workspace/logs/1backtrace.txt`
- CFX Lite 32-bit crash: `/home/gng/docker-workspace/logs/2backtrace.txt`
- Debug output: `/tmp/reaper_all_traces.log`, `/tmp/cfx_lite_debug.log`

---

## Environment Details
- **OS**: Ubuntu 24.04, Kernel 6.14.0-1019-oem
- **Wine Custom**: `/usr/local/bin/wine` (Wine 11.0, new WoW64)
- **Wine Stable**: `/usr/bin/wine` → `/opt/wine-stable/bin/wine` (Wine 11.0, old WoW64)
- **JACK**: Running (PID 27361), ports available
- **Test Prefix**: `/home/gng/wine-test-winestable`

---

## Resources
- Wine 11 Porting Guide: `WINE11_PORTING.md`
- Wine Versions Clarity: `WINE-VERSIONS-CLARITY.md`
- Original Issue: `ISSUE-32BIT-SUPPORT.md`
- Success Document: `WINE11-32BIT-SUCCESS.md`

---

## Questions for Next Session

1. **Does legacy WineASIO (pre-Wine-11) work with 32-bit apps?**
   - If YES: Bug is in Wine 11 PE/Unix split implementation
   - If NO: Bug was pre-existing (may never have worked)

2. **What is the EXACT function being called when crash occurs?**
   - Need to dump vtable addresses at runtime
   - Compare with crash EIP address

3. **Are there any working 32-bit ASIO drivers on Wine 11?**
   - Test with ASIO4ALL or other native Windows ASIO drivers
   - Would confirm if it's WineASIO-specific or Wine-wide

4. **Is the ASIO COM interface definition correct?**
   - Compare with official Steinberg ASIO SDK
   - Check if any methods changed between ASIO versions

---

## Workaround for Users

**Use 64-bit applications** - WineASIO 64-bit works perfectly:
- CFX Lite 64-bit: ✅ Tested and working
- REAPER 64-bit: Expected to work
- FL Studio 64-bit: Expected to work

**32-bit support is currently broken** and requires code fixes before release.