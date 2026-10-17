#!/usr/bin/env python3
"""
Відтворювані записи логічного аналізатора для ЛР2 (форма B, Wokwi).

Для кожного запису:
  1. береться diagram.json (монтажна схема стенда) і в ньому ПЕРЕСТАВЛЯЮТЬСЯ ЩУПИ
     логічного аналізатора на потрібні сигнали (решта схеми не змінюється) —
     це docs/captures/<name>.diagram.json;
  2. генерується сценарій автоматизації Wokwi <name>.scenario.yaml: дочекатися READY,
     надіслати в консоль команду, яка ініціює саме цю транзакцію (cap i2c / cap spi ...);
  3. wokwi-cli запускає симуляцію і зберігає <name>.vcd та <name>.uart.log;
  4. sigrok-cli декодує VCD у текст <name>.<decoder>.txt (анотації для аналізу і тестів).

Запуск: python3 tools/capture.py [ім'я ...]     (потрібен WOKWI_CLI_TOKEN або ~/.wokwi_token)
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "captures"
WOKWI = shutil.which("wokwi-cli") or str(pathlib.Path.home() / ".wokwi" / "bin" / "wokwi-cli")
SIGROK = shutil.which("sigrok-cli") or "sigrok-cli"

# Сигнали стенда -> пін плати в diagram.json
SIG = {
    "SCL": "nucleo:PB8", "SDA": "nucleo:PB9",
    "SCK": "nucleo:PB3", "MOSI": "nucleo:PA7", "MISO": "nucleo:PA6", "CS": "nucleo:D10",
    "PWM_PAN": "nucleo:PB4", "PWM_TILT": "nucleo:PB5",
    "VCP_TX": "nucleo:PA2", "VCP_RX": "nucleo:PA3",
    "FT_RXD": "nucleo:PB6", "FT_TXD": "nucleo:PB7",
    "MARK": "nucleo:PA9", "BTN": "nucleo:PC13",
}

# downsample: Wokwi пише VCD з роздільністю 1 нс (еквівалент 1 ГГц);
# при імпорті ділимо до samplerate = 1e9 / downsample.
CAPTURES = {
    "i2c-mpu6050-whoami-burst": {
        "channels": ["SCL", "SDA", "MARK", "VCP_TX"],
        "commands": ["mode hold", "cap i2c"], "until": "CAP,i2c,END",
        "downsample": 50,  # 20 MS/s для I2C 400 кГц -> 50 вибірок/біт
        "decoders": ["i2c:scl=SCL:sda=SDA"],
    },
    "spi-w25q64-jedec-wren-program-read": {
        "channels": ["SCK", "MOSI", "MISO", "CS", "MARK", "VCP_TX"],
        "commands": ["mode hold", "cap spi"], "until": "CAP,spi,END",
        "downsample": 10,  # 100 MS/s для SCK 3 МГц -> 33 вибірки/біт
        "decoders": ["spi:clk=SCK:mosi=MOSI:miso=MISO:cs=CS:cpol=0:cpha=0"],
    },
    "pwm-servo-pan-tilt-3-angles": {
        "channels": ["PWM_PAN", "PWM_TILT", "MARK", "VCP_TX"],
        "commands": ["mode hold", "cap pwm"], "until": "CAP,pwm,END",
        "downsample": 1000,  # 1 MS/s -> роздільність 1 мкс = 1 тік TIM3
        "decoders": ["pwm:data=PWM_PAN", "pwm:data=PWM_TILT"],
    },
    "uart-vcp-and-ft232rl": {
        "channels": ["VCP_TX", "VCP_RX", "FT_RXD", "FT_TXD", "MARK"],
        "commands": ["mode hold", "cap uart"], "until": "CAP,uart,END",
        "downsample": 100,  # 10 MS/s для 115200 бод -> 87 вибірок/біт
        "decoders": ["uart:rx=VCP_TX:baudrate=115200", "uart:rx=VCP_RX:baudrate=115200",
                     "uart:rx=FT_RXD:baudrate=115200"],
    },
    "i2c-anomaly-nack-restart-hang": {
        "channels": ["SCL", "SDA", "MARK", "VCP_TX"],
        "commands": ["mode hold", "cap anomaly"], "until": "CAP,anomaly,END",
        "downsample": 50,
        "decoders": ["i2c:scl=SCL:sda=SDA"],
    },
    "i2c-anomaly-hal-build-on-wokwi": {
        # прошивка BOARD_TARGET=nucleo (HAL_I2C_Mem_Read + HAL_I2C_Master_Transmit) у симуляторі
        "channels": ["SCL", "SDA", "MARK", "VCP_TX"],
        "commands": [], "until": "READY",
        "downsample": 50,
        "decoders": ["i2c:scl=SCL:sda=SDA"],
        "elf": "firmware/build-nucleo/firmware.elf",
    },
    "boot-selftest-all-buses": {
        "channels": ["SCL", "SDA", "SCK", "MOSI", "MISO", "CS", "PWM_PAN", "MARK"],
        "commands": [], "until": "READY",
        "downsample": 50,
        "decoders": ["i2c:scl=SCL:sda=SDA", "spi:clk=SCK:mosi=MOSI:miso=MISO:cs=CS:cpol=0:cpha=0"],
    },
}


def token_env():
    env = dict(os.environ)
    if "WOKWI_CLI_TOKEN" not in env:
        tok = pathlib.Path.home() / ".wokwi_token"
        if tok.exists():
            env["WOKWI_CLI_TOKEN"] = tok.read_text().strip()
    return env


def make_diagram(name, channels):
    d = json.loads((ROOT / "diagram.json").read_text())
    for p in d["parts"]:
        if p["id"] == "logic":
            p["attrs"]["channelNames"] = ",".join(channels + [f"D{i}" for i in range(len(channels), 8)])
    d["connections"] = [c for c in d["connections"] if not c[0].startswith("logic:D")]
    for i, ch in enumerate(channels):
        d["connections"].append([f"logic:D{i}", SIG[ch], "white", []])
    path = OUT / f"{name}.diagram.json"
    path.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n")
    return path


def make_scenario(name, commands, until):
    lines = [f"name: '{name}'", "version: 1", "author: 'tools/capture.py'", "steps:",
             "  - wait-serial: 'READY'", "  - delay: 20ms"]
    for cmd in commands:
        lines += [f'  - write-serial: "{cmd}\\n"']
        if cmd.startswith("mode"):
            lines += [f"  - wait-serial: 'MODE,{cmd.split()[1].upper()}'", "  - delay: 20ms"]
    if commands:
        lines += [f"  - wait-serial: '{until}'"]
    lines += ["  - delay: 5ms"]
    path = OUT / f"{name}.scenario.yaml"
    path.write_text("\n".join(lines) + "\n")
    return path


def decode(name, cfg):
    vcd = OUT / f"{name}.vcd"
    for dec in cfg["decoders"]:
        proto = dec.split(":")[0]
        tag = proto + ("-" + dec.split("=")[1].split(":")[0] if proto in ("pwm", "uart") else "")
        out = OUT / f"{name}.{tag}.txt"
        res = subprocess.run([SIGROK, "-I", f"vcd:downsample={cfg['downsample']}", "-i", str(vcd),
                              "-P", dec, "--protocol-decoder-samplenum", "-A", proto],
                             capture_output=True, text=True)
        out.write_text(res.stdout)
        print(f"   decoded {out.name}: {len(res.stdout.splitlines())} annotations")


def run(name, cfg, elf=None):
    diag = make_diagram(name, cfg["channels"])
    scen = make_scenario(name, cfg["commands"], cfg["until"])
    cmd = [WOKWI, "--quiet", "--timeout", "8000", "--diagram-file", str(diag.relative_to(ROOT)),
           "--scenario", str(scen.relative_to(ROOT)), "--vcd-file", str(OUT / f"{name}.vcd"),
           "--serial-log-file", str(OUT / f"{name}.uart.log")]
    if not cfg["commands"]:
        cmd += ["--expect-text", cfg["until"]]
    if elf:
        cmd += ["--elf", elf]
    print(f"== {name}")
    res = subprocess.run(cmd + ["."], cwd=ROOT, env=token_env(), capture_output=True, text=True)
    if res.returncode not in (0,):
        print(res.stdout[-2000:], res.stderr[-2000:])
        raise SystemExit(f"wokwi-cli failed for {name} (exit {res.returncode})")
    decode(name, cfg)


def main(argv):
    OUT.mkdir(parents=True, exist_ok=True)
    names = argv or list(CAPTURES)
    for n in names:
        run(n, CAPTURES[n], CAPTURES[n].get("elf"))


if __name__ == "__main__":
    main(sys.argv[1:])
