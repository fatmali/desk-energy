#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
binary="$(mktemp /tmp/desk-focus-tests.XXXXXX)"
trap 'rm -f "$binary"' EXIT HUP INT TERM
for source in test/test_pomodoro.cpp test/test_desk_state.cpp test/test_timer_button.cpp test/test_daily_focus.cpp test/test_energy_state.cpp test/test_energy_storage.cpp test/test_energy_relay_config.cpp; do
  c++ -std=c++11 -Wall -Wextra -Werror -I include \
    -I .pio/libdeps/esp32dev/ArduinoJson/src "$source" -o "$binary"
  "$binary"
done
python3 -m unittest discover -s test -p 'test_*.py'
node test/test_sync_shortcut.mjs
node test/test_scriptable.mjs
