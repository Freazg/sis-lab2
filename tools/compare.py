#!/usr/bin/env python3
"""
Порівняння super loop (ЛР3) і FreeRTOS (ЛР4), методичка ЛР4 §7.3–7.4 -> docs/comparison.md.

Вхід: docs/metrics/results-lr3.json і results-lr4.json (tools/analyze_metrics.py --fw ...), логи сценаріїв ОС
docs/metrics/runs-lr4/os-*.log і s3-tick100.log, звіти пам'яті docs/metrics/memory.md (ЛР3) і memory-lr4.md.
Обидві колонки виміряно тим самим інструментом у той самий день; інструмент — у колонці «Спосіб».
"""
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
MET = ROOT / "docs" / "metrics"
OUT = ROOT / "docs" / "comparison.md"


def load(fw):
    p = MET / f"results-{fw}.json"
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else {"runs": {}, "wire": {}}


def log(fw, name):
    p = MET / f"runs-{fw}" / f"{name}.log"
    return p.read_text(encoding="utf-8", errors="replace") if p.exists() else ""


def blk(r, name, i=-1):
    try:
        return r["runs"][name]["blocks"][i]
    except (KeyError, IndexError):
        return None


def stat(r, name, key, field="max"):
    b = blk(r, name)
    if not b or key not in b["stat"] or not b["stat"][key]["n"]:
        return None
    return b["stat"][key][field]


def jit(r, name, field, key="imu"):
    b = blk(r, name)
    return b["jit"][key][field] if b and key in b["jit"] else None


def load_max(r, name):
    b = blk(r, name)
    return b["load"]["max"] if b and "load" in b else None


def cnt(r, name, field):
    b = blk(r, name)
    return b["cnt"][field] if b and "cnt" in b else None


def fmt_us(v):
    if v is None:
        return "—"
    return f"{v / 1000:.1f} мс" if abs(v) >= 1000 else f"{v:.0f} мкс" if isinstance(v, float) else f"{v} мкс"


def fmt_n(v, unit=""):
    return "—" if v is None else f"{v}{unit}"


def diff(a, b, kind):
    """Абсолютна і відносна різниця: «−96,7 мс» і «÷ 790»/«+8 %»."""
    if a is None or b is None:
        return "—"
    d = b - a
    if kind == "us":
        absd = ("+" if d > 0 else "−" if d < 0 else "") + fmt_us(abs(d))
    elif kind == "pct":
        absd = f"{d:+d} п.п."
    else:
        absd = f"{d:+d}" if isinstance(d, int) else f"{d:+.1f}"
    if a and b and kind in ("us", "n") and a > 0 and b > 0:
        ratio = a / b
        rel = f"÷ {ratio:.0f}" if ratio >= 2 else (f"× {b / a:.0f}" if b / a >= 2 else f"{100 * d / a:+.0f} %")
        return f"{absd} ({rel})"
    return absd


def better(a, b, lower_is_better=True, tie=0.05):
    if a is None or b is None:
        return "—"
    if a == b or (max(a, b) and abs(a - b) / max(abs(a), abs(b)) < tie):
        return "≈"
    return ("FreeRTOS" if b < a else "super loop") if lower_is_better else ("FreeRTOS" if b > a else "super loop")


def p_long_max_ms(fw, name):
    """Тривалість довгої операції: найширший імпульс P_LONG на записі аналізатора (обидві прошивки ставлять
    P_LONG на весь dump і на кожне стирання)."""
    import analyze_captures as ac  # noqa: E402
    p = ROOT / "docs" / "captures" / "cmp" / f"{fw}-{name}.vcd"
    if not p.exists():
        return None
    sig = ac.load_vcd(p)["P_LONG"]
    w, rise = [], None
    for t, v in sig:
        if v == 1:
            rise = t
        elif rise is not None:
            w.append(t - rise)
            rise = None
    return max(w) / 1000 if w else None  # мкс


def mem_lr3():
    t = (MET / "memory.md").read_text(encoding="utf-8")
    f = int(re.search(r"FLASH:\s+(\d+) B", t)[1])
    ram = int(re.search(r"RAM:\s+(\d+) B", t)[1])
    return f, ram


def mem_lr4():
    p = MET / "memory-lr4.md"
    if not p.exists():
        return None, None, None
    t = p.read_text(encoding="utf-8")
    f = int(re.search(r"\| FLASH \| (\d+) Б", t)[1])
    ram = int(re.search(r"\| RAM \| (\d+) Б", t)[1])
    kernel = int(re.search(r"\*\*разом ядро\*\* \| \*\*(\d+)\*\*", t)[1])
    return f, ram, kernel


def os_tasks(text):
    """Два знімки tasks навколо вікна вимірювання -> частка кожної задачі за вікно, %."""
    snaps = []
    for chunk in text.split("TASKS,BEGIN,t_us=")[1:]:
        t_us = int(re.match(r"(\d+)", chunk)[1])
        body = chunk.split("TASKS,END")[0]
        rows = {m[1]: int(m[2]) for m in re.finditer(r"(\w+)\s+\t?(\d+)\t+", body)}
        sw = re.search(r"sw=(\d+)", body)
        snaps.append((t_us, rows, int(sw[1]) if sw else None))
    if len(snaps) < 2:
        return None
    (t0, a, s0), (t1, b, s1) = snaps[0], snaps[-1]
    span = t1 - t0
    share = {k: round(100 * (b[k] - a.get(k, 0)) / span, 1) for k in b}
    return {"span_s": round(span / 1e6, 1), "share": share,
            "switches_per_s": round((s1 - s0) / (span / 1e6), 1) if s0 is not None and s1 is not None else None}


def os_stack(text):
    rows = re.findall(r"STACK task=(\w+) free_min_words=(\d+) size_words=(\d+)", text)
    out = {}
    for name, free, size in rows:
        out[name] = (int(size), min(int(free), out.get(name, (0, 10 ** 9))[1]))
    msp = re.findall(r"STACK task=MSP used=(\d+) reserved=(\d+)", text)
    return out, (int(msp[-1][0]), int(msp[-1][1])) if msp else None


def os_heap(text):
    m = re.findall(r"HEAP free=(\d+) min_ever_free=(\d+) total=(\d+)", text)
    return tuple(map(int, m[-1])) if m else None


def os_ctxsw(text):
    sw = [tuple(map(int, m)) for m in re.findall(r"CTXSW n=\d+ min_cyc=(\d+) avg_cyc=(\d+) max_cyc=(\d+)", text)]
    tick = [tuple(map(int, m)) for m in re.findall(r"OSTICK hz=\d+ n=\d+ min_cyc=(\d+) avg_cyc=(\d+) max_cyc=(\d+)",
                                                   text)]
    return sw, tick


def loc(paths, ref=None):
    files = []
    for p in paths:
        if ref:
            out = subprocess.run(["git", "ls-tree", "-r", "--name-only", ref, p], cwd=ROOT, capture_output=True,
                                 text=True).stdout.split()
            files += [subprocess.run(["git", "show", f"{ref}:{f}"], cwd=ROOT, capture_output=True,
                                     text=True).stdout for f in out if f.endswith((".c", ".h"))]
        else:
            files += [f.read_text() for f in (ROOT / p).rglob("*.[ch]")]
    lines = [l for t in files for l in t.splitlines()]
    return sum(1 for l in lines if l.strip() and not re.match(r"\s*(/\*|\*|//)", l))


def main():
    r3, r4 = load("lr3"), load("lr4")
    rows = []

    def add(metric, scen, a, b, kind, need, how, lower=True):
        fa = fmt_us(a) if kind == "us" else fmt_n(a, " %" if kind == "pct" else "")
        fb = fmt_us(b) if kind == "us" else fmt_n(b, " %" if kind == "pct" else "")
        rows.append(f"| {metric} | {scen} | {fa} | {fb} | {diff(a, b, kind)} | {need} | {better(a, b, lower)} | {how} |")

    add("Затримка реакції на кнопку, max", "S3", stat(r3, "s3", "btn_latency"), stat(r4, "s3", "btn_latency"), "us",
        "≤ 100 мс (FR-06, tasks.md)", "лічильник МК")
    w3, w4 = (r.get("wire", {}).get("button_s3") or {} for r in (r3, r4))
    add("Затримка реакції на кнопку, max", "S3", w3.get("max"), w4.get("max"), "us", "≤ 100 мс", "аналізатор: BTN → P_REACT")
    add("Затримка кнопки під час dump, max", "6.2.2", stat(r3, "sc2-btn-long", "btn_latency"),
        stat(r4, "sc2-btn-long", "btn_latency"), "us", "≤ 100 мс", "лічильник МК")
    add("Затримка кнопки без навантаження, max", "6.2.2", stat(r3, "sc2-btn-idle", "btn_latency"),
        stat(r4, "sc2-btn-idle", "btn_latency"), "us", "≤ 100 мс", "лічильник МК")
    for s in ("s1", "s2", "s3"):
        add("Джитер IMU, max", s.upper(), jit(r3, s, "max"), jit(r4, s, "max"), "us", "≤ 0,5 мс (FR-01)", "лічильник МК")
    for s in ("s2", "s3"):
        add("Пропущені запуски IMU", s.upper(), jit(r3, s, "missed"), jit(r4, s, "missed"), "n", "0 (FR-01)",
            "лічильник МК")
        add("Порушення ±0,5 мс", s.upper(), jit(r3, s, "viol"), jit(r4, s, "viol"), "n", "0 (FR-01)", "лічильник МК")
    for s in ("s2", "s3"):
        add("Команда → відповідь, max", s.upper(), stat(r3, s, "cmd_latency"), stat(r4, s, "cmd_latency"), "us",
            "≤ 100 мс (FR-09, tasks.md)", "лічильник МК")
    for s in ("s2", "fr03"):
        add("Рух → запис CCR, max", s.upper(), stat(r3, s, "e2e_latency"), stat(r4, s, "e2e_latency"), "us",
            "≤ 50 мс (FR-03)", "лічильник МК")
    add("Втрачені байти UART", "6.2.3 потік", cnt(r3, "sc3-flood", "rx_dropped"), cnt(r4, "sc3-flood", "rx_dropped"),
        "n", "0", "лічильник ISR")
    add("Команда → відповідь під час dump, max", "6.2.3 потік", stat(r3, "sc3-flood", "cmd_latency"),
        stat(r4, "sc3-flood", "cmd_latency"), "us", "≤ 100 мс", "лічильник МК")
    add("Час довгої операції (dump 500 семплів)", "6.2.1", p_long_max_ms("lr3", "sc1-dump"),
        p_long_max_ms("lr4", "sc1-dump"), "us", "—", "аналізатор: ширина P_LONG")
    add("Джитер IMU під час стирання 16 секторів", "6.2.1", jit(r3, "sc1-erase", "max"), jit(r4, "sc1-erase", "max"),
        "us", "≤ 0,5 мс", "лічильник МК")
    f8 = [(r.get("fr08") or {}).get("diff_ms") for r in (r3, r4)]
    add("FR-08: тривалість відтворення − запису", "fr07-08", f8[0] and round(f8[0] * 1000),
        f8[1] and round(f8[1] * 1000), "us", "≤ 20 мс (FR-08)", "мітки МК REC/PLAY,DONE")
    for s in ("s1", "s2", "s3"):
        add("Завантаження CPU, max", s.upper(), load_max(r3, s), load_max(r4, s), "pct", "—",
            "ЛР3: зайнятість циклу; ЛР4: 100 % − idle", lower=True)
    boot = [((r.get("boot") or {}).get("first_imu_us") or [None])[0] for r in (r3, r4)]
    add("Час від скидання до першого виміру IMU", "6.2.7", boot[0], boot[1], "us", "—", "мітка МК")
    sw = [(r.get("sweep") or []) for r in (r3, r4)]

    def ceiling(rows_):
        ok = [x["imu_runs"] / 3 for x in rows_ if x.get("viol") == 0]
        return round(max(ok)) if ok else None

    add("Стеля частоти IMU без порушень, Гц", "6.2.5", ceiling(sw[0]), ceiling(sw[1]), "n", "≥ 200 Гц (FR-01)",
        "лічильник МК", lower=False)

    f3, ram3 = mem_lr3()
    f4, ram4, kern = mem_lr4()
    add("Flash, всього, Б", "—", f3, f4, "n", "≤ 32 768 Б (C031C6)", "map, `--print-memory-usage`")
    add("RAM, статично (data + bss + резерви), Б", "—", ram3, ram4, "n", "≤ 12 288 Б", "`--print-memory-usage`")
    l3 = log("lr4", "s3")
    st, msp = os_stack(l3 or log("lr4", "s1"))
    heap = os_heap(l3)
    if st:
        res4 = sum(s * 4 for s, _ in st.values()) + (msp[1] if msp else 0)
        used4 = sum((s - f) * 4 for s, f in st.values()) + (msp[0] if msp else 0)
        rows.append(f"| Стек: зарезервовано / використано | S3 | 1024 / 912 Б | {res4} / {used4} Б | "
                    f"{res4 - 1024:+d} / {used4 - 912:+d} Б | — | super loop | ЛР3: stack painting; ЛР4: high water mark + MSP |")
    if heap:
        rows.append(f"| Heap ОС: зарезервовано / використано на піку | S3 | 0 | {heap[2]} / {heap[2] - heap[1]} Б | "
                    f"+{heap[2]} Б | — | super loop | `heap` |")
    loc3 = loc(["firmware/App"], "refs/tags/lab3")
    loc4 = loc(["firmware/App", "firmware/Rtos"])
    add("Рядків коду в прикладному шарі (без порожніх і коментарів)", "—", loc3, loc4, "n", "—",
        "ЛР3: App/; ЛР4: App/ + Rtos/")

    o = ["# Порівняння super loop і FreeRTOS", "",
         "Генерується `tools/compare.py`. Колонка «ЛР3» — прошивка з тегу `lab3`, «ЛР4» — поточна; обидві виміряно "
         "тим самим стендом Wokwi, тими самими сценаріями (`tools/run_scenarios.py --fw lr3|lr4`) в один день. "
         "Порівнюється максимум, середнє — лише як доповнення (методичка §7.3).", "",
         "## Порівняльна таблиця", "",
         "| Метрика | Сценарій | Super loop (ЛР3) | FreeRTOS (ЛР4) | Різниця | Вимога PRD | Хто кращий | Спосіб |",
         "|---|---|---:|---:|---|---|---|---|"] + rows

    # ---- метрики ОС ----
    o += ["", "## Метрики, специфічні для ОС (§6)", ""]
    ts = os_tasks(l3)
    if ts:
        o += [f"Завантаження за задачами в S3 (вікно {ts['span_s']} с, різниця двох знімків `tasks`):", "",
              "| Задача | Частка часу |", "|---|---:|"]
        o += [f"| {k} | {v} % |" for k, v in sorted(ts["share"].items(), key=lambda kv: -kv[1])]
        if ts["switches_per_s"]:
            o += ["", f"Перемикань контексту: {ts['switches_per_s']} за секунду."]
    if st:
        o += ["", "Запас стека кожної задачі після S3 (`stack`, слова по 4 Б):", "",
              "| Задача | Розмір | Мінімальний вільний залишок | Використано, Б |", "|---|---:|---:|---:|"]
        o += [f"| {k} | {s} | {f} | {(s - f) * 4} |" for k, (s, f) in st.items()]
        if msp:
            o.append(f"| MSP (переривання) | {msp[1] // 4} | {(msp[1] - msp[0]) // 4} | {msp[0]} |")
    cs = log("lr4", "os-ctxsw")
    sw_, tick_ = os_ctxsw(cs)
    if sw_:
        o += ["", "| Вартість ОС (`ctxsw`) | min | avg | max |", "|---|---:|---:|---:|"]
        for title, (mn, av, mx) in zip(("перемикання контексту, HOLD", "перемикання контексту, під час dump"), sw_):
            o.append(f"| {title}, такти | {mn} | {av} | {mx} |")
        for title, (mn, av, mx) in zip(("обробник тіку, HOLD", "обробник тіку, під час dump"), tick_):
            o.append(f"| {title}, такти | {mn} | {av} | {mx} |")
        if ts and ts["switches_per_s"] and tick_:
            ov = (ts["switches_per_s"] * sw_[0][1] + 1000 * tick_[0][1]) / 48e6 * 100
            o += ["", f"Накладні витрати ядра в S3 ≈ ({ts['switches_per_s']} × {sw_[0][1]} + 1000 × {tick_[0][1]}) / 48·10⁶ = "
                  f"**{ov:.1f} %** процесорного часу (такти моделі Wokwi)."]

    # ---- сценарії 7.2 ----
    o += ["", "## Сценарії, специфічні для ОС (§7.2)", ""]
    inv = re.findall(r"INV,kind=(\w+),hold_us=(\d+),medium_us=(\d+),wait_us=(\d+)", log("lr4", "os-inv"))
    if inv:
        o += ["### 7.2.1 Інверсія пріоритетів", "", "| Ресурс | Тримає `long` (1) | Займає процесор `log` (2) | Чекає `cli` (3) |",
              "|---|---:|---:|---:|"]
        o += [f"| {k} | {fmt_us(int(h))} | {fmt_us(int(m))} | {fmt_us(int(w))} |" for k, h, m, w in inv]
    for name, title in (("os-stack", "7.2.2 Переповнення стека (стек `imu` 96 слів)"),
                        ("os-stack-nocheck", "7.2.2 Те саме без контролю переповнення"),
                        ("os-heap", "7.2.5 Вичерпання heap (6144 Б)")):
        t = log("lr4", name)
        if t:
            fatal = re.findall(r"FATAL[^\r\n]*", t)
            o += ["", f"### {title}", "", f"Результат: `{fatal[0]}`." if fatal else
                  "Хук не спрацював. Останні рядки логу: `" + " / ".join(t.strip().splitlines()[-3:]) + "`."]
    stv = log("lr4", "os-starve")
    if stv:
        m = re.search(r"STARVE,flag_seen=(\d+),waited_ms=(\d+)", stv)
        tsv = os_tasks(stv)
        o += ["", "### 7.2.3 Голодування", ""]
        if m:
            o.append(f"`cli` активно чекав прапорця {m[2]} мс; прапорець від `long` побачено: {m[1]}.")
        if tsv:
            o.append("Частка процесора за вікно: " + ", ".join(f"{k} {v} %" for k, v in
                                                                sorted(tsv["share"].items(), key=lambda kv: -kv[1])) + ".")
    qf = re.findall(r"QFLOOD,timeout_ms=(\d+),depth=(\d+),sent=(\d+),lost=(\d+),producer_us=(\d+)", log("lr4", "os-qflood"))
    if qf:
        o += ["", "### 7.2.4 Вичерпання черги", "", "| Timeout відправника | Глибина | Відправлено | Втрачено | Час відправника |",
              "|---:|---:|---:|---:|---:|"]
        o += [f"| {t} мс | {d} | {s} | {l} | {fmt_us(int(p))} |" for t, d, s, l, p in qf]
    t100 = log("lr4", "s3-tick100")
    if t100:
        r100 = re.findall(r"JITTER name=imu period_us=(\d+) n=(\d+) min_us=(-?\d+) max_us=(-?\d+) p99_us=(\d+) "
                          r"missed=(\d+) viol=(\d+)", t100)
        load100 = re.findall(r"LOAD pct=(\d+) min=(\d+) max=(\d+)", t100)
        if r100:
            p, n, mn, mx, p99, missed, viol = r100[-1]
            o += ["", "### 7.2.6 Частота тіку 100 Гц (S3)", "",
                  "| Тік | Джитер IMU, max | Запусків | Пропуски | Порушення | Завантаження, max |", "|---|---:|---:|---:|---:|---:|",
                  f"| 1000 Гц | {fmt_us(jit(r4, 's3', 'max'))} | {jit(r4, 's3', 'n')} | {jit(r4, 's3', 'missed')} | "
                  f"{jit(r4, 's3', 'viol')} | {load_max(r4, 's3')} % |",
                  f"| 100 Гц | {fmt_us(int(mx))} | {n} | {missed} | {viol} | {load100[-1][2] if load100 else '—'} % |"]
    OUT.write_text("\n".join(o) + "\n", encoding="utf-8")
    print(f"written {OUT}: {len(rows)} rows")


if __name__ == "__main__":
    import sys
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    main()
