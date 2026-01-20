#!/bin/bash
# Test REAPER 32-bit with WineASIO vtable debugging

set -e

echo "=========================================="
echo "WineASIO 32-bit vtable Debug Test"
echo "=========================================="
echo ""

# Check if JACK is running
if ! pgrep -x "jackd" > /dev/null && ! pgrep -x "jackdbus" > /dev/null; then
    echo "⚠️  WARNING: JACK is not running!"
    echo "Starting JACK with jackdbus..."
    jackdbus auto &
    sleep 2
fi

echo "✓ JACK is running"
echo ""

# Setup environment
export WINEPREFIX="$HOME/wine-test-winestable"
export WINEDEBUG="warn+wineasio"
export REAPER_DIR="$WINEPREFIX/drive_c/Program Files (x86)/REAPER"
export LOG_FILE="/tmp/reaper_vtable_debug.log"

echo "Environment:"
echo "  WINEPREFIX: $WINEPREFIX"
echo "  WINEDEBUG: $WINEDEBUG"
echo "  REAPER: $REAPER_DIR"
echo "  LOG: $LOG_FILE"
echo ""

# Check if REAPER exists
if [ ! -f "$REAPER_DIR/reaper.exe" ]; then
    echo "❌ ERROR: REAPER not found at $REAPER_DIR/reaper.exe"
    exit 1
fi

echo "✓ REAPER found"
echo ""

# Run REAPER with logging
echo "=========================================="
echo "Starting REAPER with vtable debugging..."
echo "=========================================="
echo ""
echo "Watch for:"
echo "  1. === VTABLE DUMP === (function pointers)"
echo "  2. >>> CALLED: Init (initialization)"
echo "  3. >>> CALLED: <next function> (what comes after Init)"
echo "  4. Crash signature (page fault at address)"
echo ""
echo "Press Ctrl+C to stop when crash occurs"
echo ""

cd "$REAPER_DIR"
/opt/wine-stable/bin/wine reaper.exe 2>&1 | tee "$LOG_FILE"

echo ""
echo "=========================================="
echo "Test Complete - Analyzing Results"
echo "=========================================="
echo ""

# Analyze log
echo "Checking for vtable dump..."
if grep -q "=== VTABLE DUMP ===" "$LOG_FILE"; then
    echo "✓ vtable dump found:"
    grep "vtable\[" "$LOG_FILE" | head -n 5
    echo "  (see full dump in log)"
else
    echo "❌ No vtable dump found (check if instance was created)"
fi

echo ""
echo "Checking which functions were called..."
CALLED_FUNCS=$(grep ">>> CALLED:" "$LOG_FILE" | wc -l)
if [ $CALLED_FUNCS -gt 0 ]; then
    echo "✓ Found $CALLED_FUNCS function call(s):"
    grep ">>> CALLED:" "$LOG_FILE" | sed 's/^/  /'
else
    echo "❌ No function calls detected"
fi

echo ""
echo "Checking for crash..."
if grep -q "page fault" "$LOG_FILE"; then
    echo "✓ Crash detected:"
    grep -A 5 "page fault" "$LOG_FILE" | sed 's/^/  /'
else
    echo "✓ No crash detected (unexpected!)"
fi

echo ""
echo "Full log saved to: $LOG_FILE"
echo ""
echo "Next steps:"
echo "  1. Check which function was called last before crash"
echo "  2. Compare crash address (EIP) with vtable offsets"
echo "  3. If crash at 0x00 or NULL: vtable entry is NULL/invalid"
echo "  4. Check 32-bit calling convention (stdcall decorations)"
