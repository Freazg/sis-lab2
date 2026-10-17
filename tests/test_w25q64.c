/* Тести драйвера W25Q64: послідовність команд до SPI-flash (mock), обробка помилок. */
#include <string.h>

#include "unity.h"

#include "capture_vectors.h"
#include "fake_time.h"
#include "mock_spi.h"
#include "w25q64.h"

static mock_spi_t m;
static spi_bus_t bus;
static w25q64_t dev;

static const uint8_t jedec_ok[3] = { 0xEF, 0x40, 0x17 };

void setUp(void)
{
  mock_spi_init(&m);
  bus = mock_spi_bus(&m);
  fake_time_reset();
  dev.bus = &bus;
  dev.time = &fake_time;
}

void tearDown(void)
{
  TEST_ASSERT_EQUAL_MESSAGE(0, m.cs_errors, "CS# має бути утриманий протягом кожної транзакції");
  TEST_ASSERT_EQUAL_MESSAGE(0, m.open, "CS# має бути знятий після кожної транзакції");
}

static void expect_txn(int idx, const uint8_t *bytes, uint16_t len)
{
  TEST_ASSERT_TRUE_MESSAGE(idx < m.count, "транзакції не вистачає");
  TEST_ASSERT_EQUAL_UINT16(len, m.txn[idx].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(bytes, m.txn[idx].mosi, len);
}

static void test_probe_reads_jedec(void)
{
  m.resp[0] = jedec_ok;
  m.resp_len[0] = 3;
  TEST_ASSERT_EQUAL(DEV_OK, w25q64_probe(&dev, &bus, &fake_time));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(jedec_ok, dev.jedec, 3);
  TEST_ASSERT_EQUAL(1, m.count);
  TEST_ASSERT_EQUAL_HEX8(W25Q_CMD_JEDEC_ID, m.txn[0].mosi[0]);
  TEST_ASSERT_EQUAL(4, m.txn[0].len); /* 9Fh + 3 байти відповіді в одній транзакції */
}

static void test_probe_matches_capture(void)
{
  /* MISO з запису docs/captures/spi-w25q64-*: після 9Fh чіп видав EF 40 17 */
  m.resp[0] = &cap_spi_jedec_miso[1];
  m.resp_len[0] = 3;
  TEST_ASSERT_EQUAL(DEV_OK, w25q64_probe(&dev, &bus, &fake_time));
  expect_txn(0, cap_spi_jedec_mosi, sizeof cap_spi_jedec_mosi);
}

static void test_probe_wrong_manufacturer(void)
{
  static const uint8_t mx[3] = { 0xC2, 0x20, 0x17 }; /* Macronix */
  m.resp[0] = mx;
  m.resp_len[0] = 3;
  TEST_ASSERT_EQUAL(DEV_ERR_ID, w25q64_probe(&dev, &bus, &fake_time));
}

static void test_probe_no_chip_reads_ff(void)
{
  /* MISO підтягнутий або висить у 1 -> FF FF FF */
  TEST_ASSERT_EQUAL(DEV_ERR_ID, w25q64_probe(&dev, &bus, &fake_time));
  TEST_ASSERT_EQUAL_HEX8(0xFF, dev.jedec[0]);
}

static void test_probe_bus_error(void)
{
  m.fail_on_transfer = 1;
  TEST_ASSERT_EQUAL(DEV_ERR_BUS, w25q64_probe(&dev, &bus, &fake_time));
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, w25q64_probe(NULL, &bus, &fake_time));
}

static void test_page_program_sequence(void)
{
  /* Datasheet p.24, p.35: WREN (06h) -> [контроль WEL через 05h] -> 02h A23..A0 data -> опитування BUSY */
  static const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
  static const uint8_t wren[1] = { 0x06 };
  static const uint8_t rdsr[2] = { 0x05, 0xFF };
  static const uint8_t pp[8] = { 0x02, 0x12, 0x34, 0x00, 0xDE, 0xAD, 0xBE, 0xEF };
  m.busy_polls = 2; /* два опитування після програмування повернуть BUSY=1 */
  TEST_ASSERT_EQUAL(DEV_OK, w25q64_page_program(&dev, 0x123400, data, sizeof data));
  expect_txn(0, wren, 1);
  expect_txn(1, rdsr, 2);
  expect_txn(2, pp, 8);
  /* WEL читався до 02h, далі busy_polls забирає перші два 05h після програмування */
  TEST_ASSERT_EQUAL(6, m.count); /* 06, 05(WEL), 02, 05(BUSY), 05(BUSY), 05(ready) */
  for (int i = 3; i < 6; i++) {
    TEST_ASSERT_EQUAL_HEX8(0x05, m.txn[i].mosi[0]);
  }
}

static void test_page_program_matches_capture(void)
{
  /* Драйвер має видати на MOSI ту саму транзакцію 02h, що зафіксував логічний аналізатор */
  TEST_ASSERT_EQUAL(DEV_OK, w25q64_page_program(&dev, 0x7FF100, &cap_spi_program_mosi[4], 16));
  expect_txn(2, cap_spi_program_mosi, sizeof cap_spi_program_mosi);
}

static void test_page_program_without_wel_is_aborted(void)
{
  m.sr1_after_wren = 0x00; /* WREN не спрацював (напр. /WP або збій лінії) */
  static const uint8_t d[1] = { 0x55 };
  TEST_ASSERT_EQUAL(DEV_ERR_VERIFY, w25q64_page_program(&dev, 0, d, 1));
  TEST_ASSERT_EQUAL(2, m.count); /* лише 06h і 05h, 02h не надсилається */
}

static void test_page_program_rejects_page_crossing(void)
{
  uint8_t d[16] = { 0 };
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, w25q64_page_program(&dev, 0x0000F8, d, 16)); /* 0xF8 + 16 > 0x100 */
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, w25q64_page_program(&dev, 0, d, 0));
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, w25q64_page_program(&dev, 0, NULL, 1));
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, w25q64_page_program(&dev, W25Q64_SIZE - 4, d, 8));
  TEST_ASSERT_EQUAL(0, m.count); /* жодної транзакції на шині */
}

static void test_page_program_busy_timeout(void)
{
  static const uint8_t d[1] = { 0x55 };
  m.busy_polls = 1000;    /* BUSY не знімається */
  fake_time_step_ms = 1;  /* кожне опитування — +1 мс */
  TEST_ASSERT_EQUAL(DEV_ERR_BUSY, w25q64_page_program(&dev, 0, d, 1));
  /* 06, 05(WEL), 02 + опитування BUSY протягом ~tPP max (3 мс при кроці 1 мс) */
  TEST_ASSERT_INT_WITHIN(3, 3 + W25Q_T_PP_MAX_MS, m.count);
}

static void test_sector_erase_sequence_aligns_address(void)
{
  static const uint8_t se[4] = { 0x20, 0x7F, 0xF0, 0x00 };
  TEST_ASSERT_EQUAL(DEV_OK, w25q64_sector_erase(&dev, 0x7FF123));
  TEST_ASSERT_EQUAL_HEX8(0x06, m.txn[0].mosi[0]);
  expect_txn(2, se, 4);
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, w25q64_sector_erase(&dev, W25Q64_SIZE));
}

static void test_read_sequence_and_data(void)
{
  uint8_t buf[16];
  m.resp[0] = &cap_spi_read_miso[4];
  m.resp_len[0] = 16;
  TEST_ASSERT_EQUAL(DEV_OK, w25q64_read(&dev, 0x7FF100, buf, sizeof buf));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(cap_spi_read_mosi, m.txn[0].mosi, 4); /* 03h + 24-біт адреса */
  TEST_ASSERT_EQUAL_HEX8_ARRAY(&cap_spi_read_miso[4], buf, 16);
  TEST_ASSERT_EQUAL(DEV_ERR_PARAM, w25q64_read(&dev, W25Q64_SIZE - 1, buf, 2));
}

static void test_write_enable_bus_error(void)
{
  m.fail_on_transfer = 1;
  TEST_ASSERT_EQUAL(DEV_ERR_BUS, w25q64_write_enable(&dev));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_probe_reads_jedec);
  RUN_TEST(test_probe_matches_capture);
  RUN_TEST(test_probe_wrong_manufacturer);
  RUN_TEST(test_probe_no_chip_reads_ff);
  RUN_TEST(test_probe_bus_error);
  RUN_TEST(test_page_program_sequence);
  RUN_TEST(test_page_program_matches_capture);
  RUN_TEST(test_page_program_without_wel_is_aborted);
  RUN_TEST(test_page_program_rejects_page_crossing);
  RUN_TEST(test_page_program_busy_timeout);
  RUN_TEST(test_sector_erase_sequence_aligns_address);
  RUN_TEST(test_read_sequence_and_data);
  RUN_TEST(test_write_enable_bus_error);
  return UNITY_END();
}
