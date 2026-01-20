# WineASIO 32-bit vtable Debugging Guide

## Overview

This guide helps debug the 32-bit WineASIO NULL pointer crash by instrumenting the vtable (virtual function table) to see which ASIO function is being called when the crash occurs.

## The Problem

- **64-bit WineASIO**: ✅ Works perfectly
- **32-bit WineASIO**: ❌ Crashes immediately after `Init()` with NULL pointer dereference
- **Crash signature**: `page fault on execute access to 0x00000000` or `0x00000060`

## Instrumentation Added

### 1. vtable Pointer Dump
After creating a WineASIO instance, all 24 function pointers are printed:
```
=== VTABLE DUMP (pAsio=0x..., lpVtbl=0x...) ===
vtable[0x00] QueryInterface   = 0x7b4xxxxx
vtable[0x04] AddRef            = 0x7b4xxxxx
...
vtable[0x5C] OutputReady       = 0x7b4xxxxx
=== END VTABLE DUMP ===
```

### 2. Function Call Logging
Every ASIO function logs entry and exit at WARN level:
```
>>> CALLED: Init(iface=0x..., sysRef=0x...)
<<< RETURNING from Init: SUCCESS (1)
>>> CALLED: GetChannels(iface=0x..., ...)
```

This shows **exactly** which function is called before the crash.

## Quick Start

### Prerequisites
- JACK running (`jackdbus auto` or `jackd`)
- REAPER 32-bit installed in wine-test-winestable prefix
- wine-stable installed at `/opt/wine-stable`

### Step 1: Build Debug Version
```bash
cd ~/docker-workspace/wineasio-fork
make -f Makefile.wine11 clean
make -f Makefile.wine11 32
```

### Step 2: Install Debug Build
```bash
./install-32bit-debug.sh
```
This will:
- Copy `wineasio.dll` to `/opt/wine-stable/lib/wine/i386-windows/`
- Copy `wineasio.so` to `/opt/wine-stable/lib/wine/i386-unix/`
- Register with 32-bit regsvr32
- **Requires sudo password**

### Step 3: Run REAPER with vtable Logging
```bash
./test-reaper-32bit-vtable.sh
```

The script will:
- Check JACK is running
- Set `WINEDEBUG=warn+wineasio`
- Launch REAPER 32-bit
- Save output to `/tmp/reaper_vtable_debug.log`
- Analyze results automatically

## What to Look For

### 1. vtable Dump
Check if all function pointers are valid (non-zero):
```
vtable[0x3C] GetClockSources   = 0x7b412340  ← Valid
vtable[0x40] SetClockSource    = 0x00000000  ← NULL! (BUG!)
```

If any pointer is `0x00000000` or `(nil)`, that function is not implemented or vtable is corrupt.

### 2. Function Call Sequence
```
>>> CALLED: Init(...)
<<< RETURNING from Init: SUCCESS (1)
>>> CALLED: GetChannels(...)     ← Last function before crash
[CRASH]
```

The last `>>> CALLED:` line shows which function was entered before the crash.

### 3. Crash Address
```
Unhandled exception: page fault on execute access to 0x00000060
EIP:00000060
```

- **If EIP = 0x00000000**: Function pointer is NULL
- **If EIP = 0x00000060**: Trying to execute at offset 0x60 (likely vtable corruption)
- **If EIP = valid address**: Crash inside the function (different problem)

### 4. Compare Crash Offset with vtable
Crash at `0x60` (96 decimal)? Look at vtable offsets:
```
vtable[0x3C] = offset 60 decimal  ← GetClockSources!
vtable[0x40] = offset 64 decimal
```

So crash at 0x60 means `GetClockSources` is being called (or its pointer is invalid).

## vtable Layout Reference

| Offset | Function           | Description                    |
|--------|--------------------|--------------------------------|
| 0x00   | QueryInterface     | COM interface query            |
| 0x04   | AddRef             | COM reference counting         |
| 0x08   | Release            | COM release                    |
| 0x0C   | Init               | Initialize ASIO driver         |
| 0x10   | GetDriverName      | Get driver name string         |
| 0x14   | GetDriverVersion   | Get version number             |
| 0x18   | GetErrorMessage    | Get last error message         |
| 0x1C   | Start              | Start audio processing         |
| 0x20   | Stop               | Stop audio processing          |
| 0x24   | GetChannels        | Get input/output channel count |
| 0x28   | GetLatencies       | Get input/output latencies     |
| 0x2C   | GetBufferSize      | Get min/max/preferred buffer   |
| 0x30   | CanSampleRate      | Check if sample rate supported |
| 0x34   | GetSampleRate      | Get current sample rate        |
| 0x38   | SetSampleRate      | Set new sample rate            |
| 0x3C   | GetClockSources    | Get available clock sources    |
| 0x40   | SetClockSource     | Set active clock source        |
| 0x44   | GetSamplePosition  | Get current sample position    |
| 0x48   | GetChannelInfo     | Get info about a channel       |
| 0x4C   | CreateBuffers      | Allocate audio buffers         |
| 0x50   | DisposeBuffers     | Free audio buffers             |
| 0x54   | ControlPanel       | Show driver control panel      |
| 0x58   | Future             | Reserved for future use        |
| 0x5C   | OutputReady        | Signal output is ready         |

## Typical ASIO Initialization Sequence

A well-behaved ASIO host calls functions in this order:

1. **CoCreateInstance** → Creates ASIO instance
2. **Init(NULL)** → Initializes driver
3. **GetChannels()** → Queries channel count
4. **GetBufferSize()** → Queries buffer size limits
5. **CanSampleRate()** → Checks sample rate support
6. **GetSampleRate()** → Gets current rate
7. **CreateBuffers()** → Allocates buffers
8. **Start()** → Starts audio processing

If crash happens before step 3, the host is calling something unexpected!

## Possible Root Causes

### A. NULL Function Pointer
- **Symptom**: vtable dump shows `0x00000000` for some function
- **Cause**: Function not implemented or vtable incorrectly initialized
- **Fix**: Implement missing function or fix vtable initialization

### B. vtable Corruption
- **Symptom**: Crash at offset like 0x60 but vtable dump looks OK
- **Cause**: Host is accessing wrong vtable offset (calling convention mismatch)
- **Fix**: Check 32-bit stdcall decorations, vtable alignment, or struct packing

### C. Calling Convention Mismatch
- **Symptom**: Crash inside valid function, stack corruption
- **Cause**: 32-bit Windows uses `__stdcall` but implementation uses wrong convention
- **Fix**: Verify all functions use `STDMETHODCALLTYPE` (stdcall)

### D. Thunk Layer Problem
- **Symptom**: Crash when calling Unix side from PE side
- **Cause**: PE→Unix calling convention translation broken for 32-bit
- **Fix**: Debug `UNIX_CALL` macro and parameter marshaling

## Manual Testing

If automated script doesn't work, test manually:

```bash
# Start JACK
jackdbus auto

# Set environment
export WINEPREFIX="$HOME/wine-test-winestable"
export WINEDEBUG="warn+wineasio"

# Run REAPER and save log
cd "$WINEPREFIX/drive_c/Program Files (x86)/REAPER"
/opt/wine-stable/bin/wine reaper.exe 2>&1 | tee /tmp/reaper_manual_test.log

# Analyze log
grep "VTABLE DUMP" /tmp/reaper_manual_test.log -A 30
grep ">>> CALLED:" /tmp/reaper_manual_test.log
grep "page fault" /tmp/reaper_manual_test.log -A 5
```

## Expected Results

### Successful Case (no crash)
```
=== VTABLE DUMP === 
vtable[0x00] QueryInterface   = 0x7b412000
vtable[0x04] AddRef            = 0x7b412020
[all entries non-zero]
=== END VTABLE DUMP ===

>>> CALLED: Init(iface=0x..., sysRef=0x...)
<<< RETURNING from Init: SUCCESS (1)
>>> CALLED: GetChannels(iface=0x..., ...)
<<< RETURNING from GetChannels: ASE_OK
>>> CALLED: GetBufferSize(...)
[continues normally]
```

### Failure Case (current bug)
```
=== VTABLE DUMP ===
vtable[0x00] QueryInterface   = 0x7b412000
[all entries non-zero]
=== END VTABLE DUMP ===

>>> CALLED: Init(iface=0x..., sysRef=0x...)
<<< RETURNING from Init: SUCCESS (1)
[CRASH - No more >>> CALLED: lines]

Unhandled exception: page fault on execute access to 0x00000000
EIP:00000000
```

## Next Steps After Results

### If vtable Entry is NULL
1. Find which function is NULL in `asio_pe.c` vtable definition
2. Check if function is implemented
3. Verify function signature matches `IWineASIOVtbl` interface

### If All Pointers Valid But Still Crashes
1. Test with legacy WineASIO (pre-Wine-11) to see if bug is new
2. Check 32-bit calling conventions (stdcall vs cdecl)
3. Compare with 64-bit implementation (working)
4. Debug the thunk layer (PE ↔ Unix calls)

### If Crash Inside Valid Function
1. Add more detailed logging to that specific function
2. Check parameter passing (stack alignment, register usage)
3. Verify Unix side receives correct parameters
4. Test Unix function in isolation

## Files Modified

- `asio_pe.c`: Lines 847-875 (vtable dump), lines 429+ (function logging)
- `install-32bit-debug.sh`: Installation script
- `test-reaper-32bit-vtable.sh`: Testing script
- `DEBUG-32BIT-CRASH.md`: Updated with instrumentation details

## Related Documents

- `DEBUG-32BIT-CRASH.md` - Full debug progress and analysis
- `WINE11_PORTING.md` - Wine 11 PE/Unix architecture details
- `WINE-VERSIONS-CLARITY.md` - Wine installation overview
- `ISSUE-32BIT-SUPPORT.md` - Original issue description

## Support

If you encounter issues:
1. Check JACK is running: `pgrep jackd || pgrep jackdbus`
2. Verify installation: `ls -l /opt/wine-stable/lib/wine/i386-{windows,unix}/wineasio.*`
3. Check registry: `cat ~/.wine-test-winestable/system.reg | grep -i wineasio`
4. Enable full trace: `export WINEDEBUG=+all,warn+wineasio`

---

**Remember**: The goal is to identify which function pointer is NULL or which function is being called when the crash occurs. Once we know that, we can fix the specific problem!