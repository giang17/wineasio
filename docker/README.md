# WineASIO Docker Build

Build WineASIO for Wine 11 in a reproducible Docker environment.

## Requirements

- Docker installed and running

## Quick Build

```bash
./build.sh
```

This will:
1. Build a Docker image with Wine 11, MinGW, and JACK
2. Compile WineASIO for 32-bit and 64-bit
3. Copy the built files to the current directory

## Output Files

| File | Description |
|------|-------------|
| `wineasio64.dll` | 64-bit PE DLL |
| `wineasio64.so` | 64-bit Unix library |
| `wineasio.dll` | 32-bit PE DLL |
| `wineasio.so` | 32-bit Unix library |

## Installation

After building, copy the files to your Wine installation:

```bash
# 64-bit
sudo cp wineasio64.dll /opt/wine-stable/lib/wine/x86_64-windows/
sudo cp wineasio64.so /opt/wine-stable/lib/wine/x86_64-unix/

# 32-bit
sudo cp wineasio.dll /opt/wine-stable/lib/wine/i386-windows/
sudo cp wineasio.so /opt/wine-stable/lib/wine/i386-unix/
```

Then register the driver:

```bash
# 64-bit
wine regsvr32 wineasio64.dll

# 32-bit (WoW64)
wine ~/.wine/drive_c/windows/syswow64/regsvr32.exe wineasio.dll
```

## Manual Docker Commands

```bash
# Build image
docker build -t wineasio-wine11 .

# Run interactive shell for debugging
docker run -it --rm wineasio-wine11 bash

# Check Wine version
docker run --rm wineasio-wine11 wine --version
```

## Dockerfile Details

- **Base:** Ubuntu 24.04
- **Wine:** WineHQ Stable (Wine 11)
- **Compilers:** gcc, mingw-w64 (x86_64 and i686)
- **Audio:** JACK development libraries

## Troubleshooting

### Docker build fails

Make sure Docker is running:
```bash
sudo systemctl start docker
```

### Permission denied

Add yourself to the docker group:
```bash
sudo usermod -aG docker $USER
# Log out and log back in
```

### i386 packages not found

The Dockerfile enables i386 architecture. If you're on an older Ubuntu/Debian, you may need to run:
```bash
sudo dpkg --add-architecture i386
sudo apt update
```
