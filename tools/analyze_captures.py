#!/usr/bin/env python3
"""
Вимірювання часових параметрів безпосередньо з VCD-записів (docs/captures/*.vcd):
частота SCL/SCK, tLOW/tHIGH, START/STOP-таймінги, тривалість транзакцій і проміжки між
ними, затримка «подія в прошивці (фронт MARK) -> реакція на шині», період і ширина ШІМ,
фактичний бодрейт UART. Результат: docs/captures/measurements.md (+ .json).

VCD від Wokwi містить моменти зміни рівня з роздільністю 1 нс, тож вимірювання
не обмежені частотою дискретизації (на відміну від реального аналізатора).
"""
import json
import pathlib
import statistics as st

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAP = ROOT / "docs" / "captures"


def load_vcd(path):
    ids, sig, t = {}, {}, 0
    for line in path.read_text().splitlines():
        line = line.strip()
        if line.startswith("$var"):
            p = line.split()
            ids[p[3]] = p[4]
            sig[p[4]] = []
        elif line.startswith("#"):
            t = int(line[1:])
        elif line[:1] in "01" and line[1:] in ids:
            name = ids[line[1:]]
            v = int(line[0])
            if not sig[name] or sig[name][-1][1] != v:
                sig[name].append((t, v))
    return sig


def level_at(ch, t):
    v = 1
    for tt, vv in ch:
        if tt > t:
            break
        v = vv
    return v


def edges(ch, v, t0=0, t1=float("inf")):
    return [t for t, x in ch if x == v and t0 <= t <= t1]


def i2c_conditions(sig, t0=0, t1=float("inf")):
    """START: SDA 1->0 при SCL=1; STOP: SDA 0->1 при SCL=1."""
    scl, sda = sig["SCL"], sig["SDA"]
    out = []
    for t, v in sda:
        if t0 <= t <= t1 and level_at(scl, t) == 1:
            out.append((t, "START" if v == 0 else "STOP"))
    return out


def ns(x):
    return round(x)


def i2c_metrics(sig, t0, t1):
    scl = sig["SCL"]
    rise = edges(scl, 1, t0, t1)
    fall = edges(scl, 0, t0, t1)
    periods = [b - a for a, b in zip(rise, rise[1:]) if b - a < 20000]
    highs, lows = [], []
    for r in rise:
        f = next((x for x in fall if x > r), None)
        if f and f - r < 20000:
            highs.append(f - r)
    for f in fall:
        r = next((x for x in rise if x > f), None)
        if r and r - f < 20000:
            lows.append(r - f)
    conds = i2c_conditions(sig, t0, t1)
    txns = []
    start = None
    for t, c in conds:
        if c == "START":
            start = t
        elif c == "STOP" and start is not None:
            txns.append((start, t))
            start = None
    # tHD;STA: START -> перший спад SCL; tSU;STO: останній фронт SCL -> STOP
    hd_sta = [min(x for x in fall if x > s) - s for s, _ in txns if any(x > s for x in fall)]
    su_sto = [e - max(x for x in rise if x < e) for _, e in txns if any(x < e for x in rise)]
    gaps = [b[0] - a[1] for a, b in zip(txns, txns[1:])]
    return {
        "scl_period_ns": {"mean": ns(st.mean(periods)), "min": min(periods), "max": max(periods)},
        "scl_freq_khz": round(1e6 / st.mean(periods), 1),
        "t_high_ns": {"mean": ns(st.mean(highs)), "min": min(highs)},
        "t_low_ns": {"mean": ns(st.mean(lows)), "min": min(lows)},
        "t_hd_sta_ns": min(hd_sta) if hd_sta else None,
        "t_su_sto_ns": min(su_sto) if su_sto else None,
        "transactions": [{"start_ns": a, "dur_ns": b - a} for a, b in txns],
        "gaps_ns": gaps,
        "t_buf_min_ns": min(gaps) if gaps else None,
    }


def mark_windows(sig):
    m = sig.get("MARK", [])
    rises = [t for t, v in m if v == 1]
    falls = [t for t, v in m if v == 0]
    out = []
    for r in rises:
        f = next((x for x in falls if x > r), None)
        out.append((r, f))
    return out


def analyze_i2c():
    sig = load_vcd(CAP / "i2c-mpu6050-whoami-burst.vcd")
    win = mark_windows(sig)
    # два останні вікна MARK — WHO_AM_I і пакетне читання з команди cap i2c
    (w1s, w1e), (w2s, w2e) = win[-2], win[-1]
    m = i2c_metrics(sig, w1s, w2e)
    first_start = next(t for t, c in i2c_conditions(sig, w1s, w1e) if c == "START")
    m["mark_to_start_ns"] = first_start - w1s
    m["whoami_window_ns"] = w1e - w1s
    m["burst_window_ns"] = w2e - w2s
    # фонове опитування 50 Гц у LIVE до команди mode hold: інтервал між пакетними читаннями
    live = [t["start_ns"] for t in i2c_metrics(sig, 0, w1s)["transactions"] if t["dur_ns"] > 1_000_000]
    bursts = [b - a for a, b in zip(live, live[1:])]
    m["live_poll_period_ns"] = ns(st.median(bursts)) if bursts else None
    m["live_poll_count"] = len(live)
    return m


def analyze_spi():
    sig = load_vcd(CAP / "spi-w25q64-jedec-wren-program-read.vcd")
    win = mark_windows(sig)[-3:]  # JEDEC, PROGRAM(WREN..BUSY), READ
    sck, cs = sig["SCK"], sig["CS"]
    rise = edges(sck, 1, win[0][0], win[-1][1])
    periods = [b - a for a, b in zip(rise, rise[1:]) if b - a < 1000]
    cs_low = []
    for t, v in cs:
        if v == 0 and win[0][0] <= t <= win[-1][1]:
            e = next(x for x, y in cs if y == 1 and x > t)
            cs_low.append((t, e - t))
    # проміжки між байтами всередині однієї транзакції (накладні витрати HAL)
    byte_gaps = []
    for t, dur in cs_low:
        r = edges(sck, 1, t, t + dur)
        byte_gaps += [b - a for a, b in zip(r, r[1:]) if b - a >= 1000]
    prog_s, prog_e = win[1]
    txn_in_prog = [(t, d) for t, d in cs_low if prog_s <= t <= prog_e]
    first_cs = next(t for t, _ in cs_low if t >= win[0][0])
    return {
        "sck_period_ns": {"mean": ns(st.mean(periods)), "min": min(periods), "max": max(periods)},
        "sck_freq_mhz": round(1e3 / st.mean(periods), 3),
        "cs_low_ns": [{"start_ns": t, "dur_ns": d} for t, d in cs_low],
        "inter_byte_gap_ns": {"mean": ns(st.mean(byte_gaps)), "min": min(byte_gaps), "max": max(byte_gaps)},
        "mark_to_cs_ns": first_cs - win[0][0],
        "program_window_ns": prog_e - prog_s,
        "program_transactions": len(txn_in_prog),
        "busy_poll_count": max(0, len(txn_in_prog) - 3),
    }


def analyze_pwm():
    sig = load_vcd(CAP / "pwm-servo-pan-tilt-3-angles.vcd")
    res = {}
    win = mark_windows(sig)[-3:]
    for ch in ("PWM_PAN", "PWM_TILT"):
        s = sig[ch]
        rises = edges(s, 1)
        segs = []
        for i, (ws, _) in enumerate(win):
            we = win[i + 1][0] if i + 1 < len(win) else ws + 100_000_000
            rr = [r for r in rises if ws + 25_000_000 <= r <= we]  # пропустити перший період після зміни
            widths = [next(t for t, v in s if v == 0 and t > r) - r for r in rr[:-1]]
            per = [b - a for a, b in zip(rr, rr[1:])]
            segs.append({"pulses": len(widths), "width_ns": sorted(set(widths)), "period_ns": sorted(set(per))})
        res[ch] = segs
    return res


def analyze_uart():
    sig = load_vcd(CAP / "uart-vcp-and-ft232rl.vcd")
    out = {}
    for ch in ("VCP_TX", "FT_RXD"):
        e = [t for t, _ in sig[ch]]
        d = [b - a for a, b in zip(e, e[1:])]
        bit = min(x for x in d if x > 1000)
        # найкоротші інтервали = 1 біт; усереднення по всіх інтервалах ≈ 1..1.5 біта
        singles = [x for x in d if x < bit * 1.5]
        out[ch] = {"bit_ns": ns(st.mean(singles)), "baud": round(1e9 / st.mean(singles))}
    return out


def analyze_anomaly():
    sig = load_vcd(CAP / "i2c-anomaly-nack-restart-hang.vcd")
    win = mark_windows(sig)[-3:]
    conds = i2c_conditions(sig, win[0][0], sig["SCL"][-1][0] + 1)
    scl_last = sig["SCL"][-1][0]
    hang_start = win[1][0]
    scl_in_hang = edges(sig["SCL"], 0, hang_start, win[2][0])
    return {
        "conditions": [{"t_ns": t, "cond": c} for t, c in conds][:20],
        "mem_read_window_ns": (win[1][1] or scl_last) - win[1][0],
        "scl_edges_during_mem_read": len(scl_in_hang),
        "recover_mark_ns": win[2][0],
    }


def fmt_us(x):
    return f"{x / 1000:.3f} мкс"


def analyze_reference():
    sig = load_vcd(CAP / "i2c-reference-timingr-100k-lr1-firmware.vcd")
    return i2c_metrics(sig, 0, float("inf"))


def main():
    data = {"i2c_ref_100k": analyze_reference(), "i2c": analyze_i2c(), "spi": analyze_spi(), "pwm": analyze_pwm(), "uart": analyze_uart(),
            "anomaly": analyze_anomaly()}
    (CAP / "measurements.json").write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n")
    i, s, u = data["i2c"], data["spi"], data["uart"]
    md = ["# Виміряні параметри (згенеровано tools/analyze_captures.py)", "",
          "| Параметр | Виміряно | Розрахунок / вимога | Джерело |", "|---|---|---|---|",
          f"| I2C: період SCL (середній, min..max) | {fmt_us(i['scl_period_ns']['mean'])} "
          f"({i['scl_period_ns']['min']}..{i['scl_period_ns']['max']} нс) → **{i['scl_freq_khz']} кГц** | "
          "прошивка: ~2.5 мкс → 400 кГц (TIMINGR 0x50330309); **модель Wokwi ігнорує TIMINGR** — див. A4 | RM0490 Table 115 |",
          f"| I2C (контроль A4): той самий вимір для TIMINGR 0xB0420F13 (100 кГц, прошивка ЛР1) | "
          f"{fmt_us(data['i2c_ref_100k']['scl_period_ns']['mean'])}, tLOW {fmt_us(data['i2c_ref_100k']['t_low_ns']['min'])}, "
          f"tHIGH {fmt_us(data['i2c_ref_100k']['t_high_ns']['min'])} | на реальному МК: tLOW 5.0 мкс, tHIGH 4.0 мкс + tSYNC | RM0490 Table 115 |",
          f"| I2C: tLOW (min) | {fmt_us(i['t_low_ns']['min'])} | ≥ 1.3 мкс (Fm); задано 10×125 нс = 1.25 мкс + синхронізація | RM0490 Table 112, 115 |",
          f"| I2C: tHIGH (min) | {fmt_us(i['t_high_ns']['min'])} | ≥ 0.6 мкс (Fm); задано 4×125 нс = 0.5 мкс + tSYNC | RM0490 Table 112, 115 |",
          f"| I2C: tHD;STA (min) | {fmt_us(i['t_hd_sta_ns'])} | ≥ 0.6 мкс | RM0490 Table 112; MPU-6050 PS §6.7 |",
          f"| I2C: tSU;STO (min) | {fmt_us(i['t_su_sto_ns'])} | ≥ 0.6 мкс | RM0490 Table 112 |",
          f"| I2C: tBUF між транзакціями (min) | {fmt_us(i['t_buf_min_ns'])} | ≥ 1.3 мкс | RM0490 Table 112 |",
          f"| I2C: MARK↑ → START | {fmt_us(i['mark_to_start_ns'])} | — (накладні витрати драйвера+Platform) | вимір |",
          f"| I2C: WHO_AM_I (2 транзакції, STOP між ними) | {fmt_us(i['whoami_window_ns'])} | 2×(9+9 біт)·10 мкс = 360 мкс + накладні (на залізі при 400 кГц ≈ 90 мкс) | розрахунок |",
          f"| I2C: пакетне читання 14 байт | {fmt_us(i['burst_window_ns'])} | (9+9)+(9+14·9) = 153 біт ·10 мкс = 1530 мкс + накладні (на залізі при 400 кГц ≈ 383 мкс) | розрахунок |",
          f"| I2C: проміжок між записом номера регістра (STOP) і читанням (START) | {fmt_us(i['gaps_ns'][0])} | ≥ tBUF 1.3 мкс; STOP замість repeated START — див. A2 | RM0490 Table 112 |",
          f"| SPI: період SCK → частота | {fmt_us(s['sck_period_ns']['mean'])} → **{s['sck_freq_mhz']} МГц** | 48 МГц / 16 = 3 МГц | RM0490 SPI_CR1.BR p.877 |",
          f"| SPI: проміжок між байтами в транзакції | {fmt_us(s['inter_byte_gap_ns']['mean'])} (min {fmt_us(s['inter_byte_gap_ns']['min'])}) | — (HAL polling) | вимір |",
          f"| SPI: MARK↑ → CS#↓ | {fmt_us(s['mark_to_cs_ns'])} | — | вимір |",
          f"| SPI: Page Program (WREN…BUSY=0) | {fmt_us(s['program_window_ns'])}, транзакцій {s['program_transactions']}, опитувань BUSY {s['busy_poll_count']} | tPP typ 0.4 мс / max 3 мс | W25Q64JV §9.6 p.64 |",
          f"| UART VCP: тривалість біта → бод | {fmt_us(u['VCP_TX']['bit_ns'])} → **{u['VCP_TX']['baud']}** | 48e6/417 = 115108 бод (−0.08 %) | RM0490 §26.5.7 |",
          f"| UART FT232RL: тривалість біта → бод | {fmt_us(u['FT_RXD']['bit_ns'])} → **{u['FT_RXD']['baud']}** | 115108 бод | RM0490 §26.5.7 |",
          ]
    for ch, segs in data["pwm"].items():
        for k, seg in enumerate(segs):
            md.append(f"| PWM {ch}, крок {k + 1}: ширина / період | {', '.join(fmt_us(w) for w in seg['width_ns'])} / "
                      f"{', '.join(fmt_us(p) for p in seg['period_ns'])} | див. CAP,pwm у UART-лозі; період 20 мс | TIM3 PSC=47, ARR=19999 |")
    a = data["anomaly"]
    md += ["", f"Аномалія A2: під час HAL_I2C_Mem_Read вікно MARK = {fmt_us(a['mem_read_window_ns'])}, "
           f"спадів SCL у цьому вікні: {a['scl_edges_during_mem_read']}.", ""]
    (CAP / "measurements.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))


if __name__ == "__main__":
    main()
