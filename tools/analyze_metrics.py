#!/usr/bin/env python3
"""
Зведення результатів ЛР3 (методичка §6.3): docs/metrics/runs/*.log і docs/captures/lr3-*.vcd ->
docs/metrics/results.json, docs/metrics/results.md.

Числа з логів — час МК (мітки всередині прошивки). Числа з VCD — час на дроті: незалежний спосіб
вимірювання того самого (логічний аналізатор Wokwi, щупи на службових GPIO).

ЛР4: python3 tools/analyze_metrics.py --fw lr3|lr4 — ті самі функції над docs/metrics/runs-<fw>/ і
docs/captures/cmp/<fw>-*.vcd, результат у docs/metrics/results-<fw>.json (порівняння — tools/compare.py).
"""
import argparse
import json
import pathlib
import re
import statistics
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import analyze_captures as ac  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
RUNS = ROOT / "docs" / "metrics" / "runs"
CAP = ROOT / "docs" / "captures"
OUT = ROOT / "docs" / "metrics"

STAT = re.compile(r"STAT name=(\S+) n=(\d+) min_us=(\d+) avg_us=(\d+) max_us=(\d+)")
JIT = re.compile(r"JITTER name=(\S+) period_us=(\d+) n=(\d+) min_us=(-?\d+) max_us=(-?\d+) p99_us=(\d+) "
                 r"missed=(\d+) viol=(\d+)")
LOAD = re.compile(r"LOAD pct=(\d+) min=(\d+) max=(\d+)")
CNT = re.compile(r"CNT rx_dropped=(\d+) imu_err=(\d+) btn=(\d+)")
STACK = re.compile(r"STACK used=(\d+) reserved=(\d+)")
GIT = re.compile(r"BOOT,fw=(\S+?),git=(\S+?),board")
EVT = re.compile(r"EVT t_us=(\d+) name=(\S+)")


def read_log(name):
    p = RUNS / f"{name}.log"
    return p.read_text(encoding="utf-8", errors="replace") if p.exists() else ""


def blocks(text):
    """Кожен вивід stats окремо: сценарій може містити кілька вимірювальних вікон."""
    out = []
    for chunk in text.split("STATS,BEGIN")[1:]:
        chunk = chunk.split("STATS,END")[0]
        b = {"stat": {}, "jit": {}}
        for m in STAT.finditer(chunk):
            b["stat"][m[1]] = {"n": int(m[2]), "min": int(m[3]), "avg": int(m[4]), "max": int(m[5])}
        for m in JIT.finditer(chunk):
            b["jit"][m[1]] = {"period": int(m[2]), "n": int(m[3]), "min": int(m[4]), "max": int(m[5]),
                              "p99": int(m[6]), "missed": int(m[7]), "viol": int(m[8])}
        m = LOAD.search(chunk)
        if m:
            b["load"] = {"last": int(m[1]), "min": int(m[2]), "max": int(m[3])}
        m = CNT.search(chunk)
        if m:
            b["cnt"] = {"rx_dropped": int(m[1]), "imu_err": int(m[2]), "btn": int(m[3])}
        out.append(b)
    return out


def summary(name):
    text = read_log(name)
    git = GIT.findall(text)
    st = STACK.findall(text)
    return {"git": git[-1][1] if git else None, "blocks": blocks(text),
            "stack_used": max((int(u) for u, _ in st), default=None)}


def spread(values):
    if not values:
        return None
    v = sorted(values)
    p99 = v[min(len(v) - 1, int(0.99 * len(v)))]
    return {"n": len(v), "min": v[0], "avg": round(statistics.mean(v), 1), "p99": p99, "max": v[-1]}


def edges(sig, level):
    return [t for t, v in sig if v == level]


def pulses(sig):
    """(фронт, ширина) кожного імпульсу високого рівня, нс."""
    out = []
    rise = None
    for t, v in sig:
        if v == 1:
            rise = t
        elif rise is not None:
            out.append((rise, t - rise))
            rise = None
    return out


def next_after(sorted_times, t, start=0):
    i = start
    while i < len(sorted_times) and sorted_times[i] < t:
        i += 1
    return (sorted_times[i], i) if i < len(sorted_times) else (None, i)


VCD_PREFIX = "lr3-"  # ЛР4 (--fw): cmp/<fw>-


def vcd_path(name):
    return CAP / f"{VCD_PREFIX}{name}.vcd"


def load_vcd(name):
    p = vcd_path(name)
    return ac.load_vcd(p) if p.exists() else None


def presses(sig, quiet_ns=20_000_000):
    """Перший спад кожного натискання: кнопка у Wokwi має брязкіт, спад рахується лише після тиші 20 мс."""
    out = []
    last = None
    for t, v in sig:
        if v == 0 and (last is None or t - last >= quiet_ns):
            out.append(t)
        last = t
    return out


def parts(name):
    """Сценарій, розбитий на сесії через ліміт CI (s3-p1…p3, run_scenarios.S3_PARTS), або він сам."""
    found = sorted(q.stem for q in RUNS.glob(f"{name}-p[0-9].log"))
    return found or [name]


def wire_button(name):
    """Перший спад BTN (PC13) -> фронт P_REACT: повна затримка реакції на дроті, мкс."""
    out = []
    for part in parts(name):
        sig = load_vcd(part)
        if not sig:
            return None
        react = edges(sig["P_REACT"], 1)
        j = 0
        for t in presses(sig["BTN"][1:]):
            r, j = next_after(react, t, j)
            if r is not None:
                out.append((r - t) / 1000)
    return spread(out)


def trimmed_pulses(sig, skip=40, gap_ns=80_000_000):
    """Імпульси вимірювального вікна: без старту і без усього після виводу stats (довга пауза в кінці)."""
    p = pulses(sig)[skip:]
    for i in range(len(p) - 1, 0, -1):
        if p[i][0] - p[i - 1][0] > gap_ns and i > len(p) * 0.9:
            return p[:i]
    return p


def wire_imu(name):
    width, per = [], []
    for part in parts(name):
        sig = load_vcd(part)
        if not sig:
            return None
        p = trimmed_pulses(sig["P_IMU"])
        width += [w / 1000 for _, w in p]
        per += [(b[0] - a[0]) / 1000 for a, b in zip(p, p[1:])]  # періоди лише всередині сесії
    return {"width": spread(width), "period": spread(per)}


def merge_blocks(bs):
    """Вимірювальні вікна кількох сесій як одне: n — сума, min/max — крайні, avg — зважене за n; для джитера
    p99 — найбільший із частин (оцінка зверху), пропуски й порушення — сума; LOAD — крайні; CNT — сума."""
    out = {"stat": {}, "jit": {}}
    for key in bs[0]["stat"]:
        xs = [b["stat"][key] for b in bs if key in b["stat"]]
        n = sum(x["n"] for x in xs)
        live = [x for x in xs if x["n"]]
        out["stat"][key] = {"n": n, "min": min((x["min"] for x in live), default=0),
                            "avg": round(sum(x["avg"] * x["n"] for x in live) / n) if n else 0,
                            "max": max((x["max"] for x in live), default=0)}
    for key in bs[0]["jit"]:
        xs = [b["jit"][key] for b in bs if key in b["jit"]]
        out["jit"][key] = {"period": xs[0]["period"], "n": sum(x["n"] for x in xs), "min": min(x["min"] for x in xs),
                           "max": max(x["max"] for x in xs), "p99": max(x["p99"] for x in xs),
                           "missed": sum(x["missed"] for x in xs), "viol": sum(x["viol"] for x in xs)}
    loads = [b["load"] for b in bs if "load" in b]
    if loads:
        out["load"] = {"last": loads[-1]["last"], "min": min(x["min"] for x in loads), "max": max(x["max"] for x in loads)}
    cnts = [b["cnt"] for b in bs if "cnt" in b]
    if cnts:
        out["cnt"] = {k: sum(c[k] for c in cnts) for k in cnts[0]}
    return out


def wire_e2e(name):
    """Ширина MARK: виявлення руху -> запис CCR; далі перший імпульс ШІМ зі зміненою шириною."""
    sig = load_vcd(name)
    if not sig:
        return None
    pp = pulses(sig["PWM_PAN"])
    marks = pulses(sig["MARK"])[1:]  # перший імпульс — до початку вимірювального вікна
    ccr = [w / 1000 for _, w in marks]
    out = []
    for t, _ in marks:
        for i in range(1, len(pp)):
            if pp[i][0] > t and abs(pp[i][1] - pp[i - 1][1]) > 2000:
                out.append((pp[i][0] - t) / 1000)
                break
    return {"to_ccr": spread(ccr), "to_pwm_output": spread(out)}


def wire_pwm(name):
    sig = load_vcd(name)
    if not sig:
        return None
    p = pulses(sig["PWM_PAN"])
    per = [(b[0] - a[0]) / 1000 for a, b in zip(p, p[1:])]
    widths = sorted({round(w / 1000) for _, w in p})
    return {"period": spread(per), "widths": widths}


def wire_rate(name):
    """Найбільша швидкість серво за шириною імпульсів (FR-05): °/с."""
    sig = load_vcd(name)
    if not sig:
        return None
    p = pulses(sig["PWM_PAN"])
    k = 180 / (2400 - 544)
    rates = []
    for a, b in zip(p, p[1:]):
        dt = (b[0] - a[0]) / 1e9
        dd = abs(b[1] - a[1]) / 1000 * k
        if dt > 0.015:
            rates.append(dd / dt)
    return round(max(rates), 1) if rates else None


def sigrok_widths(name, channel, edge, downsample=100):
    """Декодер timing (методичка §5.3): інтервали між фронтами каналу, мкс."""
    res = subprocess.run(["sigrok-cli", "-I", f"vcd:downsample={downsample}", "-i", str(vcd_path(name)),
                          "-P", f"timing:data={channel}:edge={edge}", "-A", "timing=time"],
                         capture_output=True, text=True)
    k = {"s": 1e6, "ms": 1e3, "μs": 1, "us": 1, "ns": 1e-3}
    return [float(v) * k[u] for v, u in re.findall(r"timing-1: ([\d.]+) (s|ms|μs|us|ns)", res.stdout)]


def evt_latencies(name, pairs):
    """Затримки за мітками часу МК у журналі EVT (методичка §5.2)."""
    events = [(int(t), n) for t, n in EVT.findall(read_log(name))]
    out = {}
    for a, b in pairs:
        d, t0 = [], None
        for t, n in events:
            if n == a:
                t0 = t
            elif n == b and t0 is not None:
                d.append(t - t0)
                t0 = None
        out[f"{a}->{b}"] = spread(d)
    return out


def methods_erase():
    """Одна метрика трьома способами: стирання сектора 4 КБ (модель: tSE = 45 мс)."""
    log = summary("sc1-erase")["blocks"]
    sig = load_vcd("sc1-erase")
    wire = [w / 1000 for _, w in pulses(sig["P_LONG"])] if sig else []
    wire = [w for w in wire if w > 20000]
    timing = [v for v in sigrok_widths("sc1-erase", "P_LONG", "any") if 40000 < v < 60000]
    return {"mcu_counter": log[0]["stat"]["flash_erase"] if log else None, "wire_python": spread(wire),
            "wire_sigrok_timing": spread(timing),
            "evt_log": evt_latencies("sc1-erase", [("erase_start", "erase_done")])}


def methods_button():
    log = summary("sc2-btn-idle")["blocks"]
    return {"mcu_counter": log[0]["stat"]["btn_latency"] if log else None, "wire": wire_button("sc2-btn-idle"),
            "evt_log": evt_latencies("fr06", [("btn_press", "mode")])}


def fr08(name):
    """Порівняння траєкторії запису і відтворення: тривалості з логу і вирівнювання за ШІМ.
    Якщо сценарій розбито на сесії (fr07-08-a: запис і відтворення, -b: запис і dump), береться A, потім B."""
    text = read_log(f"{name}-a") + read_log(f"{name}-b") or read_log(name)
    rec = re.search(r"REC,DONE,session=\d+,samples=(\d+),pages=(\d+),dur_us=(\d+)", text)
    play = re.search(r"PLAY,DONE,session=\d+,samples=(\d+),dur_us=(\d+)", text)
    dump = re.search(r"DUMP,END,session=\d+,samples=(\d+),pages=(\d+),crc=(\w+)", text)
    res = {}
    if rec and play:
        res.update(rec_samples=int(rec[1]), rec_pages=int(rec[2]), rec_dur_us=int(rec[3]),
                   play_samples=int(play[1]), play_dur_us=int(play[2]),
                   diff_ms=round((int(play[2]) - int(rec[3])) / 1000, 2))
    if dump:
        res.update(dump_samples=int(dump[1]), dump_pages=int(dump[2]), dump_crc=dump[3])
    return res


def wire_servo_motion(name):
    """FR-05: найбільша швидкість за один період ШІМ і час відпрацювання стрибка."""
    sig = load_vcd(name)
    if not sig:
        return None
    k = 180 / (2400 - 544)
    deg = [(t / 1e9, (w / 1000 - 544) * k) for t, w in pulses(sig["PWM_PAN"])]
    moves = [(a, b) for a, b in zip(deg, deg[1:]) if abs(b[1] - a[1]) > 1]
    seg, cur = [], [moves[0]]
    for x, y in zip(moves, moves[1:]):
        if y[0][0] - x[1][0] < 0.1:
            cur.append(y)
        else:
            seg.append(cur)
            cur = [y]
    seg.append(cur)
    last = seg[-1]
    span = abs(last[-1][1][1] - last[0][0][1])
    dur = last[-1][1][0] - last[0][0][0]
    return {"step_deg": round(span, 1), "step_s": round(dur, 3), "avg_dps": round(span / dur, 1)}


def fault_timing():
    """Від першої невдалої транзакції (P_IMU різко вужчає) до ERR,IMU lost."""
    sig = load_vcd("sc4-fault")
    if not sig:
        return None
    p = pulses(sig["P_IMU"])
    first = next((t for (t, w), (_, w0) in zip(p[1:], p) if w < 1_000_000 and w0 > 2_000_000), None)
    return {"first_fail_ms": round(first / 1e6, 1) if first else None}


def fr02():
    text = read_log("fr02")
    return [(int(p), int(r)) for p, r in re.findall(r"pitch_cdeg=(-?\d+),roll_cdeg=(-?\d+)", text)]


def fr05():
    text = read_log("fr05")
    return [int(x) for x in re.findall(r"pan_cdeg=(\d+)", text)]


def boot():
    text = read_log("sc7-boot")
    first = [int(x) for x in re.findall(r"BOOT,first_imu_us=(\d+)", text)]
    selftest = re.findall(r"ST,dev=MPU6050,res=(\w+)", text)
    return {"first_imu_us": first, "mpu_selftest": selftest,
            "restored_ms": [int(x) for x in re.findall(r"IMU,restored,t_ms=(\d+)", text)]}


def powerloss():
    text = read_log("sc8-powerloss")
    return {"traj": re.findall(r"TRAJ,session=(\d+),pages=(\d+),samples=(\d+),torn=(\d+)", text),
            "dump": re.findall(r"DUMP,END,session=\d+,samples=(\d+),pages=(\d+),crc=(\w+)", text),
            "play": re.findall(r"PLAY,DONE,session=\d+,samples=(\d+)", text)}


def fault():
    text = read_log("sc4-fault")
    return {"lost_ms": [int(x) for x in re.findall(r"ERR,IMU lost,t_ms=(\d+)", text)],
            "restored_ms": [int(x) for x in re.findall(r"IMU,restored,t_ms=(\d+)", text)],
            "fault_ms": [int(x) for x in re.findall(r"FAULT,t_ms=(\d+)", text)],
            "blocks": blocks(text), "evt": [(int(t), n) for t, n in EVT.findall(text)]}


def breakpoint_sweep():
    text = read_log("sc5-break")
    rows = []
    for chunk in text.split("RATE,hz=")[1:]:
        hz = int(chunk.split(",")[0])
        b = blocks(chunk)
        if b:
            rows.append({"hz": hz, "imu_runs": b[0]["jit"]["imu"]["n"], "viol": b[0]["jit"]["imu"]["viol"],
                         "missed": b[0]["jit"]["imu"]["missed"], "jit_max": b[0]["jit"]["imu"]["max"],
                         "servo_jit_max": b[0]["jit"]["servo"]["max"], "load": b[0].get("load", {}).get("max"),
                         "imu_avg": b[0]["stat"]["imu"]["avg"]})
    return rows


def long_summary():
    t = read_log("long")
    n = sum(int(x) for x in re.findall(r"JITTER name=imu.*? n=(\d+)", t.split("нове з'єднання")[0]))
    j = [int(x) for x in re.findall(r"JITTER name=imu.*?max_us=(\d+)", t.split("нове з'єднання")[0])]
    end = re.findall(r"DUMP,END,session=(\d+),samples=(\d+),pages=(\d+),crc=(\w+)", t)
    stack = sorted(set(re.findall(r"STACK used=(\d+)", t)))
    errors = len(re.findall(r"ERR,", t))
    return (f"{len(j)} вікон, {n} запусків IMU, джитер max {min(j)}…{max(j)} мкс, стек {stack} Б, "
            f"помилок: {errors}. Після скидання: {end[-1] if end else '—'}.")


def ms(us):
    return f"{us / 1000:.1f} мс" if abs(us) >= 1000 else f"{us} мкс"


def last(r, name):
    return r["runs"][name]["blocks"][-1]


def write_md(r):
    git = sorted({v["git"] for v in r["runs"].values() if v["git"]})
    s1, s2, s3 = (last(r, n) for n in ("s1", "s2", "s3"))
    o = ["# Результати вимірювань ЛР3", "",
         "Генерується `make metrics` (tools/analyze_metrics.py) з `docs/metrics/runs/*.log` і `docs/captures/lr3-*.vcd`.",
         "Усі числа отримано в симуляторі Wokwi (форма B). Які з них достовірні — розділ 3.5 звіту.", "",
         f"Збірка: `{', '.join(git)}`, 48 МГц, UART 115200, SPI 3 МГц, вимірювальний код увімкнено.", "",
         "## Навантаження S1–S3", "",
         "S1 — HOLD, 10 с. S2 — запис траєкторії 45 с, рух, `status` раз на 2 с. S3 — 40 циклів: `dump`, потік `status`, рух, кнопка.", "",
         "| Метрика | S1 | S2 | S3 | S3−S1 | Вимога | Спосіб |", "|---|---:|---:|---:|---:|---|---|"]

    def row(title, f, need, how, unit=ms):
        a, b, c = f(s1), f(s2), f(s3)
        fmt = lambda v: "—" if v is None else unit(v)  # noqa: E731
        delta = "—" if a is None or c is None else unit(c - a)
        o.append(f"| {title} | {fmt(a)} | {fmt(b)} | {fmt(c)} | {delta} | {need} | {how} |")

    def stat(name, key):
        return lambda b: b["stat"][name][key] if b["stat"][name]["n"] else None

    row("imu_task, avg", stat("imu", "avg"), "—", "лічильник")
    row("imu_task, max", stat("imu", "max"), "—", "лічильник")
    row("servo_task, max", stat("servo", "max"), "—", "лічильник")
    row("rec_task, max", stat("rec", "max"), "—", "лічильник")
    row("Період циклу, avg", stat("loop", "avg"), "—", "лічильник")
    row("Період циклу, max", stat("loop", "max"), "—", "лічильник")
    row("Джитер IMU, max", lambda b: b["jit"]["imu"]["max"], "≤ 0,5 мс (FR-01)", "лічильник")
    row("Джитер IMU, p99 (межа кошика)", lambda b: b["jit"]["imu"]["p99"], "—", "гістограма")
    row("Джитер серво, max", lambda b: b["jit"]["servo"]["max"], "—", "лічильник")
    row("Затримка кнопки, max", stat("btn_latency", "max"), "≤ 100 мс (tasks.md)", "лічильник + аналізатор")
    row("Команда → відповідь, max", stat("cmd_latency", "max"), "≤ 100 мс (tasks.md)", "лічильник")
    row("Рух → CCR, max", stat("e2e_latency", "max"), "≤ 50 мс (FR-03)", "лічильник")
    n = str
    row("Пропущені запуски IMU", lambda b: b["jit"]["imu"]["missed"], "0", "лічильник", n)
    row("Порушення ±0,5 мс", lambda b: b["jit"]["imu"]["viol"], "0", "лічильник", n)
    row("Запусків IMU", lambda b: b["jit"]["imu"]["n"], "≥ 1000", "лічильник", n)
    row("Завантаження CPU, max", lambda b: b["load"]["max"], "—", "лічильник", lambda v: f"{v} %")
    o += ["", f"Глибина стека за stack painting: S1 {r['runs']['s1']['stack_used']} Б, S2 {r['runs']['s2']['stack_used']} Б, "
          f"S3 {r['runs']['s3']['stack_used']} Б (максимум припадає на самотест при старті). Heap: не використовується.", "",
          "## Сценарії 6.2", "",
          "| Сценарій | Джитер IMU, max | Пропуски | Завантаження | Головне |", "|---|---:|---:|---:|---|"]
    for name, title, extra in (
        ("s1", "S1, контроль", "—"),
        ("sc1-erase", "6.2.1 стирання 16 секторів", f"блокування {ms(last(r, 'sc1-erase')['stat']['cli']['max'])}"),
        ("sc1-dump", "6.2.1 dump 500 семплів", f"блокування {ms(last(r, 'sc1-dump')['stat']['cli']['max'])}"),
        ("sc2-btn-idle", "6.2.2 кнопка, контроль", f"затримка max {ms(last(r, 'sc2-btn-idle')['stat']['btn_latency']['max'])}"),
        ("sc2-btn-long", "6.2.2 кнопка під час dump", f"затримка max {ms(last(r, 'sc2-btn-long')['stat']['btn_latency']['max'])}"),
        ("sc3-flood", "6.2.3 потік команд", f"втрачено {last(r, 'sc3-flood')['cnt']['rx_dropped']} байт, "
                                            f"команда → відповідь max {ms(last(r, 'sc3-flood')['stat']['cmd_latency']['max'])}"),
    ):
        b = last(r, name)
        j = b["jit"]["imu"]
        o.append(f"| {title} | {ms(j['max'])} | {j['missed']} | {b['load']['max']} % | {extra} |")
    fb = r["fault"]["blocks"]
    o += ["", "## Відмови (6.2.4, `sc4-fault`)", "", "| Стан | I²C, avg | I²C, max | imu_task, max | Джитер IMU, max | Завантаження |",
          "|---|---:|---:|---:|---:|---:|"]
    for title, b in zip(("Норма", "Сенсор від'єднано", "SDA на землі"), fb):
        o.append(f"| {title} | {ms(b['stat']['i2c_read']['avg'])} | {ms(b['stat']['i2c_read']['max'])} | "
                 f"{ms(b['stat']['imu']['max'])} | {ms(b['jit']['imu']['max'])} | {b['load']['max']} % |")
    o += ["", f"Перша невдала транзакція {r['wire']['fault']['first_fail_ms']} мс, `ERR,IMU lost` {r['fault']['lost_ms'][0]} мс. "
          f"Відновлення: `IMU,restored` на {r['fault']['restored_ms']} мс.", "",
          "## Точка зламу (6.2.5, `sc5-break`)", "", "| Задано, Гц | Досягнуто, Гц | Порушення ±0,5 мс | Пропуски | Джитер max | Завантаження |",
          "|---:|---:|---:|---:|---:|---:|"]
    for x in r["sweep"]:
        o.append(f"| {x['hz']} | {x['imu_runs'] / 3:.0f} | {x['viol']} | {x['missed']} | {ms(x['jit_max'])} | {x['load']} % |")
    bt = r["boot"]
    pl = r["powerloss"]
    o += ["", "## Старт і обрив живлення", "",
          f"Від скидання до першого виміру IMU: {bt['first_imu_us'][0] / 1000:.1f} мс, із них самотест ≈ 256 мс. "
          f"Сенсор під'єднано через 300 мс після READY: {bt['first_imu_us'][1] / 1000:.1f} мс. Через 1000 мс: {bt['first_imu_us'][2] / 1000:.1f} мс.", "",
          f"Обрив запису сторінки: після старту `TRAJ,session={pl['traj'][-1][0]},pages={pl['traj'][-1][1]},samples={pl['traj'][-1][2]},torn={pl['traj'][-1][3]}`. "
          f"`dump` дав {pl['dump'][-1][0]} семплів, відтворення {pl['play'][-1]}.", "",
          "## Тривала робота (6.2.6, `long`)", "",
          long_summary(), "",
          "## Одна метрика різними способами", "", "| Метрика | Лічильник МК | Журнал EVT | Аналізатор, Python | Аналізатор, декодер timing |",
          "|---|---:|---:|---:|---:|"]
    e = r["methods"]["erase"]
    b = r["methods"]["button"]
    o.append(f"| Стирання сектора, avg (n = 64) | {e['mcu_counter']['avg']} мкс | {e['evt_log']['erase_start->erase_done']['avg']} мкс | "
             f"{e['wire_python']['avg']} мкс | {e['wire_sigrok_timing']['avg']} мкс |")
    o.append(f"| Затримка кнопки, avg (n = 50) | {b['mcu_counter']['avg']} мкс | — | {b['wire']['avg']} мкс | — |")
    o.append(f"| Затримка кнопки, max | {b['mcu_counter']['max']} мкс | — | {b['wire']['max']} мкс | — |")
    w = r["wire"]
    o += ["", "## Вимоги PRD за окремими прогонами", "",
          f"- FR-01, період IMU за P_IMU: S1 avg {w['imu_s1']['period']['avg']} мкс, min {w['imu_s1']['period']['min']}, max {w['imu_s1']['period']['max']}.",
          f"- FR-02, нахил 15/30/45°: pitch {[p / 100 for p, _ in r['fr02'][2::2]]}, roll {[q / 100 for _, q in r['fr02'][3::2]]}.",
          f"- FR-03: рух → CCR max {last(r, 'fr03')['stat']['e2e_latency']['max'] / 1000:.1f} мс (лічильник), {w['e2e']['to_ccr']['max'] / 1000:.1f} мс (MARK); "
          f"рух → змінений імпульс на виводі max {w['e2e']['to_pwm_output']['max'] / 1000:.1f} мс.",
          f"- FR-04: період ШІМ avg {w['pwm']['period']['avg']} мкс, ширини імпульсів {w['pwm']['widths']} мкс.",
          f"- FR-05: тремтіння ±1° — серво не рухались ({r['fr05'][0]} → {r['fr05'][1]} сотих °). Стрибок {w['servo_motion']['step_deg']}° за {w['servo_motion']['step_s']} с; "
          f"найбільша швидкість за один період ШІМ {w['rate_dps']} °/с.",
          f"- FR-06: {last(r, 'fr06')['cnt']['btn']} натискань, стільки ж реакцій.",
          f"- FR-07: {r['fr08']['rec_samples']} семплів, {r['fr08']['rec_pages']} сторінок, CRC {r['fr08']['dump_crc']}.",
          f"- FR-08: запис {r['fr08']['rec_dur_us'] / 1e6:.3f} с, відтворення {r['fr08']['play_dur_us'] / 1e6:.3f} с, різниця {r['fr08']['diff_ms']} мс.", ""]
    (OUT / "results.md").write_text("\n".join(o), encoding="utf-8")


def safe(fn, *args):
    """ЛР4: частина прогонів може бути відсутня або обірвана (напр. sc5-break на FreeRTOS) — не падати."""
    try:
        return fn(*args)
    except (KeyError, IndexError, TypeError, ZeroDivisionError, ValueError, StopIteration):
        return None


def main_fw(fw):
    global RUNS, VCD_PREFIX
    RUNS = OUT / f"runs-{fw}"
    VCD_PREFIX = f"cmp/{fw}-"
    names = sorted(p.stem for p in RUNS.glob("*.log"))
    r = {"fw": fw, "runs": {n: summary(n) for n in names}}
    for base in ("s3", "s3-tick100"):  # сесії через ліміт CI зливаються в один сценарій
        ps = parts(base)
        if ps != [base]:
            sums = [r["runs"][q] for q in ps]
            r["runs"][base] = {"git": sums[0]["git"], "parts": ps,
                               "blocks": [merge_blocks([q["blocks"][-1] for q in sums])],
                               "stack_used": max((q["stack_used"] or 0 for q in sums), default=None)}
    r["wire"] = {
        "button_idle": safe(wire_button, "sc2-btn-idle"), "button_long": safe(wire_button, "sc2-btn-long"),
        "button_s3": safe(wire_button, "s3"), "imu_s1": safe(wire_imu, "s1"), "imu_s2": safe(wire_imu, "s2"),
        "imu_s3": safe(wire_imu, "s3"), "e2e": safe(wire_e2e, "fr03"), "pwm": safe(wire_pwm, "fr04"),
        "rate_dps": safe(wire_rate, "fr05"), "servo_motion": safe(wire_servo_motion, "fr05"),
        "fault": safe(fault_timing),
    }
    r["methods"] = {"erase": safe(methods_erase), "button": safe(methods_button)}
    for key, fn, args in (("fr02", fr02, ()), ("fr05", fr05, ()), ("fr08", fr08, ("fr07-08",)), ("boot", boot, ()),
                          ("powerloss", powerloss, ()), ("fault", fault, ()), ("sweep", breakpoint_sweep, ())):
        r[key] = safe(fn, *args)
    (OUT / f"results-{fw}.json").write_text(json.dumps(r, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"written results-{fw}.json: {len(names)} runs")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", choices=("lr3", "lr4"))
    a = ap.parse_args()
    if a.fw:
        main_fw(a.fw)
        return
    names = sorted(p.stem for p in RUNS.glob("*.log"))
    r = {"runs": {n: summary(n) for n in names}}
    r["perfcal"] = re.findall(r"PERFCAL loop500_cyc=(\d+)|read_cyc=(\d+) probe_pair_cyc=(\d+) line40_us=(\d+) "
                              r"backward=(\d+)", read_log("s1"))
    r["wire"] = {
        "button_idle": wire_button("sc2-btn-idle"), "button_long": wire_button("sc2-btn-long"),
        "button_s3": wire_button("s3"), "imu_s1": wire_imu("s1"), "imu_s2": wire_imu("s2"), "imu_s3": wire_imu("s3"),
        "e2e": wire_e2e("fr03"), "pwm": wire_pwm("fr04"), "rate_dps": wire_rate("fr05"),
        "servo_motion": wire_servo_motion("fr05"), "fault": fault_timing(),
    }
    r["methods"] = {"erase": methods_erase(), "button": methods_button()}
    r["fr02"] = fr02()
    r["fr05"] = fr05()
    r["fr08"] = fr08("fr07-08")
    r["boot"] = boot()
    r["powerloss"] = powerloss()
    r["fault"] = fault()
    r["sweep"] = breakpoint_sweep()
    (OUT / "results.json").write_text(json.dumps(r, ensure_ascii=False, indent=1), encoding="utf-8")
    write_md(r)
    print(json.dumps({k: r[k] for k in ("wire", "fr08", "boot", "powerloss", "sweep")}, ensure_ascii=False)[:3000])


if __name__ == "__main__":
    main()
