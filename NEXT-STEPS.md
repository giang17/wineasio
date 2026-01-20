# WineASIO 32-bit Debug - Next Steps

## 🎯 Current Status

**Phase**: vtable Runtime Debugging  
**Branch**: `wine11-32bit-test`  
**Build**: Complete with instrumentation  
**Status**: ✅ Ready to test

---

## 📋 What We Just Did

### 1. Added vtable Pointer Dump
In `asio_pe.c`, after creating a WineASIO instance, we now print all 24 function pointers:
```
=== VTABLE DUMP ===
vtable[0x00] QueryInterface   = 0x7b4xxxxx
vtable[0x04] AddRef            = 0x7b4xxxxx
...
vtable[0x5C] OutputReady       = 0x7b4xxxxx
=== END VTABLE DUMP ===
```

**Purpose**: See if any function pointer is NULL (0x00000000)

### 2. Added Function Call Logging
Every ASIO function now logs when it's called:
```
>>> CALLED: Init(iface=0x..., sysRef=0x...)
<<< RETURNING from Init: SUCCESS (1)
>>> CALLED: GetChannels(...)
```

**Purpose**: See exactly which function is called before the crash

### 3. Created Test Scripts
- `install-32bit-debug.sh` - Install the debug build
- `test-reaper-32bit-vtable.sh` - Run REAPER with logging

### 4. Updated Documentation
- `VTABLE-DEBUG-README.md` - Complete debugging guide
- `DEBUG-32BIT-CRASH.md` - Updated with instrumentation details

---

## ⚡ What You Need to Do Now

### Step 1: Install the Debug Build
```bash
cd ~/docker-workspace/wineasio-fork
./install-32bit-debug.sh
```

**You will be asked for sudo password** to copy files to `/opt/wine-stable/`

### Step 2: Run REAPER Test
```bash
./test-reaper-32bit-vtable.sh
```

This will:
- Check JACK is running (start it if needed)
- Launch REAPER 32-bit with `WINEDEBUG=warn+wineasio`
- Save output to `/tmp/reaper_vtable_debug.log`
- Automatically analyze the results

### Step 3: Wait for Crash
REAPER will crash (expected). The script will then show:
- ✓ Which functions were called
- ✓ Which function was called last
- ✓ The crash signature

---

## 🔍 What We're Looking For

### A. vtable Dump Analysis
Look for any NULL pointers:
```
vtable[0x3C] GetClockSources   = 0x00000000  ← NULL! This is the bug!
```

If all pointers are valid (non-zero), continue to next check.

### B. Function Call Sequence
```
>>> CALLED: Init(...)
<<< RETURNING from Init: SUCCESS (1)
>>> CALLED: GetChannels(...)  ← Last function before crash
[CRASH]
```

The last `>>> CALLED:` line tells us which function was entered.

### C. Crash Address
```
Unhandled exception: page fault on execute access to 0x00000060
EIP:00000060
```

Compare crash address with vtable offsets:
- **0x00**: NULL pointer (function is NULL)
- **0x3C** (60): GetClockSources offset
- **0x44** (68): GetSamplePosition offset

### D. Conclusion
Combine all three pieces of info:
1. **Which pointer is NULL?** (from vtable dump)
2. **Which function was called last?** (from >>> CALLED:)
3. **What offset crashed?** (from EIP address)

---

## 📊 Expected Outcomes

### Scenario A: Function Pointer is NULL
```
vtable[0x10] GetDriverName = 0x00000000
>>> CALLED: Init
<<< RETURNING from Init
[CRASH at 0x00]
```
**Root Cause**: Function not implemented or vtable entry missing  
**Fix**: Add missing function to vtable

### Scenario B: Host Calls Wrong Function
```
vtable[0x3C] GetClockSources = 0x7b412340  (valid)
>>> CALLED: Init
<<< RETURNING from Init
[CRASH at 0x3C but no >>> CALLED: GetClockSources]
```
**Root Cause**: Host thinks vtable has different layout  
**Fix**: Check 32-bit calling convention, stdcall decorations, vtable alignment

### Scenario C: Crash Inside Valid Function
```
vtable[0x24] GetChannels = 0x7b412340  (valid)
>>> CALLED: GetChannels
[CRASH at 0x7b412350]
```
**Root Cause**: Bug inside the function implementation  
**Fix**: Debug that specific function

---

## 📁 Important Files

### Test Scripts
- `install-32bit-debug.sh` - Install debug build (needs sudo)
- `test-reaper-32bit-vtable.sh` - Run REAPER with logging

### Documentation
- `VTABLE-DEBUG-README.md` - Complete debugging guide
- `DEBUG-32BIT-CRASH.md` - Debug status and progress
- `NEXT-STEPS.md` - This file

### Code Changes
- `asio_pe.c` - vtable dump (lines 847-875) + function logging (lines 429+)

### Logs
- `/tmp/reaper_vtable_debug.log` - Test output (will be created)

---

## 🔄 After Testing - What Comes Next?

### If vtable Entry is NULL
→ Fix the vtable definition in `asio_pe.c`

### If All Pointers Valid but Crash at Offset
→ Investigate calling convention mismatch (32-bit stdcall)

### If Still Unclear
→ Test legacy WineASIO (pre-Wine-11) to compare

### If All Else Fails
→ Create minimal ASIO host for isolated testing

---

## 🆘 Troubleshooting

### JACK not running
```bash
jackdbus auto
# or
jackd -d alsa
```

### REAPER not found
Check path: `~/wine-test-winestable/drive_c/Program Files (x86)/REAPER/reaper.exe`

### No vtable dump in log
Check if instance was created: `grep "WineASIOCreateInstance" /tmp/reaper_vtable_debug.log`

### Need more detail
```bash
export WINEDEBUG=+all,warn+wineasio
/opt/wine-stable/bin/wine reaper.exe 2>&1 | tee /tmp/verbose.log
```

---

## 📞 Questions to Answer

After running the test, we should be able to answer:

1. ✅ **Which function pointer is NULL (if any)?**
2. ✅ **What function is called immediately after Init?**
3. ✅ **What is the exact crash address (EIP)?**
4. ✅ **How does the crash address relate to vtable offsets?**

With these answers, we can pinpoint the exact bug and create a targeted fix!

---

## 🚀 Quick Command Reference

```bash
# Install debug build
cd ~/docker-workspace/wineasio-fork
./install-32bit-debug.sh

# Run test
./test-reaper-32bit-vtable.sh

# View log manually
less /tmp/reaper_vtable_debug.log

# Search for specific info
grep "VTABLE DUMP" /tmp/reaper_vtable_debug.log -A 30
grep ">>> CALLED:" /tmp/reaper_vtable_debug.log
grep "page fault" /tmp/reaper_vtable_debug.log -A 5

# Manual test (if script fails)
export WINEPREFIX=~/wine-test-winestable
export WINEDEBUG=warn+wineasio
cd "$WINEPREFIX/drive_c/Program Files (x86)/REAPER"
/opt/wine-stable/bin/wine reaper.exe 2>&1 | tee /tmp/manual_test.log
```

---

**Ready to test? Run `./install-32bit-debug.sh` and then `./test-reaper-32bit-vtable.sh`!**

The crash will happen (that's expected), but now we'll see **exactly** what's causing it. 🎯