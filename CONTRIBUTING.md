# Contributing to WineASIO

Thank you for your interest in contributing to WineASIO! 🎵🍷

## Ways to Contribute

### Bug Reports

1. Check existing issues first to avoid duplicates
2. Include your system information:
   - Wine version (`wine --version`)
   - Linux distribution and version
   - JACK version (`jackd --version`)
   - DAW name and version
3. Describe steps to reproduce the issue
4. Include relevant logs (see Debugging section below)

### Feature Requests

Open an issue describing:
- What you'd like to see
- Why it would be useful
- Any implementation ideas you have

### Code Contributions

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Make your changes
4. Test with multiple DAWs if possible
5. Submit a pull request

## Development Setup

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install gcc-mingw-w64 wine-stable wine-stable-dev libjack-jackd2-dev python3-pyqt5

# Fedora
sudo dnf install mingw64-gcc mingw32-gcc wine-devel jack-audio-connection-kit-devel python3-qt5

# Arch Linux
sudo pacman -S mingw-w64-gcc wine wine-staging jack2 python-pyqt5
```

### Build

```bash
# Build both architectures
make -f Makefile.wine11 all

# Install locally
sudo make -f Makefile.wine11 install

# Register
make -f Makefile.wine11 register
```

### Testing

Please test your changes with:

1. **Multiple Wine versions** - Wine 10.2+, Wine 11
2. **Both architectures** - 32-bit and 64-bit DAWs
3. **Multiple DAWs** - FL Studio, Reaper, Ableton, etc.
4. **Settings GUI** - Verify control panel opens from DAW
5. **JACK MIDI** - Check MIDI ports appear in `jack_lsp`

## Debugging

Enable Wine debug output:

```bash
WINEDEBUG=+loaddll,+module wine your_daw.exe
```

Check JACK connections:

```bash
jack_lsp -c | grep -i wine
```

Verify WineASIO is loaded:

```bash
WINEDEBUG=+loaddll wine your_daw.exe 2>&1 | grep -i asio
```

## Code Style

- Use 4 spaces for indentation (no tabs)
- Follow existing code formatting
- Comment complex logic
- Keep functions focused and small

## Architecture Overview

WineASIO uses Wine 11's split architecture:

| Component | Language | Purpose |
|-----------|----------|---------|
| `asio_pe.c` | C (mingw) | Windows ASIO/COM interface |
| `asio_unix.c` | C (gcc) | Linux JACK interface |
| `unixlib.h` | C header | Shared structures |
| `gui/settings.py` | Python | Settings GUI |

Communication between PE and Unix uses `__wine_unix_call`.

## Pull Request Guidelines

1. **One feature per PR** - Keep changes focused
2. **Update documentation** - README, WINE11_PORTING.md if needed
3. **Test thoroughly** - List what you tested in PR description
4. **Descriptive commits** - Explain what and why

## License

- Library code: LGPL v2.1 (COPYING.LIB)
- GUI code: GPL v2+ (COPYING.GUI)

By contributing, you agree your code will be licensed under these terms.

## Questions?

Open an issue or discussion if you need help!