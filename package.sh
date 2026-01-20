#!/bin/bash
#
# WineASIO 1.4.0 Release Packaging Script
# Creates release tarballs for distribution
#

set -e

VERSION="1.4.0"
PACKAGE_NAME="wineasio-${VERSION}"
BUILD_DIR="build_wine11"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  WineASIO ${VERSION} Release Packager${NC}"
echo -e "${GREEN}========================================${NC}"
echo

# Check if we're in the right directory
if [ ! -f "Makefile.wine11" ]; then
    echo -e "${RED}Error: Run this script from the WineASIO source directory${NC}"
    exit 1
fi

# Create dist directory
DIST_DIR="dist"
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

echo -e "${YELLOW}[1/5]${NC} Creating source tarball..."

# Files to include in source release
SOURCE_FILES=(
    # Core source files (Wine 11)
    "asio_pe.c"
    "asio_unix.c"
    "unixlib.h"

    # Legacy source files
    "asio.c"
    "main.c"
    "regsvr.c"
    "jackbridge.c"
    "jackbridge.h"

    # Build system
    "Makefile.wine11"
    "Makefile"
    "Makefile.mk"

    # Definition files
    "wineasio.def"
    "ntdll_wine.def"
    "ntdll_wine32.def"
    "wineasio.dll.spec"
    "wineasio32.dll.spec"
    "wineasio64.dll.spec"

    # Documentation
    "README.md"
    "RELEASE_NOTES.md"
    "WINE11_PORTING.md"
    "COPYING.LIB"
    "COPYING.GUI"

    # Scripts
    "wineasio-register"
    "wineasio-register.sh"
    "package.sh"

    # Assets
    "screenshot.png"
)

# Create source package directory
SOURCE_PKG="${DIST_DIR}/${PACKAGE_NAME}-source"
mkdir -p "${SOURCE_PKG}"
mkdir -p "${SOURCE_PKG}/gui"
mkdir -p "${SOURCE_PKG}/docker"

# Copy source files
for file in "${SOURCE_FILES[@]}"; do
    if [ -f "$file" ]; then
        cp "$file" "${SOURCE_PKG}/"
    fi
done

# Copy GUI directory
if [ -d "gui" ]; then
    cp gui/settings.py "${SOURCE_PKG}/gui/" 2>/dev/null || true
    cp gui/settings.ui "${SOURCE_PKG}/gui/" 2>/dev/null || true
    cp gui/ui_settings.py "${SOURCE_PKG}/gui/" 2>/dev/null || true
    cp gui/wineasio-settings "${SOURCE_PKG}/gui/" 2>/dev/null || true
    cp gui/wineasio-settings-launcher.c "${SOURCE_PKG}/gui/" 2>/dev/null || true
    cp gui/wineasio-settings.bat "${SOURCE_PKG}/gui/" 2>/dev/null || true
    cp gui/Makefile "${SOURCE_PKG}/gui/" 2>/dev/null || true
fi

# Copy docker directory if exists
if [ -d "docker" ]; then
    cp -r docker/* "${SOURCE_PKG}/docker/" 2>/dev/null || true
fi

# Create source tarball
cd "${DIST_DIR}"
tar -czvf "${PACKAGE_NAME}-source.tar.gz" "${PACKAGE_NAME}-source"
cd ..

echo -e "${GREEN}  ✓ Created ${DIST_DIR}/${PACKAGE_NAME}-source.tar.gz${NC}"

echo -e "${YELLOW}[2/5]${NC} Creating binary tarball..."

# Check if binaries exist
if [ ! -d "${BUILD_DIR}" ]; then
    echo -e "${RED}  Warning: Build directory not found. Run 'make -f Makefile.wine11 all' first.${NC}"
    echo -e "${YELLOW}  Skipping binary package...${NC}"
else
    BINARY_PKG="${DIST_DIR}/${PACKAGE_NAME}-binaries"
    mkdir -p "${BINARY_PKG}/64bit"
    mkdir -p "${BINARY_PKG}/32bit"
    mkdir -p "${BINARY_PKG}/gui"

    # Copy 64-bit binaries
    if [ -f "${BUILD_DIR}/wineasio64.dll" ]; then
        cp "${BUILD_DIR}/wineasio64.dll" "${BINARY_PKG}/64bit/"
    fi
    if [ -f "${BUILD_DIR}/wineasio64.so" ]; then
        cp "${BUILD_DIR}/wineasio64.so" "${BINARY_PKG}/64bit/"
    fi

    # Copy 32-bit binaries
    if [ -f "${BUILD_DIR}/wineasio.dll" ]; then
        cp "${BUILD_DIR}/wineasio.dll" "${BINARY_PKG}/32bit/"
    fi
    if [ -f "${BUILD_DIR}/wineasio.so" ]; then
        cp "${BUILD_DIR}/wineasio.so" "${BINARY_PKG}/32bit/"
    fi

    # Copy GUI launchers
    if [ -f "gui/wineasio-settings.exe" ]; then
        cp gui/wineasio-settings.exe "${BINARY_PKG}/gui/"
    fi
    if [ -f "gui/wineasio-settings64.exe" ]; then
        cp gui/wineasio-settings64.exe "${BINARY_PKG}/gui/"
    fi
    if [ -f "gui/wineasio-settings" ]; then
        cp gui/wineasio-settings "${BINARY_PKG}/gui/"
    fi
    if [ -f "gui/settings.py" ]; then
        cp gui/settings.py "${BINARY_PKG}/gui/"
    fi
    if [ -f "gui/ui_settings.py" ]; then
        cp gui/ui_settings.py "${BINARY_PKG}/gui/"
    fi

    # Copy documentation
    cp README.md "${BINARY_PKG}/"
    cp RELEASE_NOTES.md "${BINARY_PKG}/" 2>/dev/null || true
    cp COPYING.LIB "${BINARY_PKG}/"
    cp COPYING.GUI "${BINARY_PKG}/"

    # Create install script for binary package
    cat > "${BINARY_PKG}/install.sh" << 'INSTALL_EOF'
#!/bin/bash
#
# WineASIO Binary Installation Script
#

set -e

# Detect Wine prefix
if [ -z "$WINE_PREFIX" ]; then
    # Try common locations
    if [ -d "/opt/wine-stable/lib/wine" ]; then
        WINE_PREFIX="/opt/wine-stable"
    elif [ -d "/opt/wine-staging/lib/wine" ]; then
        WINE_PREFIX="/opt/wine-staging"
    elif [ -d "/usr/lib/wine" ]; then
        WINE_PREFIX="/usr"
    else
        echo "Could not detect Wine installation."
        echo "Please set WINE_PREFIX environment variable."
        exit 1
    fi
fi

echo "Installing WineASIO to ${WINE_PREFIX}..."

# 64-bit installation
if [ -f "64bit/wineasio64.dll" ]; then
    sudo cp 64bit/wineasio64.dll "${WINE_PREFIX}/lib/wine/x86_64-windows/"
    sudo cp 64bit/wineasio64.so "${WINE_PREFIX}/lib/wine/x86_64-unix/"
    echo "  ✓ 64-bit libraries installed"
fi

# 32-bit installation
if [ -f "32bit/wineasio.dll" ]; then
    sudo cp 32bit/wineasio.dll "${WINE_PREFIX}/lib/wine/i386-windows/"
    sudo cp 32bit/wineasio.so "${WINE_PREFIX}/lib/wine/i386-unix/"
    echo "  ✓ 32-bit libraries installed"
fi

# GUI installation
if [ -f "gui/wineasio-settings" ]; then
    sudo cp gui/wineasio-settings /usr/bin/
    sudo chmod +x /usr/bin/wineasio-settings
    sudo mkdir -p /usr/share/wineasio
    sudo cp gui/settings.py /usr/share/wineasio/
    sudo cp gui/ui_settings.py /usr/share/wineasio/ 2>/dev/null || true
    if [ -f "gui/wineasio-settings.exe" ]; then
        sudo cp gui/wineasio-settings*.exe /usr/share/wineasio/
    fi
    echo "  ✓ GUI installed"
fi

echo ""
echo "Installation complete!"
echo ""
echo "Now register the driver:"
echo "  wine regsvr32 wineasio64.dll"
echo "  wine ~/.wine/drive_c/windows/syswow64/regsvr32.exe wineasio.dll"
INSTALL_EOF
    chmod +x "${BINARY_PKG}/install.sh"

    # Create binary tarball
    cd "${DIST_DIR}"
    tar -czvf "${PACKAGE_NAME}-binaries.tar.gz" "${PACKAGE_NAME}-binaries"
    cd ..

    echo -e "${GREEN}  ✓ Created ${DIST_DIR}/${PACKAGE_NAME}-binaries.tar.gz${NC}"
fi

echo -e "${YELLOW}[3/5]${NC} Creating checksums..."

cd "${DIST_DIR}"
sha256sum *.tar.gz > SHA256SUMS.txt
cd ..

echo -e "${GREEN}  ✓ Created ${DIST_DIR}/SHA256SUMS.txt${NC}"

echo -e "${YELLOW}[4/5]${NC} Creating GitHub release template..."

cat > "${DIST_DIR}/GITHUB_RELEASE.md" << 'RELEASE_EOF'
# WineASIO 1.4.0 - Wine 11 Edition 🍷🎵

## What's New

This is a major release bringing **full Wine 11 compatibility** with a complete rewrite of the build system and internal architecture.

### ✨ Highlights

- **Wine 11 Support** - New PE/Unix split architecture for Wine 10.2+/11
- **Settings GUI** - Launch settings directly from your DAW's ASIO control panel
- **JACK MIDI** - New `WineASIO:midi_in` and `WineASIO:midi_out` ports

### 📦 Downloads

| File | Description |
|------|-------------|
| `wineasio-1.4.0-source.tar.gz` | Complete source code |
| `wineasio-1.4.0-binaries.tar.gz` | Pre-built binaries (DLLs + SOs) |

### 🔧 Quick Install

```bash
# From source
tar -xzf wineasio-1.4.0-source.tar.gz
cd wineasio-1.4.0-source
make -f Makefile.wine11 all
sudo make -f Makefile.wine11 install
make -f Makefile.wine11 register

# From binaries
tar -xzf wineasio-1.4.0-binaries.tar.gz
cd wineasio-1.4.0-binaries
./install.sh
```

### 💻 Requirements

- Wine 10.2+ or Wine 11
- JACK Audio Connection Kit
- PyQt5 or PyQt6 (for settings GUI)

### 📊 Tested DAWs

| DAW | Status |
|-----|--------|
| FL Studio | ✅ Working |
| Reaper | ✅ Working |
| Ableton Live | ✅ Working |
| Bitwig Studio | ✅ Working |

### 📄 Checksums

See `SHA256SUMS.txt` for file checksums.

---

**Full changelog:** See RELEASE_NOTES.md
RELEASE_EOF

echo -e "${GREEN}  ✓ Created ${DIST_DIR}/GITHUB_RELEASE.md${NC}"

echo -e "${YELLOW}[5/5]${NC} Cleanup..."

# Remove temporary directories (keep tarballs)
rm -rf "${DIST_DIR}/${PACKAGE_NAME}-source"
rm -rf "${DIST_DIR}/${PACKAGE_NAME}-binaries"

echo -e "${GREEN}  ✓ Cleanup complete${NC}"

echo
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Package creation complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo
echo "Files created in ${DIST_DIR}/:"
ls -la "${DIST_DIR}/"
echo
echo -e "${YELLOW}Next steps:${NC}"
echo "1. Create a new GitHub repository: https://github.com/new"
echo "2. Push the source code"
echo "3. Create a new release and upload:"
echo "   - ${DIST_DIR}/${PACKAGE_NAME}-source.tar.gz"
echo "   - ${DIST_DIR}/${PACKAGE_NAME}-binaries.tar.gz"
echo "   - ${DIST_DIR}/SHA256SUMS.txt"
echo "4. Use ${DIST_DIR}/GITHUB_RELEASE.md as release description"
echo
