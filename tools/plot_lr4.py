#!/usr/bin/env python3
"""
Рисунки звіту ЛР4 (SVG без зовнішніх бібліотек), стиль як у tools/plot_lr3.py:
  docs/lab_4/img/tasks.svg            — задачі, пріоритети, механізми взаємодії (звіт §10);
  docs/lab_4/img/button_dump_cmp.svg  — кнопка під час dump: ЛР3 і ЛР4 на тій самій шкалі (sc2-btn-long);
  docs/lab_4/img/s3_chronology.svg    — фрагмент S3 на аналізаторі: IMU і кнопка посеред dump (витіснення);
  docs/lab_4/img/trace.svg            — хронологія задач із буфера traceTASK_SWITCHED_IN (os-trace).
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import analyze_captures as ac  # noqa: E402
import analyze_metrics as am  # noqa: E402
from plot_lr3 import W, LEFT, ROW, trace  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
CMP = ROOT / "docs" / "captures" / "cmp"
RUNS = ROOT / "docs" / "metrics" / "runs-lr4"
OUT = ROOT / "docs" / "lab_4" / "img"


def panel(o, sig, names, t0, t1, top, notes, title):
    """Одна панель сигналів; повертає y наступної панелі."""
    x = lambda t: LEFT + (t - t0) * (W - LEFT - 20) / (t1 - t0)  # noqa: E731
    o.append(f'<text x="10" y="{top + 14}" font-weight="bold">{title}</text>')
    rows = {}
    for i, n in enumerate(names):
        y = top + 26 + ROW * i + 22
        rows[n] = y
        o.append(f'<text x="10" y="{y - 6}">{n}</text>')
        o.append(trace(sig[n], t0, t1, y, x))
    bottom = top + 26 + ROW * len(names)
    for a, b, row, text in notes:
        y = rows[row] + 14
        for v in (a, b):
            o.append(f'<line x1="{x(v):.1f}" y1="{top + 20}" x2="{x(v):.1f}" y2="{bottom}" stroke="#b91c1c" '
                     'stroke-dasharray="4 3"/>')
        o.append(f'<line x1="{x(a):.1f}" y1="{y}" x2="{x(b):.1f}" y2="{y}" stroke="#b91c1c" stroke-width="2"/>')
        o.append(f'<text x="{min(x(a) + 6, W - 260):.1f}" y="{y + 15}" fill="#b91c1c">{text}</text>')
    return bottom + 16


def btn_window(fw):
    """Натискання посеред найдовшого dump у sc2-btn-long: (сигнали, t0, t1, натискання, реакція)."""
    sig = ac.load_vcd(CMP / f"{fw}-sc2-btn-long.vcd")
    press = am.presses(sig["BTN"][1:])
    react = am.edges(sig["P_REACT"], 1)
    longs = am.pulses(sig["P_LONG"])
    inside = [t for t in press if any(p[0] < t < p[0] + p[1] for p in longs)]
    t = inside[len(inside) // 2]
    r, _ = am.next_after(react, t)
    lp = [p for p in longs if p[0] < t < p[0] + p[1]][0]
    return sig, lp, t, r


def button_dump_cmp():
    """Обидві прошивки на однаковій шкалі: вікно — dump ЛР3 від початку до кінця, плюс поля."""
    s3, lp3, t3, r3 = btn_window("lr3")
    s4, lp4, t4, r4 = btn_window("lr4")
    span = max(lp3[1], lp4[1]) + 80_000_000
    names = ["P_LONG", "BTN", "P_REACT", "P_IMU"]
    h = 40 + 2 * (26 + ROW * len(names) + 16) + 30
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {h}" font-family="Arial" font-size="12">',
         f'<rect width="{W}" height="{h}" fill="white"/>',
         '<text x="10" y="20" font-size="14" font-weight="bold">Кнопка B1 під час dump (sc2-btn-long): super loop і FreeRTOS</text>']
    y = 30
    for sig, lp, t, r, title in ((s3, lp3, t3, r3, "ЛР3, super loop (тег lab3)"), (s4, lp4, t4, r4, "ЛР4, FreeRTOS")):
        t0 = lp[0] - 40_000_000
        y = panel(o, sig, names, t0, t0 + span, y, [(t, r, "P_REACT", f"реакція {(r - t) / 1e6:.2f} мс")], title)
    o.append(f'<text x="{LEFT}" y="{h - 10}" fill="#555">обидві панелі — вікно {span / 1e6:.0f} мс; час на дроті, '
             'логічний аналізатор Wokwi</text></svg>')
    (OUT / "button_dump_cmp.svg").write_text("\n".join(o), encoding="utf-8")


def s3_chronology():
    """Фрагмент S3 (ЛР4): натискання B1 посеред dump, вікно 60 мс — видно, як btn і imu витісняють long."""
    sig = ac.load_vcd(CMP / "lr4-s3.vcd")
    press = am.presses(sig["BTN"][1:])
    react = am.edges(sig["P_REACT"], 1)
    longs = am.pulses(sig["P_LONG"])
    t = next(t for t in press if any(p[0] < t < p[0] + p[1] for p in longs))
    r, _ = am.next_after(react, t)
    imu = [p[0] for p in am.pulses(sig["P_IMU"]) if p[0] > t]
    t0, t1 = t - 20_000_000, t + 40_000_000
    names = ["P_LONG", "BTN", "P_REACT", "P_IMU", "P_PWM"]
    h = 40 + 26 + ROW * len(names) + 16 + 30
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {h}" font-family="Arial" font-size="12">',
         f'<rect width="{W}" height="{h}" fill="white"/>',
         '<text x="10" y="20" font-size="14" font-weight="bold">S3, FreeRTOS: dump (long, пріоритет 1) витісняють btn (6) і imu (5)</text>']
    notes = [(t, r, "P_REACT", f"реакція btn {(r - t) / 1e3:.0f} мкс")]
    if len(imu) >= 2:
        notes.append((imu[0], imu[1], "P_IMU", f"період IMU {(imu[1] - imu[0]) / 1e6:.2f} мс посеред dump"))
    panel(o, sig, names, t0, t1, 30, notes, "фрагмент 60 мс, P_LONG = 1 увесь час dump")
    o.append(f'<text x="{LEFT}" y="{h - 10}" fill="#555">вікно 60 мс; час на дроті, логічний аналізатор Wokwi</text></svg>')
    (OUT / "s3_chronology.svg").write_text("\n".join(o), encoding="utf-8")


COLORS = {"IDLE": "#d4d4d4", "btn": "#dc2626", "imu": "#2563eb", "servo": "#7c3aed", "cli": "#059669",
          "traj": "#d97706", "log": "#0891b2", "long": "#4b5563"}


def trace_gantt():
    """32 записи «задача отримала процесор у t_us» із B1 під час dump -> діаграма Ганта."""
    text = (RUNS / "os-trace.log").read_text(errors="replace")
    rec = [(int(t), n) for t, n in re.findall(r"TRACE t_us=(\d+) task=(\w+)", text)]
    if len(rec) < 2:
        return
    t0, t1 = rec[0][0], rec[-1][0]
    tasks = ["btn", "imu", "servo", "cli", "traj", "log", "long", "IDLE"]
    x = lambda t: LEFT + (t - t0) * (W - LEFT - 20) / (t1 - t0)  # noqa: E731
    h = 40 + 26 * len(tasks) + 50
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {h}" font-family="Arial" font-size="12">',
         f'<rect width="{W}" height="{h}" fill="white"/>',
         '<text x="10" y="20" font-size="14" font-weight="bold">Хронологія задач (traceTASK_SWITCHED_IN) з натискання B1 '
         'під час dump</text>']
    for i, n in enumerate(tasks):
        y = 40 + 26 * i
        o.append(f'<text x="10" y="{y + 15}">{n}</text>')
        o.append(f'<line x1="{LEFT}" y1="{y + 20}" x2="{W - 20}" y2="{y + 20}" stroke="#eee"/>')
    for (ta, n), (tb, _) in zip(rec, rec[1:]):
        if n not in tasks:
            continue
        y = 40 + 26 * tasks.index(n)
        o.append(f'<rect x="{x(ta):.1f}" y="{y + 4}" width="{max(x(tb) - x(ta), 1.0):.1f}" height="16" '
                 f'fill="{COLORS.get(n, "#999")}"/>')
    o.append(f'<text x="{LEFT}" y="{h - 10}" fill="#555">{len(rec)} перемикань за {(t1 - t0) / 1000:.1f} мс; '
             'мітки — platform_micros() у traceTASK_SWITCHED_IN, docs/metrics/runs-lr4/os-trace.log</text></svg>')
    (OUT / "trace.svg").write_text("\n".join(o), encoding="utf-8")


def tasks_diagram():
    """Задачі за пріоритетом зверху вниз; ліворуч — переривання, праворуч — ресурси під м'ютексами."""
    tasks = [("btn", 6, "сповіщення EXTI13"), ("imu", 5, "DelayUntil 5 мс"), ("servo", 4, "DelayUntil 20 мс"),
             ("cli", 3, "сповіщення USART2"), ("traj", 2, "черга q_traj"), ("log", 2, "черга q_log"),
             ("long", 1, "сповіщення від cli"), ("IDLE", 0, "ядро")]
    H = 60 + 52 * len(tasks) + 40
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" font-family="Arial" font-size="12">',
         f'<rect width="{W}" height="{H}" fill="white"/>',
         '<defs><marker id="a" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">'
         '<path d="M0,0 L8,4 L0,8 z" fill="#333"/></marker></defs>',
         '<text x="10" y="20" font-size="14" font-weight="bold">Задачі FreeRTOS, пріоритети і механізми взаємодії</text>']
    tx, tw = 380, 190
    ty = {}
    for i, (n, p, how) in enumerate(tasks):
        y = 50 + 52 * i
        ty[n] = y + 20
        o.append(f'<rect x="{tx}" y="{y}" width="{tw}" height="40" rx="6" fill="#ecfdf5" stroke="#047857"/>')
        o.append(f'<text x="{tx + tw / 2}" y="{y + 17}" text-anchor="middle" font-weight="bold">{n} — пріоритет {p}</text>')
        o.append(f'<text x="{tx + tw / 2}" y="{y + 33}" text-anchor="middle">{how}</text>')

    def arrow(x1, y1, x2, y2, label="", dash=False, lx=None, ly=None):
        d = ' stroke-dasharray="5 3"' if dash else ""
        o.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#333"{d} marker-end="url(#a)"/>')
        if label:
            o.append(f'<text x="{lx if lx is not None else (x1 + x2) / 2}" y="{ly if ly is not None else (y1 + y2) / 2 - 4}" '
                     f'text-anchor="middle" font-size="11">{label}</text>')

    for name, y in (("EXTI4_15 (B1), NVIC 1", ty["btn"]), ("USART2 RX, NVIC 2", ty["cli"])):
        o.append(f'<rect x="20" y="{y - 18}" width="190" height="36" rx="6" fill="#fef3c7" stroke="#b45309"/>')
        o.append(f'<text x="115" y="{y + 4}" text-anchor="middle">{name}</text>')
        arrow(210, y, tx, y, "xTaskNotifyFromISR", lx=295)
    o.append(f'<rect x="20" y="{ty["imu"] - 18}" width="190" height="62" rx="6" fill="#fef3c7" stroke="#b45309"/>')
    o.append(f'<text x="115" y="{ty["imu"] + 2}" text-anchor="middle">SysTick, NVIC 3: тік ОС</text>')
    o.append(f'<text x="115" y="{ty["imu"] + 20}" text-anchor="middle">будить imu і servo</text>')
    arrow(210, ty["imu"], tx, ty["imu"], "DelayUntil", lx=295)
    arrow(210, ty["imu"] + 20, tx, ty["servo"], "", dash=True)
    # черги і сповіщення між задачами — ліворуч від колонки задач
    lane = tx - 30
    for src, label in (("btn", "MSG_MODE"), ("cli", "MSG_MODE"), ("servo", "MSG_SAMPLE")):
        o.append(f'<polyline points="{tx},{ty[src] + 8} {lane},{ty[src] + 8} {lane},{ty["traj"]} {tx - 2},{ty["traj"]}" '
                 'fill="none" stroke="#2563eb" marker-end="url(#a)"/>')
    o.append(f'<text x="{lane - 36}" y="{ty["traj"] + 4}" text-anchor="end" fill="#2563eb" font-size="11">q_traj 16 × 6 Б: '
             'режим (btn, cli), семпл (servo)</text>')
    o.append(f'<polyline points="{tx},{ty["imu"] + 12} {lane - 14},{ty["imu"] + 12} {lane - 14},{ty["log"]} {tx - 2},{ty["log"]}" '
             'fill="none" stroke="#0891b2" marker-end="url(#a)"/>')
    o.append(f'<text x="{lane - 36}" y="{ty["log"] + 4}" text-anchor="end" fill="#0891b2" font-size="11">q_log 4 × 97 Б: '
             'рядки imu</text>')
    o.append(f'<polyline points="{tx},{ty["cli"] + 12} {lane - 28},{ty["cli"] + 12} {lane - 28},{ty["long"]} {tx - 2},{ty["long"]}" '
             'fill="none" stroke="#7c3aed" marker-end="url(#a)"/>')
    o.append(f'<text x="{lane - 36}" y="{ty["long"] + 4}" text-anchor="end" fill="#7c3aed" font-size="11">сповіщення зі '
             'значенням: dump, erase</text>')
    # ресурси праворуч
    rx = tx + tw + 150
    res = [("М'ютекс I2C → MPU-6050", ["imu", "cli"], "imu чекає ≤ 2 мс"),
           ("М'ютекс FLASH → W25Q64", ["servo", "cli", "traj", "long"], "одна операція"),
           ("М'ютекс CON → USART2 TX", ["btn", "imu", "servo", "cli", "traj", "log", "long"], "рядок цілком")]
    for i, (title, users, note) in enumerate(res):
        y = 70 + 130 * i
        o.append(f'<rect x="{rx}" y="{y}" width="230" height="44" rx="6" fill="#ede9fe" stroke="#6d28d9"/>')
        o.append(f'<text x="{rx + 115}" y="{y + 18}" text-anchor="middle" font-weight="bold">{title}</text>')
        o.append(f'<text x="{rx + 115}" y="{y + 35}" text-anchor="middle">{note}</text>')
        for k, u in enumerate(users):
            o.append(f'<line x1="{tx + tw}" y1="{ty[u] - 10 + 4 * i}" x2="{rx}" y2="{y + 10 + 4 * k}" stroke="#a78bfa" '
                     'stroke-width="0.8"/>')
    o.append(f'<text x="{rx}" y="{70 + 130 * 3}" font-size="11" fill="#555">порядок захоплення: шина (I2C або FLASH),</text>')
    o.append(f'<text x="{rx}" y="{86 + 130 * 3}" font-size="11" fill="#555">потім CON; з CON шину не беруть</text>')
    o.append(f'<text x="10" y="{H - 12}" fill="#555">Пріоритет — за жорсткістю дедлайну (§5.1). ISR лише фіксує подію й будить '
             'задачу; дані між задачами — копією в черзі.</text></svg>')
    (OUT / "tasks.svg").write_text("\n".join(o), encoding="utf-8")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    tasks_diagram()
    for fn in (button_dump_cmp, s3_chronology, trace_gantt):
        try:
            fn()
        except (FileNotFoundError, IndexError, StopIteration, TypeError) as e:
            print(f"{fn.__name__}: пропущено ({e!r})")
    print("ok")
