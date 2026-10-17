#!/usr/bin/env python3
"""
Автоматична перевірка правил залежностей між шарами прошивки (методичка ЛР2, п. 4.5).

Шари і дозволені #include (див. docs/architecture.md):

  App/       -> App/, Devices/, стандартна бібліотека C
  Devices/   -> Devices/, стандартна бібліотека C
  Platform/  -> Platform/, Devices/ (лише інтерфейси шин), HAL/CMSIS, Core/Inc/main.h
  Core/      -> будь-що (composition root)

Заборонено, зокрема: App -> HAL / Platform / main.h; Devices -> HAL / Platform / App;
Platform -> App. Будь-яке порушення — ненульовий код виходу (ламає `make build` і CI).
"""
import pathlib
import re
import sys

FW = pathlib.Path(__file__).resolve().parent.parent / "firmware"
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)

STD = {
    "stdint.h", "stddef.h", "stdbool.h", "stdio.h", "stdlib.h", "string.h", "stdarg.h",
    "ctype.h", "limits.h", "math.h", "assert.h",
}


def headers_in(layer):
    return {p.name for p in (FW / layer).glob("*.h")}


LAYERS = {name: headers_in(name) for name in ("App", "Devices", "Platform")}
CORE_INC = {p.name for p in (FW / "Core" / "Inc").glob("*.h")}
VENDOR = re.compile(r"^(stm32c0xx.*|core_cm0plus|cmsis_.*|system_stm32c0xx)\.h$")

RULES = {
    "App": lambda h: h in STD or h in LAYERS["App"] or h in LAYERS["Devices"],
    "Devices": lambda h: h in STD or h in LAYERS["Devices"],
    "Platform": lambda h: (h in STD or h in LAYERS["Platform"] or h in LAYERS["Devices"]
                           or VENDOR.match(h) is not None or h == "main.h"),
}

WHY = {
    "App": "прикладний шар не знає про HAL, плату і Platform/",
    "Devices": "драйвер залежить лише від інтерфейсів шин (Devices/bus.h)",
    "Platform": "адаптер плати не може залежати від прикладної логіки",
}


def main():
    errors = []
    checked = 0
    for layer, allowed in RULES.items():
        for src in sorted((FW / layer).glob("*.[ch]")):
            checked += 1
            for inc in INCLUDE.findall(src.read_text(encoding="utf-8")):
                name = pathlib.PurePosixPath(inc).name
                if not allowed(name):
                    errors.append(f"{src.relative_to(FW)}: #include \"{inc}\" — {WHY[layer]}")
    if errors:
        print("check_deps: ПОРУШЕННЯ правил залежностей:")
        for e in errors:
            print("  " + e)
        return 1
    print(f"check_deps: OK ({checked} файлів App/Devices/Platform перевірено)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
