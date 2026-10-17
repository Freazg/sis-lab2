#!/usr/bin/env python3
"""
Анотовані осцилограми (SVG) з VCD-записів + анотацій sigrok-cli — для звіту ЛР2.
Без сторонніх бібліотек. Вихід: docs/lab_2/img/*.svg
"""
import html
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import analyze_captures as ac  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAP = ROOT / "docs" / "captures"
IMG = ROOT / "docs" / "lab_2" / "img"

W, LEFT, ROW, TOP = 1400, 110, 46, 40
COL = {"SCL": "#1565C0", "SDA": "#2E7D32", "SCK": "#1565C0", "MOSI": "#6A1B9A", "MISO": "#EF6C00",
       "CS": "#424242", "MARK": "#C62828", "PWM_PAN": "#1565C0", "PWM_TILT": "#AD1457",
       "VCP_TX": "#00838F", "FT_RXD": "#5D4037"}


def sigrok_ann(vcd, dec, classes, t0, ds=10):
    res = subprocess.run(["sigrok-cli", "-I", f"vcd:downsample={ds}", "-i", str(vcd), "-P", dec,
                          "--protocol-decoder-samplenum", "-A", f"{dec.split(':')[0]}={classes}"],
                         capture_output=True, text=True).stdout
    out = []
    for line in res.splitlines():
        rng, _, txt = line.partition(" ")
        a, b = (int(x) * ds for x in rng.split("-"))
        out.append((a, b, txt.split(": ", 1)[1] if ": " in txt else txt))
    return [x for x in out if x[1] >= t0]


def svg(name, title, sig, chans, t0, t1, anns=(), marks=(), unit="мкс", scale=1e3):
    h = TOP + ROW * len(chans) + 70
    xs = lambda t: LEFT + (t - t0) * (W - LEFT - 20) / (t1 - t0)  # noqa: E731
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{h}" font-family="Helvetica,Arial" font-size="12">',
         f'<rect width="{W}" height="{h}" fill="white"/>',
         f'<text x="10" y="20" font-size="15" font-weight="bold">{html.escape(title)}</text>']
    for i, ch in enumerate(chans):
        y0 = TOP + i * ROW
        hi, lo = y0 + 6, y0 + 28
        o.append(f'<text x="10" y="{y0 + 22}" fill="{COL.get(ch, "#000")}" font-weight="bold">{ch}</text>')
        pts, v = [], ac.level_at(sig[ch], t0)
        pts.append((xs(t0), hi if v else lo))
        for t, nv in sig[ch]:
            if t0 < t < t1:
                pts.append((xs(t), hi if v else lo))
                v = nv
                pts.append((xs(t), hi if v else lo))
        pts.append((xs(t1), hi if v else lo))
        o.append(f'<polyline fill="none" stroke="{COL.get(ch, "#000")}" stroke-width="1.4" points="'
                 + " ".join(f"{x:.1f},{y}" for x, y in pts) + '"/>')
    for row_ch, a, b, txt, color in anns:
        i = chans.index(row_ch)
        y = TOP + i * ROW + 30
        xa, xb = xs(max(a, t0)), xs(min(b, t1))
        o.append(f'<rect x="{xa:.1f}" y="{y}" width="{max(xb - xa, 2):.1f}" height="14" fill="{color}" opacity="0.85" rx="2"/>')
        if xb - xa > 6 * len(txt) or len(txt) <= 3:
            o.append(f'<text x="{(xa + xb) / 2:.1f}" y="{y + 11}" fill="white" font-size="10" text-anchor="middle">{html.escape(txt)}</text>')
    for t, txt in marks:
        x = xs(t)
        o.append(f'<line x1="{x:.1f}" y1="{TOP - 6}" x2="{x:.1f}" y2="{TOP + ROW * len(chans)}" stroke="#999" stroke-dasharray="3,3"/>')
        o.append(f'<text x="{x + 3:.1f}" y="{TOP - 8}" font-size="10" fill="#555">{html.escape(txt)}</text>')
    ya = TOP + ROW * len(chans) + 20
    o.append(f'<line x1="{LEFT}" y1="{ya}" x2="{W - 20}" y2="{ya}" stroke="#333"/>')
    for k in range(11):
        t = t0 + (t1 - t0) * k / 10
        x = xs(t)
        o.append(f'<line x1="{x:.1f}" y1="{ya}" x2="{x:.1f}" y2="{ya + 5}" stroke="#333"/>')
        o.append(f'<text x="{x:.1f}" y="{ya + 18}" text-anchor="middle" font-size="10">{(t - t0) / scale:.1f} {unit}</text>')
    o.append("</svg>")
    IMG.mkdir(parents=True, exist_ok=True)
    (IMG / f"{name}.svg").write_text("\n".join(o))
    print("written", name)


def i2c_bits(sig, a, b):
    """Ручне декодування: SDA на кожному фронті SCL, START/STOP — без декодера."""
    out = []
    for t, c in ac.i2c_conditions(sig, a, b):
        out.append(("SDA", t - 1500, t + 1500, "S" if c == "START" else "P", "#000"))
    rises = ac.edges(sig["SCL"], 1, a, b)
    labels = ["A6", "A5", "A4", "A3", "A2", "A1", "A0", "R/W", "ACK"] + [f"D{7 - k}" for k in range(8)] + ["ACK"]
    for k, r in enumerate(rises[:18]):
        bit = ac.level_at(sig["SDA"], r)
        lab = labels[k] if k < len(labels) else ""
        out.append(("SCL", r - 2200, r + 2200, lab, "#455A64"))
        out.append(("SDA", r - 2200, r + 2200, str(bit), "#2E7D32" if k not in (8, 17) else "#C62828"))
    return out


def main():
    m = ac.load_vcd(CAP / "i2c-mpu6050-whoami-burst.vcd")
    w = ac.mark_windows(m)
    (a1, b1), (a2, b2) = w[-2], w[-1]
    conds = ac.i2c_conditions(m, a1, b1)
    s0 = conds[0][0]
    p0 = next(t for t, c in conds if c == "STOP")
    svg("i2c_manual_decode", "I2C, ручне декодування: START, адреса 0x68 (1101000), W=0, ACK, регістр 0x75, ACK, STOP",
        m, ["SCL", "SDA"], s0 - 4000, p0 + 4000, i2c_bits(m, s0 - 10, p0 + 10))
    an = sigrok_ann(CAP / "i2c-mpu6050-whoami-burst.vcd", "i2c:scl=SCL:sda=SDA",
                    "address-read:address-write:data-read:data-write:ack:nack:start:stop", a1, 50)
    rows = [("SDA", a, b, t.replace("Address ", "A ").replace("Data ", "").replace("write: ", "W ").replace("read: ", "R "),
             "#1565C0" if "write" in t else ("#2E7D32" if "read" in t else "#757575"))
            for a, b, t in an if a <= b2]
    svg("i2c_whoami_burst", "I2C: WHO_AM_I (запис 0x75 | читання 0x68) і пакетне читання 14 байт з 0x3B (cap i2c)",
        m, ["SCL", "SDA", "MARK", "VCP_TX"], a1 - 20000, b2 + 20000, rows, [(a1, "MARK↑ who"), (a2, "MARK↑ burst")])

    s = ac.load_vcd(CAP / "spi-w25q64-jedec-wren-program-read.vcd")
    ws = ac.mark_windows(s)[-3:]
    vcd = CAP / "spi-w25q64-jedec-wren-program-read.vcd"
    mosi = sigrok_ann(vcd, "spi:clk=SCK:mosi=MOSI:miso=MISO:cs=CS", "mosi-data", ws[0][0], 5)
    miso = sigrok_ann(vcd, "spi:clk=SCK:mosi=MOSI:miso=MISO:cs=CS", "miso-data", ws[0][0], 5)
    rows = [("MOSI", a, b, t, "#6A1B9A") for a, b, t in mosi] + [("MISO", a, b, t, "#EF6C00") for a, b, t in miso]
    a, b = ws[0]
    svg("spi_jedec", "SPI mode 0, 3 МГц: Read JEDEC ID (9Fh) -> EF 40 17 при утриманому CS#", s,
        ["CS", "SCK", "MOSI", "MISO", "MARK"], a - 3000, b + 3000, [r for r in rows if a - 3000 <= r[1] <= b + 3000])
    a, b = ws[1]
    svg("spi_wren_program", "SPI: WREN (06h) -> RDSR (05h, WEL=1) -> Page Program (02h 7F F1 00 + 16 байт) -> опитування BUSY",
        s, ["CS", "SCK", "MOSI", "MISO", "MARK"], a - 5000, b + 5000, [r for r in rows if a - 5000 <= r[1] <= b + 5000])

    p = ac.load_vcd(CAP / "pwm-servo-pan-tilt-3-angles.vcd")
    wp = ac.mark_windows(p)[-3:]
    svg("pwm_3_angles", "PWM TIM3: pan 0°/90°/180° (544/1472/2400 мкс), tilt 45°/90°/135° (1008/1472/1936 мкс), період 20 мс",
        p, ["PWM_PAN", "PWM_TILT", "MARK"], wp[0][0] - 25e6, wp[2][0] + 75e6, (), [(t, f"крок {k + 1}") for k, (t, _) in enumerate(wp)],
        "мс", 1e6)

    u = ac.load_vcd(CAP / "uart-vcp-and-ft232rl.vcd")
    wu = ac.mark_windows(u)[-1]
    ua = sigrok_ann(CAP / "uart-vcp-and-ft232rl.vcd", "uart:rx=VCP_TX:baudrate=115200", "rx-data", wu[0], 100)
    first = [x for x in ua if x[0] >= wu[0]][:6]
    t0, t1 = first[0][0] - 20000, first[-1][1] + 20000
    svg("uart_bytes", "UART 115200 8N1: перші байти рядка CAP,uart,... (start-біт, 8 біт LSB first, stop-біт)", u,
        ["VCP_TX", "MARK"], t0, t1, [("VCP_TX", a, b, t, "#00838F") for a, b, t in first])

    an = ac.load_vcd(CAP / "i2c-anomaly-nack-restart-hang.vcd")
    wa = ac.mark_windows(an)[-3:]
    aa = sigrok_ann(CAP / "i2c-anomaly-nack-restart-hang.vcd", "i2c:scl=SCL:sda=SDA",
                    "address-read:address-write:data-read:data-write:ack:nack:start:stop", wa[0][0], 50)
    svg("i2c_anomaly", "Аномалії: NACK на 0x69 (A1); HAL_I2C_Mem_Read зависає після байта регістра (A2); відновлення",
        an, ["SCL", "SDA", "MARK"], wa[0][0] - 20000, wa[2][1] + 20000,
        [("SDA", a, b, t.replace("Address ", "A ").replace("Data ", ""), "#C62828" if "NACK" in t else "#1565C0")
         for a, b, t in aa if a <= wa[2][1]], [(wa[0][0], "wrong addr"), (wa[1][0], "Mem_Read"), (wa[2][0], "після recover")])


if __name__ == "__main__":
    main()
