#!/usr/bin/make -f
# Makefile for WineASIO Settings GUI #
# ---------------------------------- #
# Created by falkTX
#

PREFIX = /usr

PYUIC ?= pyuic5
PYRCC ?= pyrcc5

# MinGW compilers for Windows launcher
MINGW32 = i686-w64-mingw32-gcc
MINGW64 = x86_64-w64-mingw32-gcc

# Windows launcher executables
LAUNCHER_SRC = wineasio-settings-launcher.c
LAUNCHER32 = wineasio-settings.exe
LAUNCHER64 = wineasio-settings64.exe

# ---------------------------------------------------------------------------------------------------------------------

all: launchers

launchers: $(LAUNCHER32) $(LAUNCHER64)
	@echo "Windows launcher executables built"
	@echo "  32-bit: $(LAUNCHER32)"
	@echo "  64-bit: $(LAUNCHER64)"

$(LAUNCHER32): $(LAUNCHER_SRC)
	@echo "Building 32-bit Windows launcher..."
	$(MINGW32) -o $@ $< -mwindows -O2

$(LAUNCHER64): $(LAUNCHER_SRC)
	@echo "Building 64-bit Windows launcher..."
	$(MINGW64) -o $@ $< -mwindows -O2

# ---------------------------------------------------------------------------------------------------------------------
# UI code

define UI_IMPORTS
try:\\n
   from PyQt6.QtCore import Qt, QCoreApplication, QMetaObject\\n
   from PyQt6.QtWidgets import QCheckBox, QComboBox, QDialogButtonBox, QLabel, QGroupBox, QSpinBox\\n
   from PyQt6.QtWidgets import QHBoxLayout, QVBoxLayout, QSpacerItem, QSizePolicy\\n
   Qt.AlignRight = Qt.AlignmentFlag.AlignRight\\n
   Qt.AlignTrailing = Qt.AlignmentFlag.AlignTrailing\\n
   Qt.AlignVCenter = Qt.AlignmentFlag.AlignVCenter\\n
   Qt.Horizontal = Qt.Orientation.Horizontal\\n
   QSizePolicy.Fixed = QSizePolicy.Policy.Fixed\\n
   QSizePolicy.Minimum = QSizePolicy.Policy.Minimum\\n
   QDialogButtonBox.Cancel = QDialogButtonBox.StandardButton.Cancel\\n
   QDialogButtonBox.Ok = QDialogButtonBox.StandardButton.Ok\\n
   QDialogButtonBox.RestoreDefaults = QDialogButtonBox.StandardButton.RestoreDefaults\\n
except ImportError:\\n
   from PyQt5.QtCore import Qt, QCoreApplication, QMetaObject\\n
   from PyQt5.QtWidgets import QCheckBox, QComboBox, QDialogButtonBox, QLabel, QGroupBox, QSpinBox\\n
   from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QSpacerItem, QSizePolicy
endef

export UI_IMPORTS

regen: ui_settings.py

ui_%.py: %.ui
	$(PYUIC) $< -o $@
	sed -i 's/QtCore.//g' $@
	sed -i 's/QtGui.//g' $@
	sed -i 's/QtWidgets.//g' $@
	sed -i 's/_translate = QCoreApplication.translate/_tr = QCoreApplication.translate/' $@
	sed -i 's/_translate(/_tr(/g' $@
	sed -i 's/"WineASIOSettings"/self.OBJECT_NAME/g' $@
	sed -i 's/ # type: ignore//g' $@
	sed -i 's/WineASIOSettings(object):/WineASIOSettings(object):\n    OBJECT_NAME = "WineASIOSettings"\n/' $@
	sed -i 's/from PyQt5 import   QtWidgets/$(shell echo "$$UI_IMPORTS")/' $@
	sed -i 's/ except ImportError:/except ImportError:/' $@

# ---------------------------------------------------------------------------------------------------------------------

clean:
	rm -f *~ *.pyc $(LAUNCHER32) $(LAUNCHER64)

destroy: clean
	rm -f ui_*.py

# ---------------------------------------------------------------------------------------------------------------------

install: launchers
	# Create directories
	install -d $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)$(PREFIX)/share/wineasio/

	# Install script files and binaries
	install -m 755 \
		wineasio-settings \
		$(DESTDIR)$(PREFIX)/bin/

	# Adjust PREFIX value in script files
	sed -i "s?X-PREFIX-X?$(PREFIX)?" \
		$(DESTDIR)$(PREFIX)/bin/wineasio-settings

	# Install code
	install -m 644 *.py $(DESTDIR)$(PREFIX)/share/wineasio/

	# Install Windows launcher executables (for use from Wine apps like FL Studio)
	install -m 755 $(LAUNCHER32) $(DESTDIR)$(PREFIX)/share/wineasio/
	install -m 755 $(LAUNCHER64) $(DESTDIR)$(PREFIX)/share/wineasio/

	@echo ""
	@echo "Installation complete!"
	@echo ""
	@echo "Native Linux GUI: $(PREFIX)/bin/wineasio-settings"
	@echo "Windows launchers (for FL Studio etc.):"
	@echo "  $(PREFIX)/share/wineasio/$(LAUNCHER32)"
	@echo "  $(PREFIX)/share/wineasio/$(LAUNCHER64)"
	@echo ""
	@echo "To use from FL Studio, copy the appropriate .exe to your Wine prefix:"
	@echo "  cp $(PREFIX)/share/wineasio/$(LAUNCHER64) ~/.wine/drive_c/Program\ Files/WineASIO/"

# ---------------------------------------------------------------------------------------------------------------------

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wineasio-settings
	rm -rf $(DESTDIR)$(PREFIX)/share/wineasio/

# ---------------------------------------------------------------------------------------------------------------------
