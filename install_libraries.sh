#!/usr/bin/env bash
# =====================================================================
#  install_libraries.sh — copy the bundled libraries into the Arduino IDE
#  ---------------------------------------------------------------------
#  The project ships the two required libraries in ./libraries:
#     * TFT_eSPI             (already configured for the CYD)
#     * XPT2046_Touchscreen
#
#  This script copies them into your Arduino "libraries" folder so the
#  sketch compiles without downloading anything from the Library Manager.
#
#  Usage:
#     ./install_libraries.sh                 # auto-detect (~/Arduino/libraries)
#     ./install_libraries.sh /path/to/libraries   # explicit destination
#
#  Windows users: just copy the two folders inside ./libraries into
#     Documents\Arduino\libraries\
#  manually (see the README).
# =====================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"

# --- pick a destination ---
if [ "${1:-}" != "" ]; then
  DEST="$1"
else
  # common Arduino sketchbook locations
  for cand in "$HOME/Arduino/libraries" \
              "$HOME/Documents/Arduino/libraries" \
              "$HOME/Documents/ArduinoData/libraries"; do
    if [ -d "$(dirname "$cand")" ]; then DEST="$cand"; break; fi
  done
  DEST="${DEST:-$HOME/Arduino/libraries}"
fi

mkdir -p "$DEST"
echo "Installing libraries into: $DEST"

for lib in TFT_eSPI XPT2046_Touchscreen; do
  SRC="$HERE/libraries/$lib"
  if [ ! -d "$SRC" ]; then
    echo "  !! missing $SRC" >&2
    exit 1
  fi
  rm -rf "$DEST/$lib"
  cp -r "$SRC" "$DEST/$lib"
  echo "  installed $lib"
done

echo
echo "Done. Restart the Arduino IDE, then open:"
echo "  $HERE/ESP32_CYD_Oscilloscope/ESP32_CYD_Oscilloscope.ino"
