#!/usr/bin/env python3
"""
Сценарій 6.2.6: тривала робота ≥ 1 год у режимі S2 через VS Code + Wokwi.

Wokwi CI на безкоштовному плані обмежений 5 хв на сесію (docs.wokwi.com/wokwi-ci/getting-started),
тому годинний прогін іде через симулятор у VS Code: wokwi.toml відкриває серійний порт на TCP 4000.
Кроки: F1 -> Wokwi: Start Simulator, потім  python3 tools/long_run.py [секунд]

Цикл: запис 5 хв, відтворення 5 хв; після кожного етапу stats і mem. Наприкінці програмне скидання
і dump: журнал у флеш має читатись із правильною CRC. Рух сенсора тут задати нема чим (автоматизація
Wokwi працює лише в CLI), тому траєкторія статична; вимірюються час, пам'ять і лічильники.
"""
import pathlib
import socket
import sys
import time

LOG = pathlib.Path(__file__).resolve().parent.parent / "docs" / "metrics" / "runs" / "long.log"
HOST, PORT = "localhost", 4000


class Link:
    def __init__(self):
        self.s = socket.create_connection((HOST, PORT), timeout=10)
        self.s.settimeout(0.2)
        self.f = LOG.open("a", encoding="utf-8", errors="replace")
        self.buf = ""

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                data = self.s.recv(4096)
            except socket.timeout:
                continue
            if not data:
                raise SystemExit("з'єднання закрито")
            data = bytes(b for b in data if b < 0x80)  # без керувальних послідовностей RFC 2217
            text = data.decode("ascii", "replace")
            self.f.write(text)
            self.f.flush()
            self.buf = (self.buf + text)[-4000:]

    def cmd(self, text, settle=1.0):
        self.s.sendall(text.encode() + b"\n")
        self.pump(settle)


def main():
    total = int(sys.argv[1]) if len(sys.argv) > 1 else 3700
    LOG.parent.mkdir(parents=True, exist_ok=True)
    LOG.write_text(f"# long_run: {time.ctime()}\n", encoding="utf-8")
    link = Link()
    link.cmd("reset", 2.0)
    t0 = time.time()
    cycle = 0
    while time.time() - t0 < total:
        cycle += 1
        link.cmd("mode rec")
        link.cmd("stats reset")
        link.pump(300)
        link.cmd("mode hold")
        link.cmd("stats", 3)
        link.cmd("mem")
        link.cmd("stats reset")
        link.cmd("mode play")
        link.pump(300)
        link.cmd("stats", 3)
        link.cmd("mem")
        link.cmd("status")
        print(f"цикл {cycle}: {time.time() - t0:.0f} с", flush=True)
    link.cmd("reset", 3.0)
    link.cmd("mode hold")
    link.cmd("dump", 90)
    print("готово", flush=True)


if __name__ == "__main__":
    main()
