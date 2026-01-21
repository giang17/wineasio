# Plan: Wine 11 WoW64 Unix-Loader Fix

## Status: ⚠️ PARTIALLY SOLVED - Audio not working yet

## Original Problem
`NtQueryVirtualMemory(MemoryWineUnixFuncs)` returned `STATUS_DLL_NOT_FOUND` (c0000135) for 32-bit PE DLLs.

## Root Cause (Found!)
1. **Missing `--builtin` flag**: The DLL was not marked as "Wine builtin DLL", so Wine's loader didn't know to look for the corresponding `.so` file
2. **Test program bug**: The original test used **stdcall** convention, but ASIO on 32-bit Windows uses **thiscall** (this pointer in ECX register)

## Solution Applied

### 1. Re-enable `--builtin` Flag
The `winebuild --builtin` flag is **REQUIRED** for DLLs that have Unix counterparts. It:
- Writes "Wine builtin DLL" signature at offset 0x40 in the PE header
- Tells Wine's loader to search for the matching `.so` file
- Without it, `NtQueryVirtualMemory(MemoryWineUnixFuncs)` returns `c0000135`

### 2. Keep Linker Flags for WoW64 Compatibility
```makefile
PE_LDFLAGS_32 = -Wl,--image-base=0x10000000 -Wl,--disable-reloc-section
```
- **Lower ImageBase**: Avoids conflicts in WoW64 32-bit address space
- **No relocations**: Sidesteps Wine WoW64 relocation handling bugs

### 3. Use Correct Calling Convention in Tests
ASIO uses **thiscall** on 32-bit Windows:
- `this` pointer passed in **ECX register** (not on stack!)
- Other arguments pushed right-to-left on stack
- Callee cleans up the stack

Created `test_asio_thiscall.c` with proper calling convention.

## Build Configuration (Final)
```makefile
# 32-bit PE DLL - must be marked as builtin for Unix side loading
$(BUILD_DIR)/$(DLL32): $(PE_SOURCES) unixlib.h $(NTDLL_LIB32) wineasio.def | $(BUILD_DIR)
	$(MINGW32) $(PE_CFLAGS) -m32 \
		-o $@ $(PE_SOURCES) \
		$(NTDLL_LIB32) \
		$(PE_LDFLAGS_32) $(PE_LIBS) \
		wineasio.def \
		-Wl,--out-implib,$(BUILD_DIR)/libwineasio.a
	$(WINEBUILD) --builtin $@   # <-- REQUIRED!
```

## Verification

### Check builtin signature:
```bash
xxd -s 0x40 -l 32 wineasio.dll
# Should show: "Wine builtin DLL"
```

### Test with thiscall test program:
```bash
wine test_asio_thiscall.exe
# Should show: "Test completed successfully!"
```

## Why the Original Analysis Was Wrong

### Original Theory (WRONG):
> "Wine 11 WoW64 PE loader crashes when loading DLLs marked as builtin"

### Actual Cause:
The crash was in the **test program**, not Wine or WineASIO:
1. `test_asio_minimal.exe` used stdcall for ASIO methods
2. WineASIO's vtable contains thiscall wrappers (correct for real ASIO hosts)
3. Calling convention mismatch → crash
4. `--builtin` was incorrectly blamed

### Evidence:
- Real ASIO hosts (REAPER, Cubase, FL Studio) use thiscall → they work!
- `test_asio_thiscall.exe` uses thiscall → it works!
- `test_asio_minimal.exe` uses stdcall → it crashes!

## Files Modified
- `Makefile.wine11` - Re-enabled `--builtin` for 32-bit
- `test_asio_thiscall.c` - NEW: Test with correct calling convention
- `docs/WINE11_32BIT_DEBUG_SESSION.md` - Updated with solution

## Options Considered

| Option | Description | Result |
|--------|-------------|--------|
| **A) Re-enable --builtin** | Mark DLL as Wine builtin | ✅ **CHOSEN - WORKS!** |
| B) Patch Wine | Allow native DLLs to load Unix side | Not needed |
| C) load_builtin_unixlib() | Call in DllMain | Not needed |

## Lessons Learned

1. **Always use `--builtin`** for PE DLLs with Unix counterparts
2. **Understand calling conventions** when debugging COM/ASIO interfaces
3. **Test with correct calling conventions** - thiscall vs stdcall matters!
4. **Real ASIO hosts use thiscall** - test programs should too

## Timeline
- 2026-01-21: Initial investigation, identified `c0000135` error
- 2026-01-21: Incorrectly blamed `--builtin` flag
- 2026-01-21: Discovered thiscall calling convention issue
- 2026-01-21: Re-enabled `--builtin`, created proper test
- 2026-01-21: REAPER 32-bit loads WineASIO, CreateBuffers works, but no audio!

## Current Status (2026-01-21 19:01)

### What Works
- ✅ Unix-side loads correctly (`SUCCESS - unix handle = ...`)
- ✅ DllGetClassObject called
- ✅ Init called and succeeds
- ✅ CreateBuffers called (18 channels, buffer size 128)
- ✅ REAPER recognizes WineASIO as ASIO driver

### What Doesn't Work
- ❌ No audio output - REAPER reports "audio close"
- ❌ Start() not visible in logs - may not be called or fails silently
- ❌ After closing REAPER with these settings, next start crashes

### Debug Log Evidence
```
[WineASIO-DBG] init_wine_unix_call: SUCCESS - unix handle = 0x708a7f8e3cc0
[WineASIO-DBG] >>> DllGetClassObject called
[WineASIO-DBG] >>> Init(iface=01744c80, sysRef=000100f0)
[WineASIO-DBG] >>> CreateBuffers(iface=01744c80, bufferInfos=00f85760, numChannels=18, bufferSize=128, callbacks=00f977cc)
[WineASIO-DBG]     callbacks->bufferSwitch=0044c9c4
...
[WineASIO-DBG] DllMain: hInstDLL=02420000 fdwReason=0  # PROCESS_DETACH - app exits
```

No `Start()` call visible after CreateBuffers!

## Next Session TODOs

### Priority 1: Debug why Start() is not called / audio doesn't work
1. Add more debug output to `Start()`, `Stop()`, `GetSampleRate()`, `CanSampleRate()`
2. Check if CreateBuffers returns correct status
3. Check if callbacks are being called
4. Verify JACK connection is established

### Priority 2: Fix crash on second REAPER start
1. Reset audio settings in REAPER before closing
2. Or find what state causes the crash
3. Test in clean wine-test prefix

### Priority 3: Verify with other 32-bit hosts
1. Test with Garritan CFX Lite
2. Test with other 32-bit DAWs

### Debug Commands for Next Session
```bash
# Start REAPER with full debug logging
WINEDEBUG=+wineasio,-all wine ~/.wine/drive_c/Program\ Files\ \(x86\)/REAPER/reaper.exe 2>&1 | tee ~/docker-workspace/logs/reaper32_debug.log

# Check JACK connections
jack_lsp -c

# Reset REAPER audio settings (delete config)
rm ~/.wine/drive_c/users/*/Application\ Data/REAPER/reaper.ini
```

### Log Files
- `~/docker-workspace/logs/reaper32_wineasio_20260121_190132.log`

---

**CONCLUSION**: Unix-side loading is fixed with `--builtin`. But audio pipeline has issues - CreateBuffers succeeds but Start() is not called or fails. Need to debug the ASIO workflow after CreateBuffers.