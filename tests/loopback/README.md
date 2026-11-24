# Loopback test: round trip without audio hardware

`run.sh` measures how much latency WineASIO adds between a host's `bufferSwitch()` and JACK. No
audio interface or cable is needed: the test host sends an impulse on WineASIO's `out_1`, JACK
routes it back to `in_1`, and the host counts the cycles until the impulse returns. A native JACK
client goes through the same loop first, as the control case, so the difference between the two
is what WineASIO adds.

It also reports:

- `GetLatencies()`, the latencies a DAW uses to compensate recordings;
- swallowed cycles: jumps of `samplePosition` between two `bufferSwitchTimeInfo()` calls (`gaps`);
- impulses that never came back (`lost`) and signal that came back with no impulse in flight
  (`stray`, which means the ports are not wired as intended);
- `systemTime` against Wine's `timeGetTime()`.

## Requirements

- a running JACK server: jackd, or PipeWire's JACK (none is started by the test);
- WineASIO built and registered in the prefix (`wine regsvr32 wineasio64.dll`, and
  `wine regsvr32 wineasio.dll` for `ARCH=32`);
- `gcc` with the JACK headers, and MinGW: `x86_64-w64-mingw32-gcc`, plus `i686-w64-mingw32-gcc`
  for the 32-bit host.

| Distribution | Packages |
|---|---|
| Fedora (PipeWire's JACK) | `gcc mingw64-gcc mingw32-gcc pipewire-jack-audio-connection-kit-devel` |
| Ubuntu/Debian | `gcc gcc-mingw-w64 libjack-jackd2-dev` |

The driver itself needs no JACK headers; only the two native test clients do.

## Running

```sh
tests/loopback/run.sh                       # 64-bit host, 50 impulses per client
ARCH=32 tests/loopback/run.sh               # 32-bit host
WINEASIO_OUTPUT_BUFFERING=1 tests/loopback/run.sh
WINEPREFIX=~/.wine-test WINE=/opt/wine/bin/wine N=200 tests/loopback/run.sh
```

`WINEASIO_OUTPUT_BUFFERING` is set for every run (default 0) and overrides the value in the
prefix's registry, so the result does not depend on the settings panel.

**Buffer size.** WineASIO uses JACK's buffer size. With PipeWire, force the quantum for the run
and reset it afterwards:

```sh
pw-metadata -n settings 0 clock.force-quantum 256
tests/loopback/run.sh
pw-metadata -n settings 0 clock.force-quantum 0
```

With jackd, a separate dummy server leaves the working server alone:

```sh
jackd -n lbtest -d dummy -r 48000 -p 256 &
JACK_DEFAULT_SERVER=lbtest tests/loopback/run.sh
```

**Safety.** WineASIO connects to the system ports when it starts. Both clients send silence
until the script has removed every connection of their ports, made the loop and checked it.
Only then are the impulses released. If the check fails, nothing is sent.

**Loop.** `LOOP=direct` connects a client's `out_1` to its own `in_1`. PipeWire passes no data
over a link from a client to itself: every impulse is lost, the native client's as well. With
PipeWire's libjack the script therefore uses `LOOP=relay` by default: the loop goes through a
second client, `lb-relay`, which copies its input to its output. The control case goes through
the same relay, so the comparison stays valid. `LOOP=direct` or `LOOP=relay` forces a mode.

## Reading the result

```
== Result
   loop alone (control):  128 samples = 1.00 periods
   WineASIO round trip:   128 samples = 1.00 periods
   added by WineASIO:     0 samples = 0.00 periods = 0.00 ms
   GetLatencies(out) wired to the system ports: 128 samples
```

| Output buffering | added by WineASIO |
|---|---|
| 0 (default) | 0 periods |
| 1 | 1 period |
| 2 | 2 periods |

In the `RESULT` and `CYCLES` lines above the summary, `lost`, `stray` and `gaps` should be 0.
The full logs are in `$OUT` (default `${TMPDIR:-/tmp}/wineasio-loopback`).

**Load.** `LB_BURN_US=<microseconds>` makes the test host busy-wait in every `bufferSwitch()`,
`LB_BURN_EVERY=<n>` only in every n-th one. This shows how a host that overruns its period
behaves with each Output buffering setting. JACK counts the xruns, not the test: watch jackd's
output, or the `ERR` column of `pw-top` with PipeWire.

## Measured

Wine 11.0 based build, 30 impulses per run, 128 frames at 48 kHz, every run with
`lost=0`, `stray=0` and `gaps=0`:

| Server | Loop | Host | Output buffering 0 / 1 / 2 adds |
|---|---|---|---|
| jackd2 1.9.21, dummy driver | direct | 64-bit | 0 / 1 / 2 periods |
| jackd2 1.9.21, dummy driver | direct | 32-bit | 0 / – / 2 periods |
| jackd2 1.9.21, dummy driver | relay | 64-bit | 0 / – / – periods |
| PipeWire 1.0.5 (`pw-jack`, Ubuntu 24.04) | relay | 64-bit | 0 / 1 / 2 periods |
| PipeWire 1.0.5 (`pw-jack`, Ubuntu 24.04) | relay | 32-bit | 0 / – / – periods |

– = not measured. With PipeWire and `LOOP=direct`, all impulses were lost for both clients.

## Files

| File | |
|---|---|
| `run.sh` | builds the clients, wires the loop, runs both clients, prints the result |
| `asio_loopback.c` | minimal ASIO host (64- and 32-bit), loads WineASIO through COM |
| `jack_loopback.c` | native JACK client with the same measuring logic (control case) |
| `lb_common.h` | the measuring logic shared by both |
| `jack_wire.c` | lists clients, makes and checks the loop, runs `lb-relay` |
