# Записи для PulseView

Кожен файл — вікно навколо транзакції з відповідного docs/captures/*.vcd (час від 0).
Відкрити з уже налаштованими декодерами:

- **i2c-whoami-burst** (з `i2c-mpu6050-whoami-burst.vcd`, 322693250 … 326331600 нс, 20 MS/s):
  `pulseview -i i2c-whoami-burst.vcd -I vcd:downsample=50 -s i2c-whoami-burst.pvs`
- **i2c-whoami-only** (з `i2c-mpu6050-whoami-burst.vcd`, 322703250 … 323192825 нс, 100 MS/s):
  `pulseview -i i2c-whoami-only.vcd -I vcd:downsample=10 -s i2c-whoami-only.pvs`
- **spi-jedec** (з `spi-w25q64-jedec-wren-program-read.vcd`, 322750750 … 322995075 нс, 200 MS/s):
  `pulseview -i spi-jedec.vcd -I vcd:downsample=5 -s spi-jedec.pvs`
- **spi-wren-program-busy** (з `spi-w25q64-jedec-wren-program-read.vcd`, 326326650 … 327704725 нс, 100 MS/s):
  `pulseview -i spi-wren-program-busy.vcd -I vcd:downsample=10 -s spi-wren-program-busy.pvs`
- **pwm-3-angles** (з `pwm-servo-pan-tilt-3-angles.vcd`, 212707875 … 646748125 нс, 1 MS/s):
  `pulseview -i pwm-3-angles.vcd -I vcd:downsample=1000 -s pwm-3-angles.pvs`
- **uart-payload** (з `uart-vcp-and-ft232rl.vcd`, 319100900 … 330480850 нс, 10 MS/s):
  `pulseview -i uart-payload.vcd -I vcd:downsample=100 -s uart-payload.pvs`
- **i2c-anomaly** (з `i2c-anomaly-nack-restart-hang.vcd`, 323460950 … 353091475 нс, 20 MS/s):
  `pulseview -i i2c-anomaly.vcd -I vcd:downsample=50 -s i2c-anomaly.pvs`
