#!/usr/bin/env python3
"""
Готує записи для перегляду в PulseView:
  docs/captures/pulseview/<name>.vcd — вікно навколо транзакції (за фронтами MARK), час від 0;
  docs/captures/pulseview/<name>.pvs — сесія PulseView з налаштованими декодерами.

Відкрити:  pulseview -i <name>.vcd -I vcd:downsample=<N> -s <name>.pvs
(команди для кожного запису — у docs/captures/pulseview/README.md)
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import analyze_captures as ac  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAP = ROOT / "docs" / "captures"
OUT = CAP / "pulseview"

# decoder id -> упорядкований список (ім'я каналу декодера, сигнал у VCD)
VIEWS = {
    "i2c-whoami-burst": {
        "src": "i2c-mpu6050-whoami-burst", "marks": (-2, -1), "pad_us": 30, "ds": 50,
        "decoders": [("i2c", [("scl", "SCL"), ("sda", "SDA")])],
    },
    "i2c-whoami-only": {
        "src": "i2c-mpu6050-whoami-burst", "marks": (-2, -2), "pad_us": 20, "ds": 10,
        "decoders": [("i2c", [("scl", "SCL"), ("sda", "SDA")])],
    },
    "spi-jedec": {
        "src": "spi-w25q64-jedec-wren-program-read", "marks": (-3, -3), "pad_us": 10, "ds": 5,
        "decoders": [("spi", [("clk", "SCK"), ("miso", "MISO"), ("mosi", "MOSI"), ("cs", "CS")])],
    },
    "spi-wren-program-busy": {
        "src": "spi-w25q64-jedec-wren-program-read", "marks": (-2, -2), "pad_us": 20, "ds": 10,
        "decoders": [("spi", [("clk", "SCK"), ("miso", "MISO"), ("mosi", "MOSI"), ("cs", "CS")])],
    },
    "pwm-3-angles": {
        "src": "pwm-servo-pan-tilt-3-angles", "marks": (-3, -1), "pad_us": 110000, "ds": 1000,
        "decoders": [("pwm", [("data", "PWM_PAN")]), ("pwm", [("data", "PWM_TILT")])],
    },
    "uart-payload": {
        "src": "uart-vcp-and-ft232rl", "marks": (-1, -1), "pad_us": 3800, "ds": 100,
        "decoders": [("uart", [("rx", "VCP_TX")]), ("uart", [("rx", "FT_RXD")])],
    },
    "i2c-anomaly": {
        "src": "i2c-anomaly-nack-restart-hang", "marks": (-3, -1), "pad_us": 30, "ds": 50,
        "decoders": [("i2c", [("scl", "SCL"), ("sda", "SDA")])],
    },
}

# Порядок каналів декодерів sigrok (обов'язкові, потім опційні)
DEC_CHANNELS = {
    "i2c": ["scl", "sda"],
    "spi": ["clk", "miso", "mosi", "cs"],
    "uart": ["rx", "tx"],
    "pwm": ["data"],
}


def window(src, marks, pad_ns):
    sig = ac.load_vcd(CAP / f"{src}.vcd")
    win = ac.mark_windows(sig)
    a, b = win[marks[0]], win[marks[1]]
    t0 = max(0, a[0] - pad_ns)
    t1 = (b[1] or a[0]) + pad_ns
    return sig, t0, t1


def write_vcd(path, sig, t0, t1):
    names = list(sig)
    ids = {n: chr(33 + i) for i, n in enumerate(names)}
    lines = ["$timescale 1ns $end", "$scope module logic $end"]
    lines += [f"$var wire 1 {ids[n]} {n} $end" for n in names]
    lines += ["$upscope $end", "$enddefinitions $end", "#0"]
    lines += [f"{ac.level_at(sig[n], t0)}{ids[n]}" for n in names]
    events = sorted((t, n, v) for n in names for t, v in sig[n] if t0 < t <= t1)
    cur = None
    for t, n, v in events:
        if t != cur:
            lines.append(f"#{t - t0}")
            cur = t
        lines.append(f"{v}{ids[n]}")
    lines.append(f"#{t1 - t0}")
    path.write_text("\n".join(lines) + "\n")


def write_pvs(path, decoders):
    out = ["[General]", f"decode_signals={len(decoders)}", "views=1", "meta_objs=0", ""]
    for i, (dec, chans) in enumerate(decoders):
        g = f"decode_signal{i}"
        assigned = dict(chans)
        title = dec.upper() + (" " + chans[0][1] if dec in ("pwm", "uart") else "")
        out += [f"[{g}]", f"name={title}", "enabled=true", "decoders=1",
                f"channels={len(DEC_CHANNELS[dec])}", ""]
        out += [f"[{g}/decoder0]", f"id={dec}", "visible=true", "options=0", ""]
        for k, ch in enumerate(DEC_CHANNELS[dec]):
            out += [f"[{g}/channel{k}]", f"name={ch}", "initial_pin_state=2"]
            if ch in assigned:
                out.append(f"assigned_signal_name={assigned[ch]}")
            out.append("")
    path.write_text("\n".join(out))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    readme = ["# Записи для PulseView", "",
              "Кожен файл — вікно навколо транзакції з відповідного docs/captures/*.vcd (час від 0).",
              "Відкрити з уже налаштованими декодерами:", ""]
    for name, v in VIEWS.items():
        sig, t0, t1 = window(v["src"], v["marks"], v["pad_us"] * 1000)
        write_vcd(OUT / f"{name}.vcd", sig, t0, t1)
        write_pvs(OUT / f"{name}.pvs", v["decoders"])
        sr = 1000 // v["ds"]
        readme.append(f"- **{name}** (з `{v['src']}.vcd`, {t0} … {t1} нс, {sr} MS/s):")
        readme.append(f"  `pulseview -i {name}.vcd -I vcd:downsample={v['ds']} -s {name}.pvs`")
        print(f"{name}: {(t1 - t0) / 1e3:.1f} us window, samplerate {sr} MS/s")
    (OUT / "README.md").write_text("\n".join(readme) + "\n")


if __name__ == "__main__":
    main()
