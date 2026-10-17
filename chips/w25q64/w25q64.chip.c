/*
 * Wokwi custom chip: Winbond W25Q64JV, 64 Mbit SPI NOR flash.
 *
 * Модель поведінки за datasheet W25Q64JV Revision J (27.03.2018):
 *  - SPI mode 0 / 3, команда стартує по спаду /CS, MSB first (§6.1, §8.1 p.21);
 *  - Manufacturer/Device ID: 9Fh -> EFh 40h 17h; 90h -> EFh 16h; ABh -> 16h (p.21);
 *  - Status Register-1: BUSY = S0, WEL = S1 (§7.1);
 *  - Write Enable 06h обов'язковий перед Page Program / Erase, WEL скидається
 *    після завершення операції (§8.2.1 p.24);
 *  - Page Program 02h: 1..256 байт, адреса загортається в межах сторінки,
 *    команда виконується лише якщо /CS піднято на межі байта (p.35);
 *  - Sector Erase 20h (4 KB), Block Erase 52h/D8h, Chip Erase C7h/60h;
 *  - під час BUSY приймаються лише команди читання статусу (§7.1.1);
 *  - час операцій — типові значення з табл. AC Electrical Characteristics (§9.6 p.64):
 *    tPP = 0.4 ms, tSE = 45 ms, tBE1 = 120 ms, tBE2 = 150 ms, tCE = 20 s.
 *
 * Wokwi обмежує пам'ять чипа 64 сторінками WASM (4 МБ), тому 8 МБ масиву не вміщаються.
 * Модель зберігає лише сектори (4 КБ), у які щось програмувалося: таблиця
 * «сектор -> слот» + пул із MAX_SLOTS слотів. Невиділений сектор читається як 0xFF
 * (стерта флеш), стирання сектора звільняє слот.
 */
#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FLASH_SIZE   (8u * 1024u * 1024u)
#define PAGE_SIZE    256u
#define SECTOR_SIZE  4096u

#define T_PP_US   400u
#define T_SE_US   45000u
#define T_BE1_US  120000u
#define T_BE2_US  150000u
#define T_CE_US   20000000u

#define SECTORS    (FLASH_SIZE / SECTOR_SIZE) /* 2048 */
#define MAX_SLOTS  128u                          /* до 512 КБ записаних даних */
#define NO_SLOT    0xFFFFu

static uint16_t sector_slot[SECTORS];
static uint8_t slots[MAX_SLOTS][SECTOR_SIZE];
static uint8_t slot_used[MAX_SLOTS];

static void mem_reset(void)
{
  for (uint32_t i = 0; i < SECTORS; i++) {
    sector_slot[i] = NO_SLOT;
  }
  memset(slot_used, 0, sizeof slot_used);
}

static uint8_t *sector_ptr(uint32_t sector, bool alloc)
{
  if (sector_slot[sector] != NO_SLOT) {
    return slots[sector_slot[sector]];
  }
  if (!alloc) {
    return NULL;
  }
  for (uint32_t k = 0; k < MAX_SLOTS; k++) {
    if (!slot_used[k]) {
      slot_used[k] = 1;
      sector_slot[sector] = (uint16_t)k;
      memset(slots[k], 0xFF, SECTOR_SIZE);
      return slots[k];
    }
  }
  printf("W25Q64 model: out of sector slots\n");
  return NULL;
}

static void erase_range(uint32_t base, uint32_t size)
{
  for (uint32_t s = base / SECTOR_SIZE; s < (base + size) / SECTOR_SIZE; s++) {
    if (sector_slot[s] != NO_SLOT) {
      slot_used[sector_slot[s]] = 0;
      sector_slot[s] = NO_SLOT;
    }
  }
}

static void program_byte(uint32_t a, uint8_t v)
{
  a %= FLASH_SIZE;
  uint8_t *p = sector_ptr(a / SECTOR_SIZE, true);
  if (p != NULL) {
    p[a % SECTOR_SIZE] &= v; /* програмування лише скидає біти 1 -> 0 */
  }
}

typedef struct {
  pin_t cs;
  spi_dev_t spi;
  timer_t busy_timer;
  uint8_t buf[1];

  bool busy;
  bool wel;
  bool power_down;
  bool reset_enabled;

  uint8_t cmd;
  uint32_t idx;       /* номер байта в поточній транзакції */
  uint32_t addr;
  uint8_t page[PAGE_SIZE];
  uint32_t page_len;  /* скільки байт даних прийнято для 02h */
  bool ignore;        /* команда ігнорується (BUSY / power-down) */
} chip_t;

static uint8_t rd(uint32_t a)
{
  a %= FLASH_SIZE;
  const uint8_t *p = sector_ptr(a / SECTOR_SIZE, false);
  return p ? p[a % SECTOR_SIZE] : 0xFF;
}

static uint8_t sr1(chip_t *c) { return (uint8_t)((c->busy ? 0x01 : 0) | (c->wel ? 0x02 : 0)); }

static void start_busy(chip_t *c, uint32_t us)
{
  c->busy = true;
  timer_start(c->busy_timer, us, false);
}

static void on_busy_done(void *user_data)
{
  chip_t *c = user_data;
  c->busy = false;
  c->wel = false;
  printf("W25Q64: operation complete (BUSY=0, WEL=0)\n");
}

static bool is_status_read(uint8_t cmd) { return cmd == 0x05 || cmd == 0x35 || cmd == 0x15; }

/* Обробити прийнятий байт, повернути байт, який буде видано на DO в наступному слоті. */
static uint8_t process(chip_t *c, uint8_t in)
{
  uint32_t i = c->idx++;

  if (i == 0) {
    c->cmd = in;
    c->addr = 0;
    c->page_len = 0;
    c->ignore = (c->busy && !is_status_read(in)) || (c->power_down && in != 0xAB);
    if (c->ignore) {
      printf("W25Q64: cmd %02Xh ignored (%s)\n", in, c->busy ? "BUSY" : "power-down");
      return 0xFF;
    }
    switch (in) {
      case 0x05: return sr1(c);
      case 0x35: return 0x02; /* SR2: QE=1 (заводське значення для IQ-версії) */
      case 0x15: return 0x60; /* SR3: DRV1:0 = 11b */
      case 0x9F: return 0xEF;
      default:   return 0xFF;
    }
  }
  if (c->ignore) {
    return 0xFF;
  }

  switch (c->cmd) {
    case 0x05: return sr1(c);
    case 0x35: return 0x02;
    case 0x15: return 0x60;
    case 0x9F: {
      static const uint8_t id[3] = { 0xEF, 0x40, 0x17 };
      return (i < 3) ? id[i] : 0xFF;
    }
    case 0x90: /* 3 байти адреси 000000h, далі EFh 16h ... */
      return (i < 3) ? 0xFF : ((i - 3) % 2 == 0 ? 0xEF : 0x16);
    case 0xAB: /* 3 dummy, далі Device ID 16h */
      return (i < 3) ? 0xFF : 0x16;
    case 0x4B: { /* 4 dummy, далі 64-bit Unique ID */
      static const uint8_t uid[8] = { 0xD2, 0x63, 0x10, 0x58, 0x17, 0x3A, 0x45, 0x2C };
      return (i < 4) ? 0xFF : uid[(i - 4) % 8];
    }
    case 0x03:
    case 0x0B:
      if (i <= 3) {
        c->addr = (c->addr << 8) | in;
        if (i < 3 || c->cmd == 0x0B) {
          return 0xFF;
        }
        return rd(c->addr++);
      }
      if (c->cmd == 0x0B && i == 4) { /* dummy byte */
        return rd(c->addr++);
      }
      return rd(c->addr++);
    case 0x02:
      if (i <= 3) {
        c->addr = (c->addr << 8) | in;
        return 0xFF;
      }
      /* дані: загортання в межах сторінки (p.35) */
      c->page[(c->addr + c->page_len) % PAGE_SIZE] = in;
      if (c->page_len < PAGE_SIZE) {
        c->page_len++;
      }
      return 0xFF;
    case 0x20:
    case 0x52:
    case 0xD8:
      if (i <= 3) {
        c->addr = (c->addr << 8) | in;
      }
      return 0xFF;
    default:
      return 0xFF;
  }
}

/* Виконати команду після підйому /CS (лише якщо /CS піднято на межі байта). */
static void finalize(chip_t *c)
{
  if (c->idx == 0 || c->ignore) {
    return;
  }
  uint32_t n = c->idx;
  switch (c->cmd) {
    case 0x06:
      c->wel = true;
      printf("W25Q64: WREN -> WEL=1\n");
      break;
    case 0x04:
      c->wel = false;
      printf("W25Q64: WRDI -> WEL=0\n");
      break;
    case 0x02:
      if (!c->wel) {
        printf("W25Q64: PAGE PROGRAM rejected: WEL=0 (no WREN)\n");
        break;
      }
      if (n < 5) {
        break;
      }
      {
        uint32_t page_base = c->addr & ~(PAGE_SIZE - 1u);
        uint32_t start = c->addr % PAGE_SIZE;
        uint32_t len = c->page_len;
        for (uint32_t k = 0; k < len; k++) {
          uint32_t off = (start + k) % PAGE_SIZE;
          program_byte(page_base + off, c->page[off]);
        }
        printf("W25Q64: PAGE PROGRAM %06Xh, %u byte(s)\n", (unsigned)c->addr, (unsigned)len);
        start_busy(c, T_PP_US);
      }
      break;
    case 0x20:
    case 0x52:
    case 0xD8: {
      if (!c->wel) {
        printf("W25Q64: ERASE %02Xh rejected: WEL=0 (no WREN)\n", c->cmd);
        break;
      }
      if (n != 4) {
        break;
      }
      uint32_t size = c->cmd == 0x20 ? SECTOR_SIZE : (c->cmd == 0x52 ? 32768u : 65536u);
      uint32_t base = (c->addr % FLASH_SIZE) & ~(size - 1u);
      erase_range(base, size);
      printf("W25Q64: ERASE %u KB at %06Xh\n", (unsigned)(size / 1024u), (unsigned)base);
      start_busy(c, c->cmd == 0x20 ? T_SE_US : (c->cmd == 0x52 ? T_BE1_US : T_BE2_US));
      break;
    }
    case 0xC7:
    case 0x60:
      if (c->wel && n == 1) {
        mem_reset();
        printf("W25Q64: CHIP ERASE\n");
        start_busy(c, T_CE_US);
      }
      break;
    case 0xB9:
      c->power_down = true;
      break;
    case 0xAB:
      c->power_down = false;
      break;
    case 0x66:
      c->reset_enabled = true;
      break;
    case 0x99:
      if (c->reset_enabled) {
        c->wel = false;
        c->power_down = false;
        printf("W25Q64: software reset\n");
      }
      break;
    case 0x9F:
      printf("W25Q64: JEDEC ID read\n");
      break;
    default:
      break;
  }
  if (c->cmd != 0x66) {
    c->reset_enabled = false;
  }
}

static void on_spi_done(void *user_data, uint8_t *buffer, uint32_t count)
{
  chip_t *c = user_data;
  if (count == 0) {
    return;
  }
  uint8_t out = process(c, buffer[0]);
  if (pin_read(c->cs) == LOW) {
    buffer[0] = out;
    spi_start(c->spi, buffer, 1);
  }
}

static void on_cs_change(void *user_data, pin_t pin, uint32_t value)
{
  (void)pin;
  chip_t *c = user_data;
  if (value == LOW) {
    c->idx = 0;
    c->ignore = false;
    c->buf[0] = 0xFF;
    spi_start(c->spi, c->buf, 1);
  } else {
    spi_stop(c->spi);
    finalize(c);
  }
}

void chip_init(void)
{
  static chip_t chip;
  chip_t *c = &chip;
  memset(c, 0, sizeof *c);
  mem_reset();

  c->cs = pin_init("CS", INPUT_PULLUP);

  const spi_config_t spi = {
    .sck = pin_init("CLK", INPUT),
    .mosi = pin_init("DI", INPUT),
    .miso = pin_init("DO", INPUT),
    .mode = 0,
    .done = on_spi_done,
    .user_data = c,
  };
  c->spi = spi_init(&spi);

  const timer_config_t timer = { .callback = on_busy_done, .user_data = c };
  c->busy_timer = timer_init(&timer);

  const pin_watch_config_t watch = { .edge = BOTH, .pin_change = on_cs_change, .user_data = c };
  pin_watch(c->cs, &watch);

  pin_init("WP", INPUT_PULLUP);
  pin_init("HOLD", INPUT_PULLUP);

  printf("W25Q64JV model ready: 8 MB, JEDEC EF 40 17\n");
}
