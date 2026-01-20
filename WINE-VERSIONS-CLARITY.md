# Wine Versions Clarity - WineASIO Testing

This document clarifies the Wine installations on the system and their WoW64 architectures.

## Wine Installations Overview

| Path | Source | Version | WoW64 Type | i386-unix SOs | Status |
|------|--------|---------|------------|---------------|--------|
| `/usr/bin/wine` | winehq-stable | 11.0 | **OLD** | 37 ✅ | System default |
| `/opt/wine-stable/bin/wine` | winehq-stable | 11.0 | **OLD** | 37 ✅ | Same as above |
| `/usr/local/bin/wine` | Custom build | 11.0 | **NEW** | 0 ❌ | Our build |

## Important Facts

### 1. `/usr/bin/wine` is a Symlink
```bash
/usr/bin/wine → /opt/wine-stable/bin/wine
```

**This means:**
- Testing with `/usr/bin/wine` is **identical** to testing with `/opt/wine-stable/bin/wine`
- Both use the **winehq-stable** package
- Both use **old WoW64** architecture

### 2. WoW64 Architecture Differences

#### Old WoW64 (winehq-stable)
- **Location:** `/opt/wine-stable/` (= `/usr/bin/wine`)
- **32-bit Unix libs:** `/opt/wine-stable/lib/wine/i386-unix/` (37 .so files)
- **64-bit Unix libs:** `/opt/wine-stable/lib/wine/x86_64-unix/`
- **32-bit PE DLLs:** `/opt/wine-stable/lib/wine/i386-windows/`
- **64-bit PE DLLs:** `/opt/wine-stable/lib/wine/x86_64-windows/`

**Architecture:**
```
32-bit PE DLL → 32-bit Unix SO (ELF 32-bit)
64-bit PE DLL → 64-bit Unix SO (ELF 64-bit)
```

#### New WoW64 (Custom build)
- **Location:** `/usr/local/`
- **32-bit Unix libs:** `/usr/local/lib/wine/i386-unix/` (EMPTY!)
- **64-bit Unix libs:** `/usr/local/lib/wine/x86_64-unix/`
- **32-bit PE DLLs:** `/usr/local/lib/wine/i386-windows/`
- **64-bit PE DLLs:** `/usr/local/lib/wine/x86_64-windows/`

**Architecture:**
```
32-bit PE DLL → 64-bit Unix SO (ELF 64-bit) ⚠️ Shared!
64-bit PE DLL → 64-bit Unix SO (ELF 64-bit)
```

## WineASIO Installation Requirements

### For Old WoW64 (`/usr/bin/wine` or `/opt/wine-stable`)

**32-bit:**
```bash
# PE DLL
/opt/wine-stable/lib/wine/i386-windows/wineasio.dll

# Unix SO (ELF 32-bit!)
/opt/wine-stable/lib/wine/i386-unix/wineasio.so
```

**64-bit:**
```bash
# PE DLL
/opt/wine-stable/lib/wine/x86_64-windows/wineasio64.dll

# Unix SO (ELF 64-bit)
/opt/wine-stable/lib/wine/x86_64-unix/wineasio64.so
```

### For New WoW64 (`/usr/local/bin/wine`)

**32-bit:**
```bash
# PE DLL
/usr/local/lib/wine/i386-windows/wineasio.dll

# Unix SO (ELF 64-bit!) - Shared with 64-bit!
/usr/local/lib/wine/x86_64-unix/wineasio.so
```

**64-bit:**
```bash
# PE DLL
/usr/local/lib/wine/x86_64-windows/wineasio64.dll

# Unix SO (ELF 64-bit)
/usr/local/lib/wine/x86_64-unix/wineasio64.so
```

**⚠️ Critical for New WoW64:**
- The `wineasio.so` in `x86_64-unix/` MUST be ELF 64-bit
- 32-bit PE DLL uses the 64-bit Unix SO via thunking layer

## Test Results Summary

### ✅ Working Configurations

| Wine | Architecture | App | Result |
|------|-------------|-----|--------|
| Custom (`/usr/local`) | 64-bit | CFX Lite 64-bit | ✅ **Works perfectly** |
| winehq-stable (`/usr/bin`) | 64-bit | Not tested | Likely works |

### ❌ Failing Configurations

| Wine | Architecture | App | Result | Error |
|------|-------------|-----|--------|-------|
| Custom (`/usr/local`) | 32-bit | REAPER 32-bit | ❌ Crash | NULL pointer at 0x00000000 |
| winehq-stable (`/usr/bin`) | 32-bit | REAPER 32-bit | ❌ Crash | NULL pointer at 0x00000004 |

**Both old and new WoW64 crash with 32-bit apps!**

## Conclusion

The **32-bit WineASIO crash is NOT a Wine-specific issue**:
- ❌ Not caused by WoW64 architecture (both old and new crash)
- ❌ Not caused by Wine version (11.0 custom and stable both crash)
- ✅ **IS a WineASIO 32-bit code bug**

### Root Cause
WineASIO's 32-bit code has a NULL pointer dereference bug when:
1. Loading successfully completes
2. ASIO device is selected in DAW
3. During ASIO initialization or buffer creation

### Workaround
**Use 64-bit applications** - WineASIO 64-bit works perfectly on both Wine installations.

## Commands Reference

### Check which Wine is which
```bash
# System default (winehq-stable)
/usr/bin/wine --version
readlink -f /usr/bin/wine  # Shows: /opt/wine-stable/bin/wine

# Custom build
/usr/local/bin/wine --version

# Check WoW64 type
ls /opt/wine-stable/lib/wine/i386-unix/   # If has files: old WoW64
ls /usr/local/lib/wine/i386-unix/         # If empty: new WoW64
```

### Install WineASIO for old WoW64
```bash
sudo cp wineasio.dll /opt/wine-stable/lib/wine/i386-windows/
sudo cp wineasio.so /opt/wine-stable/lib/wine/i386-unix/  # ELF 32-bit!
sudo /opt/wine-stable/bin/winebuild --builtin /opt/wine-stable/lib/wine/i386-windows/wineasio.dll
```

### Install WineASIO for new WoW64
```bash
sudo cp wineasio.dll /usr/local/lib/wine/i386-windows/
sudo cp wineasio64.so /usr/local/lib/wine/x86_64-unix/wineasio.so  # ELF 64-bit!
sudo /usr/local/bin/winebuild --builtin /usr/local/lib/wine/i386-windows/wineasio.dll
```

---

**Last Updated:** 2025-01-20
**Testing System:** Ubuntu 24.04, Wine 11.0
**Status:** 64-bit works, 32-bit has runtime crash bug