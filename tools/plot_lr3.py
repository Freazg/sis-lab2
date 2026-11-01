#!/usr/bin/env python3
"""
Анотовані записи логічного аналізатора й діаграма циклу для звіту ЛР3 (SVG без зовнішніх бібліотек):
  docs/lab_3/img/button_during_dump.svg  — затримка реакції на кнопку під час довгої операції;
  docs/lab_3/img/imu_hole_erase.svg      — «дірка» в опитуванні IMU під час стирання сектора;
  docs/lab_3/img/loop.svg                — структура головного циклу.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import analyze_captures as ac  # noqa: E402
import analyze_metrics as am  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAP = ROOT / "docs" / "captures"
OUT = ROOT / "docs" / "lab_3" / "img"
W, LEFT, ROW = 980, 110, 46


def level(sig, t):
    v = 0
    for tt, vv in sig:
        if tt > t:
            break
        v = vv
    return v


def trace(sig, t0, t1, y, x):
    pts = [f"{x(t0):.1f},{y - 22 * level(sig, t0)}"]
    last = level(sig, t0)
    for t, v in sig:
        if t0 < t < t1 and v != last:
            pts.append(f"{x(t):.1f},{y - 22 * last}")
            pts.append(f"{x(t):.1f},{y - 22 * v}")
            last = v
    pts.append(f"{x(t1):.1f},{y - 22 * last}")
    return f'<polyline points="{" ".join(pts)}" fill="none" stroke="#14532d" stroke-width="1.2"/>'


def svg(path, sig, names, t0, t1, notes, title):
    h = 54 + ROW * len(names) + 44
    x = lambda t: LEFT + (t - t0) * (W - LEFT - 20) / (t1 - t0)  # noqa: E731
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {h}" font-family="Arial" font-size="12">',
         f'<rect width="{W}" height="{h}" fill="white"/>', f'<text x="10" y="20" font-size="14" font-weight="bold">{title}</text>']
    rows = {}
    for i, n in enumerate(names):
        y = 62 + ROW * i + 22
        rows[n] = y
        o.append(f'<text x="10" y="{y - 6}">{n}</text>')
        o.append(trace(sig[n], t0, t1, y, x))
    for a, b, row, text in notes:
        y = rows[row] + 14
        o.append(f'<line x1="{x(a):.1f}" y1="42" x2="{x(a):.1f}" y2="{h - 30}" stroke="#b91c1c" stroke-dasharray="4 3"/>')
        o.append(f'<line x1="{x(b):.1f}" y1="42" x2="{x(b):.1f}" y2="{h - 30}" stroke="#b91c1c" stroke-dasharray="4 3"/>')
        o.append(f'<line x1="{x(a):.1f}" y1="{y}" x2="{x(b):.1f}" y2="{y}" stroke="#b91c1c" stroke-width="2"/>')
        o.append(f'<text x="{(x(a) + x(b)) / 2:.1f}" y="{y + 15}" text-anchor="middle" fill="#b91c1c">{text}</text>')
    o.append(f'<text x="{LEFT}" y="{h - 10}" fill="#555">вікно {(t1 - t0) / 1e6:.0f} мс; час на дроті, логічний аналізатор Wokwi</text></svg>')
    path.write_text("\n".join(o), encoding="utf-8")


def button_during_dump():
    sig = ac.load_vcd(CAP / "lr3-sc2-btn-long.vcd")
    press = am.presses(sig["BTN"][1:])
    react = am.edges(sig["P_REACT"], 1)
    t = press[len(press) // 2]
    r, _ = am.next_after(react, t)
    long_p = [p for p in am.pulses(sig["P_LONG"]) if p[0] < t < p[0] + p[1]][0]
    t0, t1 = long_p[0] - 30_000_000, r + 40_000_000
    svg(OUT / "button_during_dump.svg", sig, ["P_LONG", "BTN", "P_REACT", "P_IMU"], t0, t1,
        [(t, r, "P_REACT", f"затримка реакції {(r - t) / 1e6:.1f} мс")],
        "Кнопка під час dump: подія чекає кінця довгої операції, IMU стоїть")


def imu_hole():
    sig = ac.load_vcd(CAP / "lr3-s2.vcd")
    erase = max(am.pulses(sig["P_LONG"]), key=lambda p: p[1])
    imu = am.pulses(sig["P_IMU"])
    before = [p for p in imu if p[0] < erase[0]][-1]
    after = [p for p in imu if p[0] > erase[0] + erase[1]][0]
    t0, t1 = erase[0] - 30_000_000, erase[0] + erase[1] + 30_000_000
    svg(OUT / "imu_hole_erase.svg", sig, ["P_LONG", "P_IMU", "P_PWM"], t0, t1,
        [(before[0], after[0], "P_IMU", f"інтервал IMU {(after[0] - before[0]) / 1e6:.1f} мс замість 5 мс")],
        "Стирання сектора в режимі REC: діра в періодичній задачі")


def loop_diagram():
    boxes = [("cli_task", "подія: байт у буфері"), ("button_task", "подія: EXTI13"), ("imu_task", "кожні 5 мс"),
             ("servo_task", "кожні 20 мс"), ("rec_task", "20 мс, REC"), ("play_task", "20 мс, PLAY"), ("load_task", "вікно 1 с")]
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} 300" font-family="Arial" font-size="12">',
         f'<rect width="{W}" height="300" fill="white"/>',
         '<text x="10" y="20" font-size="14" font-weight="bold">Головний цикл app_poll() і переривання</text>']
    bw = 122
    for i, (a, b) in enumerate(boxes):
        x = 18 + i * (bw + 16)
        o.append(f'<rect x="{x}" y="60" width="{bw}" height="60" rx="6" fill="#ecfdf5" stroke="#047857"/>')
        o.append(f'<text x="{x + bw / 2}" y="85" text-anchor="middle" font-weight="bold">{a}</text>')
        o.append(f'<text x="{x + bw / 2}" y="104" text-anchor="middle">{b}</text>')
        if i:
            o.append(f'<line x1="{x - 16}" y1="90" x2="{x}" y2="90" stroke="#333"/><polygon points="{x},90 {x - 6},86 {x - 6},94" fill="#333"/>')
    end = 18 + 6 * (bw + 16) + bw / 2
    o.append(f'<polyline points="{end},120 {end},150 79,150 79,120" fill="none" stroke="#333" stroke-dasharray="5 3"/>')
    o.append('<text x="430" y="166" text-anchor="middle">наступний прохід: задача перевіряє час і повертає керування</text>')
    for i, (a, b) in enumerate((("SysTick, пріоритет 0", "uwTick, база лічильника тактів"), ("EXTI13, пріоритет 1", "мітка часу, btn_pending"),
                                ("USART2 RX, пріоритет 2", "кільцевий буфер 64 байти"))):
        x = 60 + i * 300
        o.append(f'<rect x="{x}" y="200" width="260" height="60" rx="6" fill="#fef3c7" stroke="#b45309"/>')
        o.append(f'<text x="{x + 130}" y="225" text-anchor="middle" font-weight="bold">{a}</text>')
        o.append(f'<text x="{x + 130}" y="244" text-anchor="middle">{b}</text>')
    o.append('<text x="10" y="285" fill="#555">Витіснення є лише в ISR. Задачі між собою не витісняються, тому довга задача затримує всі наступні.</text></svg>')
    (OUT / "loop.svg").write_text("\n".join(o), encoding="utf-8")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    button_during_dump()
    imu_hole()
    loop_diagram()
    print("ok")
