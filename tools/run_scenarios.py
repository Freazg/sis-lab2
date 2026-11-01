#!/usr/bin/env python3
"""
Відтворювані сценарії ЛР3 (методичка §6) у Wokwi, форма B.

Кожен сценарій — список кроків автоматизації Wokwi. Скрипт генерує:
  docs/metrics/scenarios/<name>.yaml     — сценарій (команди UART, керування IMU, кнопка B1);
  docs/metrics/scenarios/<name>.diagram.json — стенд, щупи аналізатора переставлено на потрібні сигнали;
і запускає wokwi-cli, зберігаючи:
  docs/metrics/runs/<name>.log           — лог UART (перший рядок містить git-хеш збірки);
  docs/captures/lr3-<name>.vcd           — запис логічного аналізатора, якщо задано channels.

Запуск:  python3 tools/run_scenarios.py [ім'я ...]     без імен — усі, крім long.
Випадкові моменти натискань задані фіксованим зерном: той самий сценарій можна повторити в ЛР4.
"""
import json
import math
import pathlib
import random
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import capture  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
MET = ROOT / "docs" / "metrics"
SCN = MET / "scenarios"
RUNS = MET / "runs"
CAP = ROOT / "docs" / "captures"
WOKWI = capture.WOKWI

capture.SIG.update({
    "P_IMU": "nucleo:PA0", "P_PWM": "nucleo:PA1", "P_LONG": "nucleo:PA4", "P_REACT": "nucleo:PB1",
})
BASE_CHANNELS = ["BTN", "P_REACT", "P_IMU", "P_PWM", "P_LONG", "PWM_PAN", "MARK", "VCP_RX"]
SEED = 3


# ---- Кроки ------------------------------------------------------------------
def cmd(text):
    return [f'  - write-serial: "{text}\\n"']


def wait(text):
    return [f"  - wait-serial: '{text}'"]


def delay(ms):
    return [f"  - delay: {int(ms)}ms"]


def ctrl(part, control, value):
    return [f"  - set-control:\n      part-id: {part}\n      control: {control}\n      value: {value}"]


def press(ms=60, part="btn_b1"):
    return ctrl(part, "pressed", 1) + delay(ms) + ctrl(part, "pressed", 0)


def imu(**axes):
    out = []
    for k, v in axes.items():
        out += ctrl("imu", k, round(v, 4))
    return out


def tilt(pitch=0.0, roll=0.0):
    """Вектор прискорення для заданого нахилу, як у tests/test_motion.c."""
    p, r = math.radians(pitch), math.radians(roll)
    return imu(accelX=-math.sin(p), accelY=math.cos(p) * math.sin(r), accelZ=math.cos(p) * math.cos(r))


def start():
    return wait("READY") + delay(100)


def begin_measure():
    return cmd("stats reset") + wait("STATS,RESET") + delay(10)


def report():
    return cmd("stats") + wait("STATS,END") + delay(20) + cmd("mem") + delay(30)


def set_mode(m):
    return cmd(f"mode {m}") + wait(f"MODE,{m.upper()}") + delay(30)


# ---- Сценарії ---------------------------------------------------------------
def s1():
    """Простій: режим HOLD, лише періодичні задачі, 10 с."""
    return start() + cmd("perfcal") + wait("PERFCAL read_cyc") + delay(10) + set_mode("hold") + begin_measure() \
        + delay(10000) + report()


def s2():
    """Номінальний: запис траєкторії 45 с, повільний рух, status раз на 2 с. Є стирання сектора на 39-й с."""
    steps = start() + set_mode("rec") + begin_measure()
    for i in range(23):
        steps += tilt(pitch=20 * math.sin(i / 3), roll=30 * math.cos(i / 4)) + delay(1000) + cmd("status") + delay(1000)
    return steps + set_mode("hold") + report()


def record_first(seconds):
    return set_mode("rec") + delay(seconds * 1000) + set_mode("hold")


def s3():
    """Найгірший: dump на хост + періодичні задачі + команди + рух + кнопка, 40 циклів."""
    rnd = random.Random(SEED)
    steps = start() + record_first(10) + set_mode("live") + begin_measure()
    for i in range(40):
        steps += tilt(roll=30 if i % 2 else -30) + cmd("dump") + delay(rnd.randint(50, 500)) + press(60)
        steps += cmd("status") + cmd("status") + cmd("status")
        steps += wait("DUMP,END") + delay(50) + cmd("mode live") + wait("MODE,LIVE") + delay(100)
    return steps + report()


def sc1_erase():
    """6.2.1: стирання 16 секторів і періодична задача."""
    steps = start() + set_mode("live") + begin_measure()
    for _ in range(4):
        steps += cmd("erase") + wait("ERASE,DONE") + delay(300)
    return steps + report()


def sc1_dump():
    """6.2.1: передача траєкторії на хост і періодична задача."""
    steps = start() + record_first(10) + set_mode("live") + begin_measure()
    for _ in range(4):
        steps += cmd("dump") + wait("DUMP,END") + delay(300)
    return steps + report()


def sc2_btn_idle():
    """Контроль до 6.2.2: кнопка без довгої операції, 50 натискань у випадкові моменти."""
    rnd = random.Random(SEED)
    steps = start() + set_mode("live") + begin_measure()
    for _ in range(50):
        steps += delay(rnd.randint(30, 130)) + press(60) + delay(30) + cmd("mode live") + wait("MODE,LIVE") + delay(30)
    return steps + report()


def sc2_btn_long():
    """6.2.2: кнопка у випадковий момент під час dump, 50 разів."""
    rnd = random.Random(SEED)
    steps = start() + record_first(4) + set_mode("live") + begin_measure()
    for _ in range(50):  # у LIVE кнопка переводить у HOLD: запис у флеш не зачіпається
        steps += cmd("dump") + delay(rnd.randint(10, 280)) + press(60) + wait("DUMP,END") + delay(40)
        steps += cmd("mode live") + wait("MODE,LIVE") + delay(30)
    return steps + report()


def sc3_flood():
    """6.2.3: команди без пауз під час довгої операції; втрачені байти, команда -> відповідь."""
    burst = "status\\n" * 24  # 168 байт: більше за кільцевий буфер 64 байти
    steps = start() + record_first(4) + set_mode("hold") + begin_measure()
    for _ in range(10):
        steps += cmd("dump") + delay(20) + [f'  - write-serial: "{burst}"'] + wait("DUMP,END") + delay(300)
    return steps + cmd("status") + delay(50) + report()


def attach(on):
    """Кнопки послідовно в SDA і SCL: натиснуто = сенсор під'єднаний, відпущено = від'єднаний."""
    return ctrl("btn_det_a", "pressed", int(on)) + ctrl("btn_det_b", "pressed", int(on))


def sc4_fault():
    """6.2.4: сенсор фізично від'єднано від шини, потім SDA затиснуто на землю; блокування, відновлення."""
    steps = attach(True) + start() + set_mode("live") + begin_measure() + delay(1000) + cmd("stats") + wait("STATS,END")
    steps += delay(20) + begin_measure() + attach(False) + delay(1000)
    steps += tilt(roll=30) + delay(500) + cmd("status") + delay(30) + cmd("stats") + wait("STATS,END") + delay(20)
    steps += attach(True) + wait("IMU,restored") + delay(500) + tilt() + delay(500)
    steps += begin_measure() + press(1000, "btn_sda") + wait("IMU,restored") + delay(300)
    steps += cmd("stats") + wait("STATS,END") + delay(20) + cmd("evt") + delay(100)
    return steps


def sc5_break():
    """6.2.5: підвищення частоти опитування IMU до зламу. Кожна частота — 3 с у режимі LIVE."""
    steps = start() + set_mode("live")
    for hz in (200, 250, 300, 350, 400, 450, 500, 550, 600, 700, 800, 1000):
        steps += cmd(f"rate {hz}") + wait("RATE,") + delay(3000) + cmd("stats") + wait("STATS,END") + delay(30)
    return steps


def sc7_boot():
    """6.2.7: штатний старт, потім сенсор не відповідає при старті і з'являється через 300 і 1000 мс."""
    steps = attach(True) + wait("BOOT,first_imu_us") + delay(100)
    for ms in (300, 1000):
        steps += attach(False) + delay(50) + cmd("reset") + wait("READY") + delay(ms) + attach(True)
        steps += wait("BOOT,first_imu_us") + delay(100)
    return steps


def sc8_powerloss():
    """6.2.8: обрив живлення під час запису сторінки; після старту перевірка CRC кожної сторінки."""
    return start() + set_mode("rec") + delay(5000) + cmd("fault powerloss") + wait("FAULT,powerloss_armed") \
        + wait("BOOT,fw=") + wait("READY") + delay(100) + cmd("dump") + wait("DUMP,END") + delay(50) \
        + set_mode("play") + wait("PLAY,DONE") + delay(50)


def fr02():
    """FR-02: нахил 0, 15, 30, 45° у статиці."""
    steps = start() + set_mode("hold")
    for a in (0, 15, 30, 45):
        steps += tilt(pitch=a) + delay(1500) + cmd("status") + delay(30)
        steps += tilt(roll=a) + delay(1500) + cmd("status") + delay(30)
    return steps


def fr03():
    """FR-03, FR-11: 30 стрибків нахилу, DWT-еквівалент + маркери GPIO на аналізаторі."""
    rnd = random.Random(SEED)
    steps = start() + set_mode("live") + begin_measure()
    for i in range(30):
        steps += tilt(roll=30 if i % 2 == 0 else -30) + delay(rnd.randint(400, 500))
    return steps + report()


def fr04():
    """FR-04: імпульси на межах і межі ходу."""
    steps = start() + set_mode("hold")
    for deg in (0, 180):  # межі за замовчуванням 10..170: імпульс має бути 10° і 170°
        steps += cmd(f"servo pan {deg}") + delay(100)
    steps += cmd("limits 0 180") + delay(30)
    for deg in (0, 90, 180):
        steps += cmd(f"servo pan {deg}") + delay(100)
    steps += cmd("limits 30 150") + delay(30) + cmd("servo pan 180") + delay(100) + cmd("servo pan 0") + delay(100)
    return steps


def fr05():
    """FR-05: тремтіння ±1° не рухає серво, стрибок 90° обмежений 300 °/с."""
    steps = start() + set_mode("live") + delay(300) + cmd("status") + delay(30)
    for i in range(40):
        steps += tilt(roll=math.sin(i * 0.9)) + delay(50)
    steps += cmd("status") + delay(30) + tilt(roll=-45) + delay(800) + cmd("status") + delay(30)
    steps += tilt(roll=45) + delay(50)
    for _ in range(8):
        steps += cmd("status") + delay(45)
    return steps + delay(400) + cmd("status") + delay(30)


def fr06():
    """FR-06: 4 натискання з перевіркою LED, потім 20 швидких натискань."""
    steps = start()
    for _ in range(4):
        steps += press(80) + delay(200)
    steps += cmd("mode live") + wait("MODE,LIVE") + delay(50) + begin_measure()
    for _ in range(20):
        steps += press(45) + delay(45)
    return steps + delay(300) + report()


def fr07_08():
    """FR-07, FR-08: 60 с запису, скидання, відтворення, dump; маркер PWM на аналізаторі."""
    steps = start() + set_mode("rec") + begin_measure()
    for i in range(30):
        steps += tilt(pitch=25 * math.sin(i / 2), roll=35 * math.cos(i / 3)) + delay(2000)
    steps += set_mode("hold") + cmd("stats") + wait("STATS,END") + delay(20) + cmd("reset") + wait("BOOT,fw=")
    steps += wait("READY") + delay(100) + set_mode("hold") + cmd("stats reset") + wait("STATS,RESET")
    steps += set_mode("play") + wait("PLAY,DONE") + delay(50) + cmd("stats") + wait("STATS,END") + delay(20)
    steps += cmd("dump") + wait("DUMP,END") + delay(50)
    return steps


def fr09():
    """FR-09: кожна команда, невідома команда і рядок понад 64 символи."""
    steps = start()
    for c in ("status", "cal", "limits 20 160", "limits 160 20", "mode hold", "bogus", "x" * 70, "help"):
        steps += cmd(c) + delay(120)
    steps += delay(1100)
    steps += cmd("status") + delay(50)
    return steps


def s_wrap():
    """Переповнення лічильника тактів (89.5 с): 200 с роботи в LIVE, періоди мають лишитись правильними."""
    steps = start() + set_mode("live") + begin_measure() + delay(200000) + report()
    return steps


def long_run():
    """6.2.6: опис циклів для прогону на годину. CI обмежений 5 хв на сесію, тому години йдуть через VS Code."""
    steps = start()
    for _ in range(6):
        steps += set_mode("rec")
        for i in range(150):
            steps += tilt(pitch=20 * math.sin(i / 3), roll=30 * math.cos(i / 4)) + delay(2000)
        steps += set_mode("hold") + cmd("stats") + wait("STATS,END") + delay(20) + cmd("mem") + delay(30)
        steps += begin_measure() + set_mode("play") + wait("PLAY,DONE") + delay(50)
        steps += cmd("stats") + wait("STATS,END") + delay(20) + cmd("mem") + delay(30) + begin_measure()
    steps += cmd("reset") + wait("BOOT,fw=") + wait("READY") + delay(100) + set_mode("hold") + cmd("dump")
    steps += wait("DUMP,END") + delay(50)
    return steps


# name: (функція, щупи аналізатора або None, запас часу симуляції, мс)
SCENARIOS = {
    "s1": (s1, BASE_CHANNELS, 15000),
    "s2": (s2, BASE_CHANNELS, 55000),
    "s3": (s3, BASE_CHANNELS, 90000),
    "sc1-erase": (sc1_erase, BASE_CHANNELS, 12000),
    "sc1-dump": (sc1_dump, BASE_CHANNELS, 30000),
    "sc2-btn-idle": (sc2_btn_idle, BASE_CHANNELS, 25000),
    "sc2-btn-long": (sc2_btn_long, BASE_CHANNELS, 60000),
    "sc3-flood": (sc3_flood, None, 40000),
    "sc4-fault": (sc4_fault, ["SCL", "SDA", "P_IMU", "P_PWM", "P_LONG", "PWM_PAN", "VCP_TX", "MARK"], 20000),
    "sc5-break": (sc5_break, None, 50000),
    "sc7-boot": (sc7_boot, None, 12000),
    "sc8-powerloss": (sc8_powerloss, None, 15000),
    "fr02": (fr02, None, 20000),
    "fr03": (fr03, BASE_CHANNELS, 20000),
    "fr04": (fr04, ["PWM_PAN", "PWM_TILT", "VCP_TX", "VCP_RX", "MARK", "P_PWM", "P_IMU", "P_LONG"], 5000),
    "fr05": (fr05, ["PWM_PAN", "PWM_TILT", "P_PWM", "VCP_RX", "MARK", "P_IMU", "P_LONG", "BTN"], 10000),
    "fr06": (fr06, BASE_CHANNELS, 10000),
    "fr07-08": (fr07_08, ["PWM_PAN", "PWM_TILT", "P_PWM", "P_LONG", "VCP_RX", "MARK", "P_IMU", "BTN"], 170000),
    "fr09": (fr09, None, 8000),
    "wrap": (s_wrap, None, 210000),
    "long": (long_run, None, 4200000),
}
FIXTURE = ("sc4-fault", "sc7-boot")  # схеми з тестовим оснащенням відмов


def fault_fixture(path):
    """Кнопки в лініях сенсора і кнопка SDA -> GND. Є лише у схемах сценаріїв відмов, у diagram.json їх немає."""
    d = json.loads(path.read_text(encoding="utf-8"))
    conn = []
    for c in d["connections"]:
        if "imu:SDA" in c[:2]:
            conn += [["imu:SDA", "btn_det_a:1.l", "green", []], ["btn_det_a:2.l", "nucleo:PB9", "green", []]]
        elif "imu:SCL" in c[:2]:
            conn += [["imu:SCL", "btn_det_b:1.l", "yellow", []], ["btn_det_b:2.l", "nucleo:PB8", "yellow", []]]
        else:
            conn.append(c)
    d["connections"] = conn + [["btn_sda:1.l", "nucleo:PB9", "green", []], ["btn_sda:2.l", "nucleo:GND.8", "black", []]]
    d["parts"] += [{"type": "wokwi-pushbutton", "id": i, "top": -130, "left": 430 + 70 * k,
                    "attrs": {"color": "black", "label": i, "bounce": "0"}}
                   for k, i in enumerate(("btn_sda", "btn_det_a", "btn_det_b"))]
    path.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return path


DEFAULT = [n for n in SCENARIOS if n not in ("long", "wrap")]


def run(name):
    fn, channels, timeout = SCENARIOS[name]
    SCN.mkdir(parents=True, exist_ok=True)
    RUNS.mkdir(parents=True, exist_ok=True)
    yaml = SCN / f"{name}.yaml"
    yaml.write_text("\n".join([f"name: '{name}'", "version: 1", "author: 'tools/run_scenarios.py'",
                               "steps:"] + fn()) + "\n", encoding="utf-8")
    capture.OUT = SCN
    if name in FIXTURE:
        diag = fault_fixture(capture.make_diagram(name, channels or BASE_CHANNELS))
    elif channels:
        diag = capture.make_diagram(name, channels)
    else:
        diag = ROOT / "diagram.json"
    log = RUNS / f"{name}.log"
    cli = [WOKWI, "--quiet", "--timeout", str(timeout), "--diagram-file", str(diag.relative_to(ROOT)),
           "--scenario", str(yaml.relative_to(ROOT)), "--serial-log-file", str(log)]
    if channels:
        cli += ["--vcd-file", str(CAP / f"lr3-{name}.vcd")]
    t0 = time.time()
    res = subprocess.run(cli + ["."], cwd=ROOT, env=capture.token_env(), capture_output=True, text=True)
    print(f"== {name}: exit {res.returncode}, {time.time() - t0:.0f} с")
    if res.returncode != 0:
        print(res.stdout[-1500:], res.stderr[-1500:])
    return res.returncode


def main(argv):
    names = argv or DEFAULT
    bad = [n for n in names if run(n) != 0]
    if bad:
        raise SystemExit(f"не завершились: {bad}")


if __name__ == "__main__":
    main(sys.argv[1:])
