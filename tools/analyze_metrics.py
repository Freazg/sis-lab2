#!/usr/bin/env python3
"""
Зведення результатів ЛР3 (методичка §6.3): docs/metrics/runs/*.log і docs/captures/lr3-*.vcd ->
docs/metrics/results.json, docs/metrics/results.md.

Числа з логів — час МК (мітки всередині прошивки). Числа з VCD — час на дроті: незалежний спосіб
вимірювання того самого (логічний аналізатор Wokwi, щупи на службових GPIO).
"""
import json
import pathlib
import re
import statistics
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


def load_vcd(name):
    p = CAP / f"lr3-{name}.vcd"
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


def wire_button(name):
    """Перший спад BTN (PC13) -> фронт P_REACT: повна затримка реакції на дроті, мкс."""
    sig = load_vcd(name)
    if not sig:
        return None
    react = edges(sig["P_REACT"], 1)
    out = []
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
    sig = load_vcd(name)
    if not sig:
        return None
    p = trimmed_pulses(sig["P_IMU"])
    width = [w / 1000 for _, w in p]
    per = [(b[0] - a[0]) / 1000 for a, b in zip(p, p[1:])]
    return {"width": spread(width), "period": spread(per)}


def wire_e2e(name):
    """Ширина MARK: виявлення руху -> запис CCR; далі перший імпульс ШІМ зі зміненою шириною."""
    sig = load_vcd(name)
    if not sig:
        return None
    pp = pulses(sig["PWM_PAN"])
    marks = pulses(sig["MARK"])
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


def fr08(name):
    """Порівняння траєкторії запису і відтворення: тривалості з логу і вирівнювання за ШІМ."""
    text = read_log(name)
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


def main():
    names = sorted(p.stem for p in RUNS.glob("*.log"))
    r = {"runs": {n: summary(n) for n in names}}
    r["perfcal"] = re.findall(r"PERFCAL loop500_cyc=(\d+)|read_cyc=(\d+) probe_pair_cyc=(\d+) line40_us=(\d+) "
                              r"backward=(\d+)", read_log("s1"))
    r["wire"] = {
        "button_idle": wire_button("sc2-btn-idle"), "button_long": wire_button("sc2-btn-long"),
        "button_s3": wire_button("s3"), "imu_s1": wire_imu("s1"), "imu_s2": wire_imu("s2"), "imu_s3": wire_imu("s3"),
        "e2e": wire_e2e("fr03"), "pwm": wire_pwm("fr04"), "rate_dps": wire_rate("fr05"),
    }
    r["fr02"] = fr02()
    r["fr05"] = fr05()
    r["fr08"] = fr08("fr07-08")
    r["boot"] = boot()
    r["powerloss"] = powerloss()
    r["fault"] = fault()
    r["sweep"] = breakpoint_sweep()
    (OUT / "results.json").write_text(json.dumps(r, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps({k: r[k] for k in ("wire", "fr08", "boot", "powerloss", "sweep")}, ensure_ascii=False)[:3000])


if __name__ == "__main__":
    main()
