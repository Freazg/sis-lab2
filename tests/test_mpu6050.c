/* Тести драйвера MPU-6050: перетворення одиниць, ініціалізація, обробка помилок шини. */
#include "unity.h"

#include "capture_vectors.h"
#include "fake_i2c.h"
#include "fake_time.h"
#include "mpu6050.h"

static fake_i2c_t fake;
static i2c_bus_t bus;
static mpu6050_t dev;

void setUp(void)
{
  fake_i2c_init_mpu6050(&fake);
  bus = fake_i2c_bus(&fake);
  fake_time_reset();
}

void tearDown(void) {}

/* ---- Перетворення сирих даних у фізичні одиниці ------------------------- */

static void test_accel_full_scale_points(void)
{
  /* [PS] §6.2 p.13: AFS_SEL=0 -> 16384 LSB/g */
  TEST_ASSERT_EQUAL_INT32(1000, mpu6050_accel_mg(16384));
  TEST_ASSERT_EQUAL_INT32(-1000, mpu6050_accel_mg(-16384));
  TEST_ASSERT_EQUAL_INT32(500, mpu6050_accel_mg(8192));
  TEST_ASSERT_EQUAL_INT32(0, mpu6050_accel_mg(0));
  TEST_ASSERT_EQUAL_INT32(2000, mpu6050_accel_mg(32767));  /* ~ +2 g, межа діапазону */
  TEST_ASSERT_EQUAL_INT32(-2000, mpu6050_accel_mg(-32768));
}

static void test_accel_rounding_is_symmetric(void)
{
  /* 8 LSB = 0.488 mg -> 0; 9 LSB = 0.549 mg -> 1; знак не впливає на округлення */
  TEST_ASSERT_EQUAL_INT32(0, mpu6050_accel_mg(8));
  TEST_ASSERT_EQUAL_INT32(1, mpu6050_accel_mg(9));
  TEST_ASSERT_EQUAL_INT32(-1, mpu6050_accel_mg(-9));
}

static void test_gyro_sensitivity(void)
{
  /* [PS] §6.1 p.12: FS_SEL=0 -> 131 LSB/(°/s) */
  TEST_ASSERT_EQUAL_INT32(1000, mpu6050_gyro_mdps(131));
  TEST_ASSERT_EQUAL_INT32(-250115, mpu6050_gyro_mdps(-32765)); /* ≈ -250 °/с */
  TEST_ASSERT_EQUAL_INT32(0, mpu6050_gyro_mdps(0));
}

static void test_temperature_datasheet_example(void)
{
  /* [PS] §6.3 p.14: «Temperature Offset 35 °C = -521 LSB», формула [RM] p.31:
   * T = raw/340 + 36.53 -> -521/340 + 36.53 = 34.998 °C */
  TEST_ASSERT_EQUAL_INT32(3500, mpu6050_temp_cdeg(-521));
  TEST_ASSERT_EQUAL_INT32(3653, mpu6050_temp_cdeg(0));     /* raw 0 -> 36.53 °C */
  TEST_ASSERT_EQUAL_INT32(3753, mpu6050_temp_cdeg(340));   /* +1 °C на 340 LSB */
}

static void test_parse_big_endian_and_sign(void)
{
  const uint8_t b[14] = { 0x80, 0x00, 0x7F, 0xFF, 0x00, 0x01, 0xFD, 0xF7, 0xFF, 0x7D, 0x00, 0x83, 0x12, 0x34 };
  mpu6050_raw_t r;
  mpu6050_parse(b, &r);
  TEST_ASSERT_EQUAL_INT16(-32768, r.accel[0]);
  TEST_ASSERT_EQUAL_INT16(32767, r.accel[1]);
  TEST_ASSERT_EQUAL_INT16(1, r.accel[2]);
  TEST_ASSERT_EQUAL_INT16(-521, r.temp);
  TEST_ASSERT_EQUAL_INT16(-131, r.gyro[0]);
  TEST_ASSERT_EQUAL_INT16(131, r.gyro[1]);
  TEST_ASSERT_EQUAL_INT16(0x1234, r.gyro[2]);
}

static void test_capture_vector_matches_uart_log(void)
{
  /* Байти з docs/captures/i2c-mpu6050-whoami-burst (декодовано sigrok-cli) ->
   * ті самі значення, що прошивка надрукувала в UART під час запису. */
  mpu6050_raw_t r;
  mpu6050_data_t v;
  mpu6050_parse(cap_i2c_burst, &r);
  mpu6050_convert(&r, &v);
  TEST_ASSERT_EQUAL_INT32_ARRAY(cap_i2c_expect_acc_mg, v.accel_mg, 3);
  TEST_ASSERT_EQUAL_INT32_ARRAY(cap_i2c_expect_gyro_mdps, v.gyro_mdps, 3);
  TEST_ASSERT_EQUAL_INT32(CAP_I2C_EXPECT_TEMP_CDEG, v.temp_cdeg);
  TEST_ASSERT_EQUAL_HEX8(MPU6050_WHO_AM_I_VALUE, CAP_I2C_WHO_AM_I);
}

static void test_norm_at_rest_is_one_g(void)
{
  mpu6050_data_t v = { { 0, 0, 1000 }, { 0 }, 0 };
  TEST_ASSERT_EQUAL_UINT32(1000, mpu6050_accel_norm_mg(&v));
  mpu6050_data_t t = { { 600, 0, 800 }, { 0 }, 0 }; /* нахил: 0.6 g / 0.8 g */
  TEST_ASSERT_EQUAL_UINT32(1000, mpu6050_accel_norm_mg(&t));
  mpu6050_data_t n = { { -577, -577, -577 }, { 0 }, 0 };
  TEST_ASSERT_UINT32_WITHIN(1, 999, mpu6050_accel_norm_mg(&n));
}

/* ---- Ідентифікація та ініціалізація ------------------------------------- */

static void test_probe_ok(void)
{
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_probe(&dev, &bus, &fake_time, MPU6050_ADDR_AD0_LOW));
  TEST_ASSERT_EQUAL_HEX8(0x68, dev.who_am_i);
  TEST_ASSERT_EQUAL(1, fake.log[0].is_read);
  TEST_ASSERT_EQUAL_HEX8(MPU6050_REG_WHO_AM_I, fake.log[0].reg);
}

static void test_probe_nack_on_wrong_address(void)
{
  /* AD0 = 1 -> 0x69, а на шині пристрій 0x68: шина повертає NACK */
  TEST_ASSERT_EQUAL(DEV_ERR_NACK, mpu6050_probe(&dev, &bus, &fake_time, MPU6050_ADDR_AD0_HIGH));
}

static void test_probe_unexpected_id(void)
{
  fake.regs[0x75] = 0x70; /* напр. MPU-6500 / підробка */
  TEST_ASSERT_EQUAL(DEV_ERR_ID, mpu6050_probe(&dev, &bus, &fake_time, MPU6050_ADDR_AD0_LOW));
  TEST_ASSERT_EQUAL_HEX8(0x70, dev.who_am_i);
}

static void test_probe_timeout_propagates(void)
{
  fake.fail_on_call = 1;
  fake.fail_status = DEV_ERR_TIMEOUT;
  TEST_ASSERT_EQUAL(DEV_ERR_TIMEOUT, mpu6050_probe(&dev, &bus, &fake_time, MPU6050_ADDR_AD0_LOW));
}

static void test_probe_null_args(void)
{
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, mpu6050_probe(NULL, &bus, &fake_time, 0x68));
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, mpu6050_probe(&dev, NULL, &fake_time, 0x68));
}

static void test_init_configures_registers(void)
{
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_probe(&dev, &bus, &fake_time, 0x68));
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_init(&dev));
  TEST_ASSERT_EQUAL_HEX8(0x01, fake.regs[MPU6050_REG_PWR_MGMT_1]); /* SLEEP=0, CLKSEL=PLL X */
  TEST_ASSERT_EQUAL_HEX8(0x03, fake.regs[MPU6050_REG_CONFIG]);
  TEST_ASSERT_EQUAL_HEX8(0x04, fake.regs[MPU6050_REG_SMPLRT_DIV]);   /* 1 кГц/(1+4) = 200 Гц */
  TEST_ASSERT_EQUAL_HEX8(0x00, fake.regs[MPU6050_REG_GYRO_CONFIG]);
  TEST_ASSERT_EQUAL_HEX8(0x00, fake.regs[MPU6050_REG_ACCEL_CONFIG]);
  TEST_ASSERT_EQUAL_UINT32(100, fake_time_total_delay_ms); /* старт ≤100 мс після DEVICE_RESET */
  /* перша транзакція init — DEVICE_RESET (0x6B <- 0x80) */
  TEST_ASSERT_EQUAL(0, fake.log[1].is_read);
  TEST_ASSERT_EQUAL_HEX8(MPU6050_REG_PWR_MGMT_1, fake.log[1].reg);
}

static void test_init_nack_midway(void)
{
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_probe(&dev, &bus, &fake_time, 0x68));
  fake.fail_on_call = fake.calls + 3; /* третій запис init */
  fake.fail_status = DEV_ERR_NACK;
  TEST_ASSERT_EQUAL(DEV_ERR_NACK, mpu6050_init(&dev));
}

static void test_init_reset_write_fails(void)
{
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_probe(&dev, &bus, &fake_time, 0x68));
  fake.fail_on_call = fake.calls + 1;
  fake.fail_status = DEV_ERR_BUS;
  TEST_ASSERT_EQUAL(DEV_ERR_BUS, mpu6050_init(&dev));
}

static void test_init_readback_error(void)
{
  /* помилка на контрольному читанні PWR_MGMT_1 передається нагору */
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_probe(&dev, &bus, &fake_time, 0x68));
  fake.fail_on_call = fake.calls + 7; /* читання PWR_MGMT_1 після 6 записів */
  fake.fail_status = DEV_ERR_TIMEOUT;
  TEST_ASSERT_EQUAL(DEV_ERR_TIMEOUT, mpu6050_init(&dev));
}

static void test_read_raw_burst_single_transaction(void)
{
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_probe(&dev, &bus, &fake_time, 0x68));
  fake_i2c_set_burst(&fake, cap_i2c_burst);
  int before = fake.log_len;
  mpu6050_raw_t r;
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_read_raw(&dev, &r));
  TEST_ASSERT_EQUAL(before + 1, fake.log_len); /* одна транзакція, без 14 окремих читань */
  TEST_ASSERT_EQUAL_HEX8(MPU6050_REG_ACCEL_XOUT_H, fake.log[before].reg);
  TEST_ASSERT_EQUAL(14, fake.log[before].len);
  TEST_ASSERT_EQUAL_INT16(16384, r.accel[2]);
}

static void test_read_raw_error_leaves_output_untouched(void)
{
  TEST_ASSERT_EQUAL(DEV_OK, mpu6050_probe(&dev, &bus, &fake_time, 0x68));
  fake.fail_on_call = fake.calls + 1;
  fake.fail_status = DEV_ERR_NACK;
  mpu6050_raw_t r = { { 7, 7, 7 }, 7, { 7, 7, 7 } };
  TEST_ASSERT_EQUAL(DEV_ERR_NACK, mpu6050_read_raw(&dev, &r));
  TEST_ASSERT_EQUAL_INT16(7, r.accel[0]);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_accel_full_scale_points);
  RUN_TEST(test_accel_rounding_is_symmetric);
  RUN_TEST(test_gyro_sensitivity);
  RUN_TEST(test_temperature_datasheet_example);
  RUN_TEST(test_parse_big_endian_and_sign);
  RUN_TEST(test_capture_vector_matches_uart_log);
  RUN_TEST(test_norm_at_rest_is_one_g);
  RUN_TEST(test_probe_ok);
  RUN_TEST(test_probe_nack_on_wrong_address);
  RUN_TEST(test_probe_unexpected_id);
  RUN_TEST(test_probe_timeout_propagates);
  RUN_TEST(test_probe_null_args);
  RUN_TEST(test_init_configures_registers);
  RUN_TEST(test_init_nack_midway);
  RUN_TEST(test_init_reset_write_fails);
  RUN_TEST(test_init_readback_error);
  RUN_TEST(test_read_raw_burst_single_transaction);
  RUN_TEST(test_read_raw_error_leaves_output_untouched);
  return UNITY_END();
}
