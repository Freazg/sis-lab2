#!/usr/bin/env python3
"""
Звіт про пам'ять (методичка ЛР3 §5.4), запускається в Docker-образі з тулчейном (make mem):
  docs/metrics/memory.md  — регіони, секції, модулі, найбільші символи, статична оцінка стека.

Розподіл за модулями: `size build/*.o` дає верхню оцінку, бо --gc-sections викидає частину коду,
тому таблиця будується за символами ELF (nm -l: файл визначення кожного символу).
Статична глибина стека: кадри з .su і граф викликів із .ci (-fstack-usage -fcallgraph-info=su), обхід
від main і від кожного ISR. Виклики через вказівник (hw->..., bus->...) у графі позначені __indirect_call,
тому для них перебираються всі функції, адреса яких потрапляє в app_hw_t і структури шин.
"""
import collections
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FW = ROOT / "firmware"
ELF = FW / "build" / "firmware.elf"
OUT = ROOT / "docs" / "metrics" / "memory.md"
SU_DIR = FW / "build"

# Цілі непрямих викликів: функції, що потрапляють у app_hw_t, i2c_bus_t, spi_bus_t, pwm_out_t, dev_time_t
INDIRECT = {
    "i2c_write", "i2c_write_read", "i2c_probe", "spi_select", "spi_transfer", "pwm_set", "pwm_get", "pwm_period",
    "millis", "delay_ms", "led_set", "led_get", "platform_button_pressed", "platform_mark",
    "platform_con_write", "platform_con_getc", "platform_aux_write", "platform_i2c_mem_read_restart",
    "platform_i2c_recover", "platform_cycles", "platform_micros", "platform_cal_loop", "platform_probe",
    "platform_btn_take", "platform_rx_eol_take", "platform_rx_dropped", "platform_stack_used",
    "platform_stack_size", "platform_reset", "platform_boot_fault_set", "platform_i2c_fault",
    # задачі головного циклу викликаються через run_task(task)
    "cli_task", "button_task", "imu_task", "servo_task", "rec_task", "play_task",
}
ISRS = ["SysTick_Handler", "EXTI4_15_IRQHandler", "USART2_IRQHandler"]
EXC_FRAME = 32  # апаратний кадр винятку Cortex-M0+: R0-R3, R12, LR, PC, xPSR


def sh(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True, cwd=ROOT).stdout


def module_of(path):
    p = path.replace("\\", "/")
    if "newlib" in p or "libgcc" in p or "/lib/" in p or "libc" in p:
        return "libc/libgcc"
    for key in ("App/", "Devices/", "Platform/", "Core/", "Drivers/"):
        if key in p:
            return key + pathlib.PurePosixPath(p).name
    return pathlib.PurePosixPath(p).name or "(без налагоджувальної інформації)"


def symbols():
    out = sh("arm-none-eabi-nm", "-S", "-l", "--size-sort", "--radix=d", str(ELF))
    rows = []
    for line in out.splitlines():
        m = re.match(r"(\d+) (\d+) (\w) (\S+)\s*(\S*)", line)
        if m:
            rows.append((int(m[2]), m[3], m[4], m[5].rsplit(":", 1)[0]))
    return rows


def by_module(rows):
    flash = collections.Counter()
    ram = collections.Counter()
    for size, kind, _, path in rows:
        if kind in "tTrRwW":
            flash[module_of(path) if path else "libc/libgcc"] += size
        elif kind in "bBdD":
            ram[module_of(path) if path else "libc/libgcc"] += size
    return flash, ram


def layer_totals(flash):
    layers = collections.Counter()
    for k, v in flash.items():
        layers[k.split("/")[0]] += v
    return layers


def parse_ci():
    frames = {}
    edges = collections.defaultdict(set)
    for ci in SU_DIR.rglob("*.ci"):
        text = ci.read_text(errors="replace")
        for m in re.finditer(r'node:\s*\{\s*title:\s*"([^"]+)"\s*label:\s*"([^"]*)"', text):
            name = m[1].split(":")[-1]
            fr = re.search(r"(\d+) bytes", m[2])
            if fr:
                frames[name] = max(frames.get(name, 0), int(fr[1]))
            elif name not in frames:
                frames[name] = 0
        for m in re.finditer(r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s*targetname:\s*"([^"]+)"', text):
            edges[m[1].split(":")[-1]].add(m[2].split(":")[-1])
    return frames, edges


def deepest(fn, frames, edges, stack=()):
    if fn in stack:
        return 0, []
    best, path = 0, []
    targets = set(edges.get(fn, ()))
    if "__indirect_call" in targets:
        targets |= INDIRECT
    for t in targets - {"__indirect_call"}:
        d, p = deepest(t, frames, edges, stack + (fn,))
        if d > best:
            best, path = d, p
    return frames.get(fn, 0) + best, [fn] + path


def main():
    frames, edges = parse_ci()
    rows = symbols()
    flash, ram = by_module(rows)
    usage = re.findall(r"(RAM|FLASH):\s+(\d+) B\s+(\d+) KB\s+([\d.]+)%",
                       subprocess.run(["make", "-C", "firmware", "-W", "Makefile", "build/firmware.elf",
                                       "BOARD_TARGET=wokwi", f"GIT_HASH={sys.argv[1] if len(sys.argv) > 1 else 'unknown'}"],
                                      cwd=ROOT, capture_output=True, text=True).stdout)
    size = sh("arm-none-eabi-size", str(ELF)).splitlines()[1].split()
    sections = sh("arm-none-eabi-size", "-A", str(ELF))
    main_depth, main_path = deepest("main", frames, edges)
    isr_rows = []
    isr_total = 0
    for isr in ISRS:
        d, p = deepest(isr, frames, edges)
        isr_rows.append((isr, d + EXC_FRAME, p))
        isr_total += d + EXC_FRAME
    biggest = sorted(frames.items(), key=lambda kv: -kv[1])[:12]
    ram_syms = sorted((r for r in rows if r[1] in "bBdD"), reverse=True)[:10]
    code_syms = sorted((r for r in rows if r[1] in "tTrRwW"), reverse=True)[:15]
    heap = [r for r in rows if r[2] in ("malloc", "_malloc_r", "_sbrk")]

    o = ["# Використання пам'яті", "",
         "Генерується `make mem` (tools/mem_report.py). Збірка Wokwi, `-Os`, newlib-nano, власний форматер рядків.", "",
         "## Регіони (`-Wl,--print-memory-usage`)", "", "```",
         "Memory region   Used Size  Region Size  %age Used"]
    for reg, used, total, pct in usage:
        o.append(f"{reg:>13}: {int(used):>8} B {int(total):>8} KB {pct:>9}%")
    o += ["```", "", f"`arm-none-eabi-size`: text={size[0]}, data={size[1]}, bss={size[2]}.",
          "У RAM входять резерви лінкер-скрипта: стек 1024 Б і heap 512 Б. "
          f"Heap не використовується: символів malloc/_sbrk в образі {'немає' if not heap else 'є'}.", "",
          "## Секції (`size -A`)", "", "```", sections.strip(), "```", "",
          "## Флеш за шарами", "", "| Шар | Байт | Частка |", "|---|---:|---:|"]
    total_f = sum(flash.values())
    for k, v in layer_totals(flash).most_common():
        o.append(f"| {k} | {v} | {100 * v / total_f:.1f} % |")
    o += ["", "## Модулі (символи ELF)", "", "| Модуль | Флеш, Б | RAM, Б |", "|---|---:|---:|"]
    for k, v in flash.most_common(24):
        o.append(f"| {k} | {v} | {ram.get(k, 0)} |")
    o += ["", "## Найбільші символи коду", "", "| Символ | Байт |", "|---|---:|"]
    o += [f"| `{n}` | {s} |" for s, _, n, _ in code_syms]
    o += ["", "## Найбільші змінні в RAM", "", "| Символ | Байт |", "|---|---:|"]
    o += [f"| `{n}` | {s} |" for s, _, n, _ in ram_syms]
    o += ["", "## Статична оцінка стека", "",
          f"Найглибший ланцюжок від `main`: **{main_depth} Б**", "", "`" + " → ".join(main_path) + "`", "",
          "| Обробник | Глибина з кадром винятку, Б | Ланцюжок |", "|---|---:|---|"]
    for isr, d, p in isr_rows:
        o.append(f"| {isr} | {d} | {' → '.join(p)} |")
    o += ["", f"Найгірший випадок із вкладеними перериваннями: {main_depth} + {isr_total} = "
          f"**{main_depth + isr_total} Б** при резерві 1024 Б.", "",
          "| Найбільші кадри | Байт |", "|---|---:|"]
    o += [f"| `{n}` | {s} |" for n, s in biggest]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(o) + "\n", encoding="utf-8")
    print(f"written {OUT}: main={main_depth}, isr={isr_total}")


if __name__ == "__main__":
    main()
