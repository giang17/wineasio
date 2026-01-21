#!/bin/bash
# Test script for REAPER 32-bit with WineASIO debug logging
# Session: 2026-01-21 (continued)

# Timestamp for log files
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOGDIR="$HOME/docker-workspace/logs"
LOGFILE="$LOGDIR/reaper32_debug_${TIMESTAMP}.log"

# Create log directory if it doesn't exist
mkdir -p "$LOGDIR"

# REAPER 32-bit executable path
REAPER32="$HOME/.wine/drive_c/Program Files (x86)/REAPER/reaper.exe"

# Check if REAPER is installed
if [ ! -f "$REAPER32" ]; then
    echo "ERROR: REAPER 32-bit not found at: $REAPER32"
    exit 1
fi

# Check if JACK is running
if ! pgrep -x jackd > /dev/null && ! pgrep -x pipewire-jack > /dev/null; then
    echo "WARNING: JACK/PipeWire-JACK doesn't seem to be running"
    echo "         Audio might not work properly"
fi

echo "=========================================="
echo "WineASIO 32-bit Debug Test - REAPER"
echo "=========================================="
echo "Date: $(date)"
echo "Log file: $LOGFILE"
echo ""
echo "Starting REAPER 32-bit with WineASIO debug output..."
echo "Watch for:"
echo "  - CreateBuffers success/failure"
echo "  - GetSampleRate calls"
echo "  - CanSampleRate calls"
echo "  - Start() call (THIS IS THE KEY!)"
echo ""
echo "Press Ctrl+C to stop logging"
echo "=========================================="
echo ""

# Run REAPER with WineASIO debug output
# -all disables most Wine warnings, +wineasio enables WineASIO traces
WINEDEBUG=+wineasio,-all wine "$REAPER32" 2>&1 | tee "$LOGFILE"

echo ""
echo "=========================================="
echo "REAPER closed. Log saved to:"
echo "$LOGFILE"
echo ""
echo "To analyze the log, look for:"
echo "  1. '[WineASIO-DBG] >>> CreateBuffers' - should show success"
echo "  2. '[WineASIO-DBG] >>> GetSampleRate' - check if called"
echo "  3. '[WineASIO-DBG] >>> CanSampleRate' - check if called"
echo "  4. '[WineASIO-DBG] >>> Start' - THIS IS CRITICAL!"
echo ""
echo "Quick analysis:"
grep -c "CreateBuffers" "$LOGFILE" && echo "CreateBuffers calls found" || echo "No CreateBuffers calls"
grep -c "GetSampleRate" "$LOGFILE" && echo "GetSampleRate calls found" || echo "No GetSampleRate calls"
grep -c "CanSampleRate" "$LOGFILE" && echo "CanSampleRate calls found" || echo "No CanSampleRate calls"
grep -c ">>> Start" "$LOGFILE" && echo "Start() calls found - GOOD!" || echo "Start() NOT called - THIS IS THE PROBLEM!"
echo "=========================================="
