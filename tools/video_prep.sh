#!/bin/sh
# Підготовка екрана до відеозвіту ЛР2 (сценарій — docs/lab_2/video-script.md).
# Відкриває: PDF звіту, VS Code зі схемою Wokwi, PulseView із записом I2C,
# Terminal з результатами тестів, сторінку репозиторію на GitHub.
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

echo "1/5 Збірка прошивки і custom chips..."
make build >/dev/null
[ -f chips/dist/w25q64.chip.wasm ] || make chips >/dev/null

echo "2/5 PDF звіту (титульний аркуш — кадр 0:00)"
open "$ROOT/docs/lab2-report.pdf"

echo "3/5 VS Code зі схемою (F1 -> Wokwi: Start Simulator — кадр 0:15)"
"/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code" "$ROOT" -g "$ROOT/diagram.json"

echo "4/5 PulseView із записом I2C (кадр 1:25)"
PV="$HOME/Applications/PulseView.app/Contents/MacOS/pulseview"
pkill -f "PulseView.app/Contents/MacOS/pulseview" 2>/dev/null || true
nohup "$PV" -c -i "$ROOT/docs/captures/pulseview/i2c-whoami-burst.vcd" >/dev/null 2>&1 &

echo "5/5 Terminal з тестами (кадр 2:20) і GitHub (кадр 2:40)"
osascript -e "tell application \"Terminal\" to do script \"cd '$ROOT' && clear && make test 2>&1 | grep -E 'Tests|FAIL' && make coverage 2>&1 | grep -E 'lines:|branches:'\"" >/dev/null
open "https://github.com/Freazg/sis-lab2"

echo "Готово. Перемикайся між вікнами ⌘Tab за сценарієм."
