#!/bin/bash
# run.sh - measure the round trip of WineASIO through a JACK-internal loop
# (out_1 -> in_1) and compare it with a native JACK client.
#
# Measures what lies between a host's bufferSwitch() and the return of its
# signal, without cables or audio hardware: an impulse goes out on out_1, JACK
# routes it back to in_1, and the host counts the cycles until it arrives.
#
#   control case (native JACK client, same logic)    -> the loop alone, 1 period
#   WineASIO, Output buffering 0 (default)           -> adds 0 periods
#   WineASIO, Output buffering 1 / 2                 -> adds 1 / 2 periods
#
# (With Output buffering 2 WineASIO writes before it reads, so the control case
# does the same and its loop costs 0 periods; see below.)
#
# It also reports what GetLatencies() returns (what a DAW compensates) and
# whether bufferSwitch() cycles get swallowed (samplePosition jumps).
#
# SAFETY: WineASIO connects to the system ports when it starts. Both clients
# send silence until this script has removed ALL connections of their ports,
# connected out_1 -> in_1 and checked the result; only then does the arm file
# release the impulses. If the check fails, nothing is released and the client
# exits by itself after 30 s.
#
# Usage:  tests/loopback/run.sh              (50 measurements per client)
#         N=200 tests/loopback/run.sh
#         ARCH=32 tests/loopback/run.sh      (32-bit host, needs the 32-bit WineASIO)
# Environment: WINEPREFIX (default ~/.wine, WineASIO must be registered there),
#              WINE (default: wine), OUT (logs and binaries, default
#              ${TMPDIR:-/tmp}/wineasio-loopback), ARCH (64 or 32, default 64),
#              WINEASIO_OUTPUT_BUFFERING (0, 1 or 2, default 0; overrides the
#              registry of the prefix for this run),
#              LOOP (direct, relay or auto, default auto: relay with PipeWire's
#              libjack, direct otherwise; see below).
# Needs: a running JACK server (jackd or PipeWire's JACK; none is started),
#        gcc and the JACK headers, x86_64-w64-mingw32-gcc (i686-w64-mingw32-gcc
#        for ARCH=32).
# Other buffer sizes: with jackd, start a separate dummy server and select it
# with JACK_DEFAULT_SERVER (Wine passes it on to libjack):
#   jackd -n lbtest -d dummy -r 48000 -p 256 &
#   JACK_DEFAULT_SERVER=lbtest tests/loopback/run.sh
# With PipeWire, force the quantum for the run and reset it afterwards:
#   pw-metadata -n settings 0 clock.force-quantum 256
#   pw-metadata -n settings 0 clock.force-quantum 0
#
# LOOP=direct connects the client's out_1 to its own in_1. PipeWire passes no
# data over such a link (every impulse is lost, the native client's as well),
# so LOOP=relay routes the loop through a second client, lb-relay, which copies
# its input to its output. Both clients go through the same loop, so "added by
# WineASIO" compares like with like in either mode.

set -u
export LC_ALL=C   # sort and comm have to agree on the order
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/wineasio-loopback}
WINE=${WINE:-wine}
PREFIX=${WINEPREFIX:-$HOME/.wine}
N=${N:-50}
ARCH=${ARCH:-64}
case $ARCH in
    64) CC=x86_64-w64-mingw32-gcc ;;
    32) CC=i686-w64-mingw32-gcc ;;
    *)  echo "ERROR: ARCH=$ARCH (allowed: 64, 32)"; exit 1 ;;
esac

# The run sets Output buffering through the environment, which overrides the
# registry of the prefix: 0 unless WINEASIO_OUTPUT_BUFFERING says otherwise.
export WINEASIO_OUTPUT_BUFFERING=${WINEASIO_OUTPUT_BUFFERING:-0}
case $WINEASIO_OUTPUT_BUFFERING in
    0|1|2) ;;
    *) echo "ERROR: WINEASIO_OUTPUT_BUFFERING=$WINEASIO_OUTPUT_BUFFERING (allowed: 0, 1, 2)"; exit 1 ;;
esac

# The self-loop costs 1 period when a client fetches its input buffer before
# writing its output, and 0 when it fetches it afterwards: the input shows the
# client's own output as it is at jack_port_get_buffer(). WineASIO writes first
# with Output buffering 2, so the control case then uses that order too.
[ "$WINEASIO_OUTPUT_BUFFERING" = 2 ] && export LB_WRITE_FIRST=1

mkdir -p "$OUT"
echo "== Build"
gcc -O2 -Wall -o "$OUT/jack_wire" "$HERE/jack_wire.c" -ljack || exit 1
gcc -O2 -Wall -o "$OUT/jack_loopback" "$HERE/jack_loopback.c" -ljack -lm || exit 1
$CC -O2 -Wall -o "$OUT/asio_loopback$ARCH.exe" "$HERE/asio_loopback.c" -lole32 -luuid -lwinmm || exit 1

"$OUT/jack_wire" clients >/dev/null || { echo "ERROR: no JACK server reachable (none is started)"; exit 1; }
echo "== JACK server: ${JACK_DEFAULT_SERVER:-default}, libjack: $(ldd "$OUT/jack_loopback" | awk '/libjack/ {print $3; exit}')"
echo "== Wine: $("$WINE" --version 2>/dev/null), prefix $PREFIX, host: $ARCH-bit, Output buffering: $WINEASIO_OUTPUT_BUFFERING"

clients() { "$OUT/jack_wire" clients | sort -u; }

LOOP=${LOOP:-auto}
if [ "$LOOP" = auto ]; then
    case $(ldd "$OUT/jack_loopback" | awk '/libjack/ {print $3; exit}') in
        *pipewire*) LOOP=relay ;;
        *) LOOP=direct ;;
    esac
fi
case $LOOP in
    direct|relay) echo "== Loop: $LOOP" ;;
    *) echo "ERROR: LOOP=$LOOP (allowed: direct, relay, auto)"; exit 1 ;;
esac

# run_one <label> <arm path for the client> <command...>
run_one() {
    local label=$1 armarg=$2; shift 2
    local log=$OUT/$label.log arm=$OUT/$label.arm before new pid rpid
    rm -f "$arm" "$log"
    before=$(clients)
    "$@" "$armarg" "$N" >"$log" 2>&1 &
    pid=$!
    for _ in $(seq 1 400); do
        grep -q '^READY' "$log" 2>/dev/null && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
    if ! grep -q '^READY' "$log"; then
        echo "ERROR: $label did not report READY"; tail -20 "$log"; wait "$pid"; return 1
    fi
    new=$(comm -13 <(echo "$before") <(clients))
    if [ "$(printf '%s\n' "$new" | grep -c .)" -ne 1 ]; then
        echo "ERROR: new JACK client not unique: '$new' -- not armed"; wait "$pid"; return 1
    fi
    echo "   JACK client: $new ($("$OUT/jack_wire" count "$new") connections before the rewiring)"
    if [ "$LOOP" = direct ]; then
        if ! "$OUT/jack_wire" loop "$new" | sed 's/^/   /'; then
            echo "   not armed, $label exits by itself"; wait "$pid"; return 1
        fi
    else
        "$OUT/jack_wire" relay "$new" >"$OUT/$label.relay" 2>&1 &
        rpid=$!
        for _ in $(seq 1 100); do
            grep -q '^wired:' "$OUT/$label.relay" 2>/dev/null && break
            kill -0 "$rpid" 2>/dev/null || break
            sleep 0.1
        done
        sed 's/^/   /' "$OUT/$label.relay"
        if ! grep -q '^wired:' "$OUT/$label.relay"; then
            echo "   not armed, $label exits by itself"; kill "$rpid" 2>/dev/null; wait "$pid"; return 1
        fi
    fi
    touch "$arm"
    wait "$pid"
    rm -f "$arm"
    [ "$LOOP" = relay ] && wait "$rpid"
    grep -E '^(INFO|GETLAT|RESULT|CYCLES|SYSTIME|ERROR|NOT_ARMED)' "$log" | sed 's/^/   /'
}

echo "== Control case: native JACK client"
run_one control "$OUT/control.arm" "$OUT/jack_loopback" || exit 1

echo "== WineASIO"
winarm=$(WINEPREFIX="$PREFIX" WINEDEBUG=-all "$WINE" winepath -w "$OUT/wineasio.arm" 2>/dev/null | tr -d '\r')
[ -n "$winarm" ] || winarm="Z:$(printf '%s' "$OUT/wineasio.arm" | sed 's#/#\\#g')"
run_one wineasio "$winarm" env WINEPREFIX="$PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="winemenubuilder.exe=d" "$WINE" "$OUT/asio_loopback$ARCH.exe" || exit 1

echo "== Result"
c=$(grep -oP '^RESULT control .*lat_med=\K[0-9]+' "$OUT/control.log")
w=$(grep -oP '^RESULT wineasio .*lat_med=\K[0-9]+' "$OUT/wineasio.log")
b=$(grep -oP '^RESULT wineasio bufsize=\K[0-9]+' "$OUT/wineasio.log")
r=$(grep -oP '^RESULT wineasio .*rate=\K[0-9]+' "$OUT/wineasio.log")
lo=$(grep -oP '^GETLAT hardware_wired .*out=\K-?[0-9]+' "$OUT/wineasio.log")
if [ -n "$c" ] && [ -n "$w" ] && [ -n "$b" ]; then
    awk -v c="$c" -v w="$w" -v b="$b" -v r="$r" -v lo="$lo" 'BEGIN {
        printf "   loop alone (control):  %d samples = %.2f periods\n", c, c / b
        printf "   WineASIO round trip:   %d samples = %.2f periods\n", w, w / b
        printf "   added by WineASIO:     %d samples = %.2f periods = %.2f ms\n", w - c, (w - c) / b, 1000 * (w - c) / r
        printf "   GetLatencies(out) wired to the system ports: %d samples\n", lo
    }'
else
    echo "   incomplete -- logs: $OUT/control.log, $OUT/wineasio.log"
fi
echo "Logs: $OUT"
