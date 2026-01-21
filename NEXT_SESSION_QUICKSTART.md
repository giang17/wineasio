# Quick Start Guide - Next Debug Session

## Current Status (2026-01-21 Late Evening)

❌ **NEW FINDING**: `Init()` fails silently - JACK connection not established!
✅ **Test program works**: `test_asio_start.exe` creates JACK ports and audio works
❌ **Real hosts fail**: REAPER and CFX Lite - Init() fails, no JACK ports created

## What We Now Know

### The Real Problem: Init() Fails

Latest debug log shows:
```
[WineASIO-DBG] >>> Init(iface=01741620, sysRef=0005022c)
[WineASIO-DBG] DllMain: fdwReason=2   # Thread created
[WineASIO-DBG] DllMain: fdwReason=3   # Thread destroyed
[WineASIO-DBG] DllMain: fdwReason=0   # Process detach - EXIT!
```

**No `<<< Init returning SUCCESS` message!** Init() is failing.

### Test Program vs Real Hosts

| Aspect | test_asio_start.exe | REAPER/CFX Lite |
|--------|---------------------|-----------------|
| Init() | ✅ SUCCESS | ❌ FAILS |
| JACK ports | ✅ Created (52 ports) | ❌ Not created |
| CreateBuffers | ✅ Called | ❌ Never reached |
| Start() | ✅ Works | ❌ Never reached |
| Audio | ✅ Callbacks fire | ❌ None |

### Why Test Works But Real Hosts Don't

The difference must be in how/when `Init()` is called:
1. **sysRef parameter**: Test uses `NULL`, REAPER uses `0005022c` (window handle?)
2. **Timing**: Real hosts may have stricter timing requirements
3. **Threading**: Real hosts call from different thread context
4. **JACK state**: Something about JACK connection fails for real hosts

## Priority 1: Debug Init() Failure

### Add Debug to Unix-side Init

The crash/failure happens in `asio_unix.c` → `asio_init()`:
- Check if JACK client opens
- Check if ports register
- Check if activate succeeds

```bash
# Edit asio_unix.c and add debug to asio_init():
# - Before/after pjack_client_open()
# - Before/after pjack_activate()
```

### Quick Commands

```bash
cd /home/gng/docker-workspace/wineasio-fork

# Rebuild with debug
make -f Makefile.wine11 32
sudo make -f Makefile.wine11 install

# Test working program (should show JACK ports)
WINEDEBUG=-all wine test_asio_start.exe 2>&1 | grep -E "Init|JACK|step"

# Test REAPER (Init will fail)
WINEDEBUG=-all wine "$HOME/.wine/drive_c/Program Files (x86)/REAPER/reaper.exe" 2>&1 | head -30
```

## Key Files to Modify

### asio_unix.c - Add Debug to asio_init()

Around line 517-600, add `WARN()` or `fprintf(stderr, ...)` calls:
1. Before `pjack_client_open()` 
2. After `pjack_client_open()` - check if client is NULL
3. Before `pjack_activate()`
4. After `pjack_activate()` - check return value

### asio_pe.c - Already Has Debug

Init() already logs:
- `>>> Init(iface=..., sysRef=...)`
- `<<< Init FAILED: Unix init returned %d` (if it fails)
- `<<< Init returning SUCCESS` (if it succeeds)

**We're NOT seeing the SUCCESS message = Unix side fails!**

## Hypotheses for Init() Failure

1. **JACK client name conflict**: Different client name handling
2. **32-bit JACK library issue**: libjack 32-bit not loading correctly
3. **PipeWire-JACK 32-bit**: May not work same as 64-bit
4. **Thread context**: JACK doesn't like being called from certain threads
5. **sysRef handling**: Non-NULL sysRef causes different code path

## Debug Strategy

### Step 1: Add Unix-side Logging
```c
// In asio_unix.c asio_init():
fprintf(stderr, "[WineASIO-Unix] Opening JACK client '%s'...\n", stream->client_name);
stream->client = pjack_client_open(stream->client_name, options, &status);
fprintf(stderr, "[WineASIO-Unix] pjack_client_open returned: client=%p, status=0x%x\n", 
        stream->client, status);
```

### Step 2: Check 32-bit JACK Library
```bash
# Is 32-bit libjack available?
ls -la /usr/lib/i386-linux-gnu/libjack*

# Or via ldconfig
ldconfig -p | grep jack | grep i386
```

### Step 3: Test JACK 32-bit Directly
```bash
# Simple 32-bit JACK client test
# (need to create or find one)
```

## Files

### Source Files
- `asio_pe.c` - PE side, has debug output ✅
- `asio_unix.c` - Unix side, **NEEDS MORE DEBUG** ⚠️

### Test Programs  
- `test_asio_start.exe` - **WORKS** - proves code is correct
- `test_asio_thiscall.exe` - Basic test, works
- `test_asio_minimal.exe` - **BROKEN** - wrong calling convention

### Logs
- `~/docker-workspace/logs/reaper32_*.log` - Various debug sessions

## Summary

**The problem is NOT in CreateBuffers or Start()!**

**The problem is in Init() → Unix side → JACK connection!**

The test program works because something is different about how it calls Init() compared to real ASIO hosts. We need to debug the Unix-side `asio_init()` function to find out why JACK client creation fails for real hosts.

## Next Session Checklist

1. [ ] Add debug output to `asio_unix.c` → `asio_init()`
2. [ ] Rebuild and test with REAPER
3. [ ] Compare Init() parameters between test and REAPER
4. [ ] Check if 32-bit libjack is loading correctly
5. [ ] Test if JACK client opens at all for real hosts
6. [ ] Find the exact line where Init() fails

Good luck! 🔍