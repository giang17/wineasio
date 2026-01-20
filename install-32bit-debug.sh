#!/bin/bash
# Install 32-bit WineASIO debug build to wine-stable

set -e

echo "Installing 32-bit WineASIO debug build..."

# Copy PE DLL
sudo cp build_wine11/wineasio.dll /opt/wine-stable/lib/wine/i386-windows/
echo "✓ Copied PE DLL to /opt/wine-stable/lib/wine/i386-windows/"

# Copy Unix SO
sudo cp build_wine11/wineasio.so /opt/wine-stable/lib/wine/i386-unix/
echo "✓ Copied Unix SO to /opt/wine-stable/lib/wine/i386-unix/"

# Mark as builtin (already done in build, but ensure it's set)
sudo winebuild --builtin /opt/wine-stable/lib/wine/i386-windows/wineasio.dll 2>/dev/null || true
echo "✓ Marked as Wine builtin"

# Register with 32-bit regsvr32
echo ""
echo "Registering with 32-bit regsvr32..."
export WINEPREFIX="$HOME/wine-test-winestable"
/opt/wine-stable/bin/wine "$WINEPREFIX/drive_c/windows/syswow64/regsvr32.exe" /u wineasio.dll 2>/dev/null || true
/opt/wine-stable/bin/wine "$WINEPREFIX/drive_c/windows/syswow64/regsvr32.exe" wineasio.dll

echo ""
echo "Installation complete!"
echo ""
echo "To test with REAPER 32-bit, run:"
echo "  cd ~/wine-test-winestable/drive_c/Program\\ Files\\ \\(x86\\)/REAPER/ && WINEDEBUG=warn+wineasio /opt/wine-stable/bin/wine reaper.exe 2>&1 | tee /tmp/reaper_vtable_debug.log"
echo ""
echo "Look for:"
echo "  - VTABLE DUMP lines (function pointers)"
echo "  - >>> CALLED lines (which functions are actually called)"
echo "  - Crash signature (which function was being called when crash occurred)"
