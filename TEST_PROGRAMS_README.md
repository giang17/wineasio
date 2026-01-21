# WineASIO Test Programs

This directory contains test programs for debugging and validating WineASIO 32-bit functionality.

## Test Programs Overview

### ✅ test_asio_start.exe - **RECOMMENDED TEST**

**Purpose**: Comprehensive test of the full ASIO audio pipeline, including Start() method.

**What it tests**:
1. COM initialization and WineASIO instance creation
2. Init() - Driver initialization
3. GetChannels() - Input/output channel enumeration
4. GetBufferSize() - Buffer size capabilities
5. GetSampleRate() - Current sample rate
6. CanSampleRate() - Sample rate validation
7. CreateBuffers() - Audio buffer allocation
8. **Start()** - Audio processing start (THE KEY TEST!)
9. Stop() - Audio processing stop
10. DisposeBuffers() - Buffer cleanup

**Result**: ✅ **PASSES** - Proves WineASIO 32-bit works correctly!

**Usage**:
```bash
# Full output
wine test_asio_start.exe

# Filtered output (recommended)
WINEDEBUG=-all wine test_asio_start.exe 2>&1 | grep -E "Step|OK|ERROR|CALLBACK"
```

**Expected output**:
```
Step 10: CreateBuffers → OK
Step 11: Start() → OK: Start() succeeded!
[CALLBACK] swapBuffers (hundreds of times during 2 second test)
Step 12: Stop() → OK
```

**Source**: `test_asio_start.c`

---

### ✅ test_asio_thiscall.exe - Basic Test

**Purpose**: Tests basic ASIO methods with correct thiscall calling convention.

**What it tests**:
1. COM initialization
2. WineASIO instance creation
3. GetDriverName()
4. GetDriverVersion()
5. Init()
6. GetChannels()
7. Release()

**Result**: ✅ **PASSES**

**Usage**:
```bash
wine test_asio_thiscall.exe

# Or with filtered output:
WINEDEBUG=-all wine test_asio_thiscall.exe 2>&1 | grep -E "WineASIO|Test|OK|ERROR"
```

**Source**: `test_asio_thiscall.c`

---

### ❌ test_asio_minimal.exe - **BROKEN - DO NOT USE**

**Purpose**: Original minimal test (kept for reference only).

**Problem**: Uses **stdcall** instead of **thiscall** calling convention.

**Result**: ❌ **CRASHES** - Calling convention mismatch

**Why it fails**:
- ASIO methods on 32-bit Windows use **thiscall** (this pointer in ECX register)
- This test uses **stdcall** (this pointer on stack)
- Causes stack corruption and crashes

**Do not use this test!** Use `test_asio_thiscall.exe` or `test_asio_start.exe` instead.

**Source**: `test_asio_minimal.c`

---

## Compilation

### Prerequisites
```bash
sudo apt-get install gcc-mingw-w64-i686
```

### Build Commands

```bash
# Comprehensive Start() test (recommended)
i686-w64-mingw32-gcc -o test_asio_start.exe test_asio_start.c -lole32 -luuid

# Basic thiscall test
i686-w64-mingw32-gcc -o test_asio_thiscall.exe test_asio_thiscall.c -lole32 -luuid

# Broken minimal test (DO NOT USE)
i686-w64-mingw32-gcc -o test_asio_minimal.exe test_asio_minimal.c -lole32 -luuid
```

---

## Running Tests

### Before Running Tests

1. **Ensure JACK is running**:
   ```bash
   # Check if JACK is running
   jack_lsp
   
   # Start JACK if needed (via PipeWire-JACK or jackd)
   # PipeWire usually starts automatically
   ```

2. **Ensure WineASIO is registered**:
   ```bash
   wine regsvr32 wineasio.dll
   
   # Verify registration
   wine reg query 'HKLM\Software\ASIO\WineASIO'
   ```

### Running with Debug Output

```bash
# Minimal Wine output + WineASIO debug
WINEDEBUG=+wineasio,-all wine test_asio_start.exe

# No Wine output at all (cleanest)
WINEDEBUG=-all wine test_asio_start.exe 2>&1 | grep -E "WineASIO|Step|OK|ERROR"
```

---

## Debug Scripts

### test_reaper32_debug.sh

**Purpose**: Launch REAPER 32-bit with comprehensive debug logging.

**Usage**:
```bash
cd /home/gng/docker-workspace/wineasio-fork
./test_reaper32_debug.sh
```

**Output**: Saves log to `~/docker-workspace/logs/reaper32_debug_TIMESTAMP.log`

**What it logs**:
- All WineASIO method calls
- Return values and error codes
- Buffer information
- State transitions

---

## Interpreting Test Results

### Success Indicators

✅ **test_asio_start.exe should show**:
- All 15 steps complete
- Start() returns success
- Hundreds of callback invocations during 2-second sleep
- Stop() succeeds
- Clean shutdown

✅ **Debug output should show**:
```
[WineASIO-DBG] >>> Start(iface=XXXXXXXX)
[WineASIO-DBG]     Start SUCCESS - driver_state now Running
[CALLBACK] swapBuffers(index=0, processNow=1)
[CALLBACK] swapBuffers(index=1, processNow=1)
...
```

### Failure Indicators

❌ **Bad signs**:
- Crash before completing all steps
- Start() returns non-zero error code
- No callbacks during sleep period
- Error messages in debug output

---

## Known Issues

### Issue 1: REAPER 32-bit Doesn't Call Start()

**Status**: Under investigation

**Symptoms**:
- REAPER loads WineASIO
- CreateBuffers() succeeds
- REAPER reports "audio close"
- Start() never called

**Evidence**:
- `test_asio_start.exe` works perfectly → WineASIO code is correct
- REAPER 32-bit doesn't call Start() → REAPER-specific validation issue

**Next steps**:
1. Run `./test_reaper32_debug.sh` to see what REAPER does after CreateBuffers
2. Compare REAPER 32-bit vs 64-bit behavior
3. Test with other 32-bit DAWs (Garritan CFX Lite)

### Issue 2: Crash on Second REAPER Start

**Status**: Under investigation

**Symptoms**:
- First REAPER start with WineASIO: CreateBuffers succeeds but no audio
- Close REAPER
- Second start: Crash

**Workaround**:
- Use clean Wine prefix for testing
- Or reset REAPER audio settings before each test

---

## Technical Notes

### Calling Convention: thiscall

On 32-bit Windows, ASIO methods use **thiscall**:
- `this` pointer in **ECX register** (NOT on stack!)
- Other arguments pushed right-to-left on stack
- Callee cleans up the stack

**Example**:
```c
// Calling Start() with thiscall
static inline LONG call_thiscall_0(void *func, IWineASIO *pThis)
{
    LONG result;
    __asm__ __volatile__ (
        "movl %1, %%ecx\n\t"    // Put 'this' in ECX
        "call *%2\n\t"          // Call the function
        : "=a" (result)
        : "r" (pThis), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}
```

### Why stdcall Doesn't Work

**stdcall** pushes `this` on the stack, but ASIO expects it in ECX:
- Stack layout becomes wrong
- Function reads garbage from wrong memory locations
- Crash or memory corruption

### COM Interface Structure

WineASIO implements:
- **IUnknown** methods (QueryInterface, AddRef, Release) use **stdcall**
- **IASIO** methods (Init, Start, Stop, etc.) use **thiscall**

Both calling conventions in one vtable!

---

## Quick Reference

### Test the Core Functionality
```bash
wine test_asio_start.exe
```

### Debug REAPER Issue
```bash
./test_reaper32_debug.sh
```

### Check JACK
```bash
jack_lsp -c
```

### Rebuild WineASIO
```bash
make -f Makefile.wine11 clean
make -f Makefile.wine11 32
sudo make -f Makefile.wine11 install
```

---

## Files in This Directory

- `test_asio_start.c` / `.exe` - **USE THIS** - Full pipeline test
- `test_asio_thiscall.c` / `.exe` - Basic test with correct calling convention
- `test_asio_minimal.c` / `.exe` - **DON'T USE** - Broken calling convention
- `test_reaper32_debug.sh` - REAPER debug launcher
- `TEST_PROGRAMS_README.md` - This file

---

## Further Reading

- `docs/SESSION_2026-01-21_CONTINUED.md` - Latest debug session results
- `docs/WINE11_32BIT_DEBUG_SESSION.md` - Historical debugging journey
- `docs/WINE_FIX_PLAN.md` - Solution summary
- `NEXT_SESSION_QUICKSTART.md` - Quick start for next debugging session

---

## Summary

**The good news**: WineASIO 32-bit code is correct and fully functional!

**The challenge**: REAPER 32-bit has specific validation requirements that prevent it from calling Start().

**The tool**: Use `test_asio_start.exe` as a baseline to understand correct behavior, then compare with REAPER's behavior using the enhanced debug logging.