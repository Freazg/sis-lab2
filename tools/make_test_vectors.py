#!/usr/bin/env python3
"""
Тестові вектори з реальних записів логічного аналізатора (методичка ЛР2, п. 4.6):
байти, побачені на шині (декодовані sigrok-cli), + значення, які прошивка в той самий
момент надрукувала в UART. Тест перевіряє, що драйвер, отримавши ці байти, дає ті самі
значення, а для SPI — що драйвер видає на MOSI ту саму послідовність, що на записі.

Вихід: tests/vectors/capture_vectors.h (згенерований файл, не редагувати вручну).
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAP = ROOT / "docs" / "captures"
OUT = ROOT / "tests" / "vectors" / "capture_vectors.h"

ANN = re.compile(r"^(\d+)-(\d+) \S+: (.*)$")


def ann(path):
    for line in path.read_text().splitlines():
        m = ANN.match(line)
        if m:
            yield int(m.group(1)), int(m.group(2)), m.group(3)


def uart_value(log, prefix):
    for line in log.read_text().replace("\r", "\n").splitlines():
        i = line.find(prefix)
        if i >= 0:
            return line[i + len(prefix):]
    raise SystemExit(f"'{prefix}' not found in {log.name}")


def i2c_vectors():
    name = "i2c-mpu6050-whoami-burst"
    rows = list(ann(CAP / f"{name}.i2c.txt"))
    # остання транзакція читання 0x68 з 14 байтами = cap i2c burst
    reads = [i for i, r in enumerate(rows) if r[2] == "Address read: 68"]
    burst = None
    who = None
    for i in reversed(reads):
        data = []
        for _, _, t in rows[i + 1:]:
            if t.startswith("Data read: "):
                data.append(int(t.split(": ")[1], 16))
            elif t == "Stop":
                break
        if len(data) == 14 and burst is None:
            burst = data
        elif len(data) == 1 and who is None and burst is not None:
            who = data[0]
        if burst and who is not None:
            break
    log = CAP / f"{name}.uart.log"
    exp = uart_value(log, "CAP,i2c,acc_mg=")  # 0/0/1000,temp_cdeg=2400,gyro_mdps=0/0/0
    acc, rest = exp.split(",temp_cdeg=")
    temp, gyro = rest.split(",gyro_mdps=")
    burst_hex = uart_value(log, "CAP,i2c,burst=").split(",")[0]
    assert burst_hex == "".join(f"{b:02X}" for b in burst), "UART-лог і запис шини не збігаються"
    return who, burst, [int(x) for x in acc.split("/")], int(temp), [int(x) for x in gyro.split("/")]


def spi_transfers(name):
    rows = list(ann(CAP / f"{name}.spi-transfer.txt"))
    out = []
    for k in range(0, len(rows) - 1, 2):
        a, b = rows[k], rows[k + 1]
        assert (a[0], a[1]) == (b[0], b[1])
        miso = [int(x, 16) for x in a[2].split()]
        mosi = [int(x, 16) for x in b[2].split()]
        out.append((mosi, miso))
    return out


def spi_vectors():
    name = "spi-w25q64-jedec-wren-program-read"
    tr = spi_transfers(name)
    jedec = [t for t in tr if t[0][0] == 0x9F][-1]
    program = [t for t in tr if t[0][0] == 0x02][-1]
    read = [t for t in tr if t[0][0] == 0x03][-1]
    log = CAP / f"{name}.uart.log"
    read_hex = uart_value(log, "CAP,spi,read=").split(",")[0]
    return jedec, program, read, read_hex


def carr(name, data, typ="uint8_t"):
    body = ", ".join(f"0x{b:02X}" for b in data)
    return f"static const {typ} {name}[{len(data)}] = {{ {body} }};"


def main():
    who, burst, acc, temp, gyro = i2c_vectors()
    jedec, program, read, read_hex = spi_vectors()
    lines = [
        "/* Згенеровано tools/make_test_vectors.py з docs/captures (VCD + декодування sigrok-cli). Не редагувати вручну. */",
        "#ifndef CAPTURE_VECTORS_H", "#define CAPTURE_VECTORS_H", "#include <stdint.h>", "",
        "/* i2c-mpu6050-whoami-burst: відповідь на WHO_AM_I і 14 байт пакетного читання з 0x3B */",
        f"#define CAP_I2C_WHO_AM_I 0x{who:02X}",
        carr("cap_i2c_burst", burst),
        "/* значення, які прошивка надрукувала в UART для цих байтів */",
        f"static const int32_t cap_i2c_expect_acc_mg[3] = {{ {', '.join(map(str, acc))} }};",
        f"static const int32_t cap_i2c_expect_gyro_mdps[3] = {{ {', '.join(map(str, gyro))} }};",
        f"#define CAP_I2C_EXPECT_TEMP_CDEG ({temp})",
        "",
        "/* spi-w25q64-jedec-wren-program-read: транзакції з утриманим CS# (MOSI / MISO) */",
        carr("cap_spi_jedec_mosi", jedec[0]), carr("cap_spi_jedec_miso", jedec[1]),
        carr("cap_spi_program_mosi", program[0]),
        carr("cap_spi_read_mosi", read[0]), carr("cap_spi_read_miso", read[1]),
        f'#define CAP_SPI_READ_HEX "{read_hex}"',
        "", "#endif", "",
    ]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines))
    print(f"written {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
