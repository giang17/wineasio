# Quick Start Guide - Next Debug Session

## Current Status (2026-01-21 Evening)

✅ **BREAKTHROUGH**: WineASIO 32-bit works perfectly in test program!
❌ **PROBLEM**: REAPER 32-bit doesn't call Start() after CreateBuffers()

## What We Know

1. **test_asio_start.exe proves WineASIO works correctly**
   - Start() succeeds
   - Callbacks fire hundreds of times
   - Full audio pipeline functional

2. **REAPER creates buffers but never starts audio**
   - CreateBuffers() succeeds
   - REAPER reports "audio close"
   - Start() never called

## Quick Commands

### Test the Working Program
```bash
cd /home/gng/docker-workspace/wineasio-fork
WINEDEBUG=-all wine test_asio_start.exe 2>&1 | grep -E "Step|OK|ERROR"
```

### Run REAPER with Debug Logging
```bash
cd /home/gng/docker-workspace/wineasio-fork
./test_reaper32_debug.sh
```

### Check JACK is Running
```bash
jack_lsp -c
```

### View Previous Logs
```bash
ls -lah ~/docker-workspace/logs/reaper32*
```

## What to Look For in REAPER Logs

1. **After CreateBuffers, look for**:
   - `[WineASIO-DBG] >>> GetSampleRate` - Is it called? What value?
   - `[WineASIO-DBG] >>> CanSampleRate` - Is it called? Does it fail?
   - `[WineASIO-DBG] >>> Start` - Is it called at all?
   - Any error messages or unusual return values

2. **Compare with test_asio_start.exe output**:
   - Same method call order?
   - Same return values?
   - What's different?

## Next Steps

### Priority 1: Run REAPER Debug Session
Run `./test_reaper32_debug.sh` and analyze the log to see:
- What REAPER calls after CreateBuffers
- Why it doesn't call Start()
- Any error conditions we're missing

### Priority 2: Add More Debug Output
If needed, add logging to these methods in `asio.c`:
- `GetLatencies()`
- `GetChannelInfo()`
- `GetSamplePosition()`
- `OutputReady()`
- `Future()`

### Priority 3: Test Other 32-bit Hosts
- Garritan CFX Lite
- Other available 32-bit DAWs

### Priority 4: Compare 32-bit vs 64-bit REAPER
Run both with same debug level and compare logs side-by-side.

## If You Need to Rebuild

```bash
cd /home/gng/docker-workspace/wineasio-fork
make -f Makefile.wine11 clean
make -f Makefile.wine11 32
sudo make -f Makefile.wine11 install
```

## Key Files

### Debug-Enhanced Files
- `asio.c` - Has debug output in Start(), Stop(), GetSampleRate(), etc.

### Test Programs
- `test_asio_start.exe` - **WORKING** - Full ASIO pipeline test
- `test_asio_thiscall.exe` - Basic test (doesn't call Start)
- `test_asio_minimal.exe` - **BROKEN** - Wrong calling convention

### Debug Scripts
- `test_reaper32_debug.sh` - Launch REAPER with logging

### Documentation
- `docs/SESSION_2026-01-21_CONTINUED.md` - Full session report
- `docs/WINE11_32BIT_DEBUG_SESSION.md` - Historical debugging
- `docs/WINE_FIX_PLAN.md` - Solution summary

## Hypotheses for REAPER Issue

1. **Return Value Check**: CreateBuffers returns 0 (success) but REAPER checks something else
2. **Buffer Validation**: REAPER validates buffer pointers/sizes more strictly than our test
3. **Sample Rate Mismatch**: REAPER queries sample rate and gets unexpected value
4. **Capability Check**: REAPER calls Future() or other capability checks we haven't logged
5. **Timing Issue**: REAPER has timeouts or thread sync issues
6. **State Machine**: REAPER expects driver to be in specific state before Start()

## The Smoking Gun

In previous log (`reaper32_wineasio_20260121_190132.log`):
```
[WineASIO-DBG] >>> CreateBuffers(numChannels=18, bufferSize=128, callbacks=00f977cc)
[WineASIO-DBG]     callbacks->bufferSwitch=0044c9c4
...
[WineASIO-DBG] DllMain: fdwReason=0  # App exits!
```

**No Start() call between CreateBuffers and exit!**

With enhanced debug output, we should now see:
- What REAPER calls between CreateBuffers and exit
- What return values it gets
- Why it decides not to call Start()

## Success Criteria

Session is successful when we identify:
1. **What** REAPER calls after CreateBuffers that our test doesn't
2. **Why** that causes REAPER to skip Start()
3. **How** to fix it

## Remember

- **WineASIO code is correct** - proven by test_asio_start.exe
- **Focus on REAPER behavior** - what's it checking that fails?
- **Use test program as baseline** - it shows correct behavior
- **Compare 32-bit vs 64-bit** - 64-bit works, what's different?

Good luck debugging! 🚀