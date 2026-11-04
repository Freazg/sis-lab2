#!/usr/bin/env python3
"""
Пам'ять прошивки на FreeRTOS (методичка ЛР4 §6.4, §6.5, §7.3) -> docs/metrics/memory-lr4.md.

Запуск у Docker-образі з gdb-multiarch (розміри структур ядра беруться з DWARF):
  docker run --rm -v "$PWD":/workspace -w /workspace stm32-gdb python3 tools/mem_report_lr4.py

Що рахується:
  * регіони і секції ELF прошивки ЛР4;
  * внесок ядра з map-файла: tasks.o, queue.o, list.o, heap_4.o, port.o, portasm.o (ядро збирається без
    LTO саме для цього; власний код і HAL — з LTO, їх розкладено за символами ELF, nm -l);
  * склад heap ОС: стеки задач, TCB, черги, м'ютекси (з розмірів у Rtos/rtos_app.c і sizeof з DWARF);
    виміряне «використано» = total - min_ever_free береться з логу S3 (docs/metrics/runs-lr4/s3-p*.log, остання сесія);
  * та сама прошивка ЛР3 (тег lab3, worktree build-lr3/) у двох збірках: як у тегу і з LTO.
"""
import collections
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
FW = ROOT / "firmware"
BUILD = "build-mem"
ELF = FW / BUILD / "firmware.elf"
MAP = FW / BUILD / "firmware.map"
OUT = ROOT / "docs" / "metrics" / "memory-lr4.md"
LR3 = ROOT / "build-lr3"
S3_LOG = max((ROOT / "docs" / "metrics" / "runs-lr4").glob("s3-p[0-9].log"), default=ROOT / "docs" / "metrics" / "runs-lr4" / "s3.log")


def sh(*cmd, cwd=ROOT):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd).stdout


def build(cwd, build_dir, extra=()):
    subprocess.run(["rm", "-rf", str(cwd / "firmware" / build_dir)])  # інакше без перелінковки немає виводу ld
    out = subprocess.run(["make", "-C", "firmware", "all", "BOARD_TARGET=wokwi", f"BUILD_DIR={build_dir}",
                          "GIT_HASH=mem", *extra], cwd=cwd, capture_output=True, text=True).stdout
    return {reg: int(used) for reg, used in re.findall(r"(RAM|FLASH):\s+(\d+) B", out)}


def map_objects():
    """Розмістені вхідні секції map-файла: об'єкт -> (флеш, RAM)."""
    lines = MAP.read_text(errors="replace").splitlines()
    start = next(i for i, l in enumerate(lines) if l.startswith("Linker script and memory map"))
    flash = collections.Counter()
    ram = collections.Counter()
    i = start
    while i < len(lines):
        m = re.match(r"^ (\.\w+)(\S*)\s*$", lines[i])
        if m and i + 1 < len(lines):
            parts = lines[i + 1].split()
            i += 1
        else:
            m = re.match(r"^ (\.\w+)(\S*)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)", lines[i])
            parts = [m[3], m[4], m[5]] if m else None
        if m and parts and len(parts) >= 3 and parts[0].startswith("0x"):
            addr, size, obj = int(parts[0], 16), int(parts[1], 16), parts[2]
            sec = m[1]
            if size:
                key = obj.split("/")[-1]
                if "ltrans" in key:
                    key = "LTO: власний код + HAL"
                elif ".a(" in key:
                    key = "libc/libgcc"
                if sec in (".text", ".rodata", ".isr_vector") or (sec == ".data" and addr >= 0x20000000):
                    if addr < 0x20000000 or sec == ".data":
                        flash[key] += size
                if sec in (".data", ".bss") and addr >= 0x20000000:
                    ram[key] += size
        i += 1
    return flash, ram


def module_of(path):
    p = path.replace("\\", "/")
    if not p or "newlib" in p or "libgcc" in p or "/lib/" in p or "libc" in p:
        return "libc/libgcc"
    for key in ("Rtos/", "App/", "Devices/", "Platform/", "Core/", "Drivers/", "Middlewares/"):
        if key in p:
            return key.rstrip("/")
    return "інше"


def layers():
    out = sh("arm-none-eabi-nm", "-S", "-l", "--size-sort", "--radix=d", str(ELF))
    flash = collections.Counter()
    for line in out.splitlines():
        m = re.match(r"(\d+) (\d+) (\w) (\S+)\s*(\S*)", line)
        if m and m[3] in "tTrR":
            flash[module_of(m[5].rsplit(":", 1)[0])] += int(m[2])
    return flash


def sizeof(*names):
    args = []
    for n in names:
        args += ["-ex", f"p sizeof({n})"]
    out = sh("gdb-multiarch", "-q", "-batch", *args, str(ELF))
    return [int(v) for v in re.findall(r"\$\d+ = (\d+)", out)]


def task_stacks():
    src = (FW / "Rtos" / "rtos_app.c").read_text()
    st = dict(re.findall(r"STACK_(\w+) = (\w+)", src))
    st["IMU"] = re.search(r"#define RTOS_STACK_IMU (\d+)", src)[1]
    return {k.lower(): int(v) for k, v in st.items()}


def measured_heap():
    logs = sorted(S3_LOG.parent.glob("s3-p[0-9].log")) or [S3_LOG]
    text = "".join(q.read_text(errors="replace") for q in logs if q.exists())
    m = re.findall(r"HEAP free=(\d+) min_ever_free=(\d+) total=(\d+)\r?\n", text)  # лише цілі рядки: останній у логу буває обрізаний
    return tuple(map(int, m[-1])) if m else None


def main():
    lr4 = build(ROOT, BUILD)
    flash_obj, ram_obj = map_objects()
    lay = layers()
    tcb, queue = sizeof("TCB_t", "Queue_t")
    log_rec, traj_msg, heap_total = sizeof("log_rec_t", "traj_msg_t", "ucHeap")
    stacks = task_stacks()
    hdr = 8  # heap_4: заголовок блоку BlockLink_t, вирівнювання portBYTE_ALIGNMENT = 8

    def blk(n):
        return (n + hdr + 7) // 8 * 8

    rows = [(f"стек {name} ({w} слів)", blk(w * 4)) for name, w in stacks.items()]
    rows += [("стек IDLE (64 слова)", blk(64 * 4))]
    rows += [(f"TCB × 8 (sizeof(TCB_t) = {tcb})", 8 * blk(tcb))]
    rows += [(f"черга log: 4 × {log_rec} Б + Queue_t", blk(queue + 4 * log_rec)),
             (f"черга traj: 16 × {traj_msg} Б + Queue_t", blk(queue + 16 * traj_msg)),
             (f"м'ютекси CON, I2C, FLASH: 3 × Queue_t ({queue})", 3 * blk(queue))]
    est = sum(v for _, v in rows)
    meas = measured_heap()

    lr3_tag = lr3_lto = None
    if (LR3 / "firmware").exists():
        lr3_tag = build(LR3, "build-mem")
        lr3_lto = build(LR3, "build-mem-lto", ["MCU=-mcpu=cortex-m0plus -mthumb -flto"])

    usage = sh("arm-none-eabi-size", "-A", str(ELF))
    kernel = {k: v for k, v in flash_obj.items() if k in ("tasks.o", "queue.o", "list.o", "heap_4.o", "port.o",
                                                          "portasm.o")}
    o = ["# Пам'ять прошивки на FreeRTOS (ЛР4)", "",
         "Генерується `tools/mem_report_lr4.py`. Збірка Wokwi, `-Os`, LTO для власного коду і HAL, ядро без LTO.", "",
         "## Регіони", "", "| Регіон | ЛР4 | Ліміт |", "|---|---:|---:|",
         f"| FLASH | {lr4['FLASH']} Б | 32 768 Б |", f"| RAM | {lr4['RAM']} Б | 12 288 Б |", "",
         "## Ядро FreeRTOS у флеші і RAM (map-файл, §6.5)", "", "| Об'єкт | Флеш, Б | RAM, Б |", "|---|---:|---:|"]
    for k in ("tasks.o", "queue.o", "list.o", "heap_4.o", "port.o", "portasm.o"):
        o.append(f"| `{k}` | {flash_obj.get(k, 0)} | {ram_obj.get(k, 0)} |")
    o.append(f"| **разом ядро** | **{sum(kernel.values())}** | **{sum(ram_obj.get(k, 0) for k in kernel)}** |")
    o += ["", f"`heap_4.o` у RAM — масив `ucHeap` (configTOTAL_HEAP_SIZE = {heap_total} Б): у ньому стеки задач, TCB і черги.",
          "", "## Флеш за шарами (символи ELF)", "", "| Шар | Байт |", "|---|---:|"]
    for k, v in lay.most_common():
        o.append(f"| {k} | {v} |")
    o += ["", "## Склад heap ОС (§6.4)", "", "| Об'єкт | Байт з заголовком heap_4 |", "|---|---:|"]
    o += [f"| {n} | {v} |" for n, v in rows]
    o += [f"| **оцінка зайнятого** | **{est}** |", f"| зарезервовано (configTOTAL_HEAP_SIZE) | {heap_total} |", ""]
    if meas:
        free, min_free, total = meas
        o += [f"Виміряно після S3 (команда `heap`): free = {free} Б, min_ever_free = {min_free} Б, total = {total} Б. "
              f"Використано на піку {total - min_free} Б (оцінка вище — {est} Б; різниця — тимчасовий масив "
              "`vTaskGetRunTimeStatistics` у команді `tasks`).", ""]
    if lr3_tag:
        o += ["## Порівняння з ЛР3 (той самий тулчейн, той самий день)", "",
              "| Збірка | Флеш, Б | RAM, Б |", "|---|---:|---:|",
              f"| ЛР3, тег lab3, як у тегу (без LTO) | {lr3_tag['FLASH']} | {lr3_tag['RAM']} |",
              f"| ЛР3, тег lab3, з LTO | {lr3_lto['FLASH']} | {lr3_lto['RAM']} |",
              f"| ЛР4 | {lr4['FLASH']} | {lr4['RAM']} |", ""]
    o += ["## Секції", "", "```", usage.strip(), "```"]
    OUT.write_text("\n".join(o) + "\n", encoding="utf-8")
    print(f"written {OUT}: flash={lr4['FLASH']} ram={lr4['RAM']} kernel={sum(kernel.values())} heap_est={est}")


if __name__ == "__main__":
    main()
