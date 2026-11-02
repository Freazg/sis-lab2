#include "app_internal.h"
#include "app_io.h"

#define SECTOR_MASK (W25Q_SECTOR_SIZE - 1u)
#define TRAJ_END    (APP_TRAJ_BASE + APP_TRAJ_SECTORS * W25Q_SECTOR_SIZE)
#define TEAR_BYTES  100u /* скільки байтів сторінки потрапляє у флеш перед «обривом живлення» */

/* Операції блокувальні для задачі, що їх викликає; інші задачі ОС її витісняють (ЛР4).
 * Кожна операція бере м'ютекс флеш: її ділять traj (REC), servo (PLAY), long (dump, erase), cli. */
static dev_status_t flash_read_page(app_t *a, uint32_t addr)
{
  const app_hw_t *hw = a->hw;
  (void)hw->lock(APP_LOCK_FLASH, APP_WAIT_FOREVER);
  hw->probe(APP_PROBE_LONG, true);
  uint32_t t0 = hw->cycles();
  dev_status_t st = w25q64_read(hw->flash, addr, a->page, TRAJ_PAGE_SIZE);
  perf_add(&a->st[APP_ST_READ], hw->cycles() - t0);
  hw->probe(APP_PROBE_LONG, false);
  hw->unlock(APP_LOCK_FLASH);
  return st;
}

static dev_status_t flash_erase(app_t *a, uint32_t addr)
{
  const app_hw_t *hw = a->hw;
  (void)hw->lock(APP_LOCK_FLASH, APP_WAIT_FOREVER);
  hw->probe(APP_PROBE_LONG, true);
  app_evt(a, APP_EV_ERASE_START);
  uint32_t t0 = hw->cycles();
  dev_status_t st = w25q64_sector_erase(hw->flash, addr);
  perf_add(&a->st[APP_ST_ERASE], hw->cycles() - t0);
  app_evt(a, APP_EV_ERASE_DONE);
  hw->probe(APP_PROBE_LONG, false);
  hw->unlock(APP_LOCK_FLASH);
  return st;
}

static dev_status_t flash_program(app_t *a, uint32_t addr, size_t len)
{
  const app_hw_t *hw = a->hw;
  (void)hw->lock(APP_LOCK_FLASH, APP_WAIT_FOREVER);
  hw->probe(APP_PROBE_LONG, true);
  app_evt(a, APP_EV_PROG_START);
  uint32_t t0 = hw->cycles();
  dev_status_t st = w25q64_page_program(hw->flash, addr, a->page, len);
  perf_add(&a->st[APP_ST_PROG], hw->cycles() - t0);
  app_evt(a, APP_EV_PROG_DONE);
  hw->probe(APP_PROBE_LONG, false);
  hw->unlock(APP_LOCK_FLASH);
  return st;
}

void app_traj_scan(app_t *a, app_traj_info_t *out)
{
  *out = (app_traj_info_t){ 0, 0, 0, 0 };
  for (uint32_t addr = APP_TRAJ_BASE; addr < TRAJ_END; addr += TRAJ_PAGE_SIZE) {
    if (flash_read_page(a, addr) != DEV_OK) {
      return;
    }
    if (!traj_page_valid(a->page)) {
      out->torn = (uint8_t)(a->page[0] != 0xFFu); /* стерта сторінка — це кінець, а не пошкодження */
      return;
    }
    uint16_t session = traj_page_session(a->page);
    if (addr == APP_TRAJ_BASE) {
      out->session = session;
    } else if (session != out->session || traj_page_seq(a->page) != out->pages) {
      return; /* залишки попереднього запису */
    }
    out->pages++;
    out->samples += traj_page_samples(a->page);
  }
}

static void rec_start_page(app_t *a)
{
  traj_enc_begin(&a->enc, a->page, a->traj_session, a->traj_seq);
}

bool app_rec_begin(app_t *a)
{
  const app_hw_t *hw = a->hw;
  uint16_t session = 1;
  if (flash_read_page(a, APP_TRAJ_BASE) == DEV_OK && traj_page_valid(a->page)) {
    session = (uint16_t)(traj_page_session(a->page) + 1u);
  }
  if (flash_erase(a, APP_TRAJ_BASE) != DEV_OK) {
    app_print(hw, "ERR,flash_erase\r\n");
    return false;
  }
  a->traj_session = session;
  a->traj_seq = 0;
  a->traj_addr = APP_TRAJ_BASE;
  a->traj_samples = 0;
  a->traj_t_first = hw->micros();
  a->traj_t_last = a->traj_t_first;
  rec_start_page(a);
  return true;
}

/** Закрити сторінку і записати у флеш. На межі сектора стирає наступний: це і є блокування на tSE. */
static bool rec_flush(app_t *a)
{
  const app_hw_t *hw = a->hw;
  traj_enc_finish(&a->enc);
  if (a->tear_next_page) {
    (void)flash_program(a, a->traj_addr, TEAR_BYTES);
    hw->reset();
    a->tear_next_page = 0; /* на платі сюди керування не повертається */
    return false;
  }
  if (flash_program(a, a->traj_addr, TRAJ_PAGE_SIZE) != DEV_OK) {
    app_print(hw, "ERR,flash_program\r\n");
    return false;
  }
  a->traj_addr += TRAJ_PAGE_SIZE;
  a->traj_seq++;
  if ((a->traj_addr & SECTOR_MASK) == 0u) {
    if (a->traj_addr >= TRAJ_END) {
      return false;
    }
    if (flash_erase(a, a->traj_addr) != DEV_OK) {
      app_print(hw, "ERR,flash_erase\r\n");
      return false;
    }
  }
  return true;
}

void app_rec_sample(app_t *a, traj_sample_t s)
{
  const app_hw_t *hw = a->hw;
  if (!traj_enc_add(&a->enc, s)) {
    if (!rec_flush(a)) {
      a->enc.n = 0; /* нічого дописувати при виході */
      app_set_mode(a, APP_MODE_HOLD);
      return;
    }
    rec_start_page(a);
    (void)traj_enc_add(&a->enc, s);
  }
  a->traj_samples++;
  a->traj_t_last = hw->micros();
}

void app_rec_end(app_t *a)
{
  if (a->enc.n != 0u) {
    (void)rec_flush(a);
  }
  a->enc.n = 0;
  app_print(a->hw, "REC,DONE,session=%u,samples=%lu,pages=%u,dur_us=%lu\r\n", a->traj_session,
            (unsigned long)a->traj_samples, a->traj_seq, (unsigned long)(a->traj_t_last - a->traj_t_first));
}

static bool play_load_page(app_t *a)
{
  if (a->traj_addr >= TRAJ_END || flash_read_page(a, a->traj_addr) != DEV_OK || !traj_page_valid(a->page)
      || traj_page_session(a->page) != a->traj_session || traj_page_seq(a->page) != a->traj_seq) {
    return false;
  }
  traj_dec_begin(&a->dec, a->page);
  a->traj_addr += TRAJ_PAGE_SIZE;
  a->traj_seq++;
  return true;
}

bool app_play_begin(app_t *a)
{
  const app_hw_t *hw = a->hw;
  if (flash_read_page(a, APP_TRAJ_BASE) != DEV_OK || !traj_page_valid(a->page)) {
    return false;
  }
  a->traj_session = traj_page_session(a->page);
  a->play_done = 0;
  a->traj_addr = APP_TRAJ_BASE;
  a->traj_seq = 0;
  a->traj_samples = 0;
  if (!play_load_page(a)) {
    return false;
  }
  a->traj_t_first = hw->micros();
  a->traj_t_last = a->traj_t_first;
  return true;
}

bool app_play_sample(app_t *a)
{
  const app_hw_t *hw = a->hw;
  traj_sample_t s;
  while (!traj_dec_next(&a->dec, &s)) {
    if (!play_load_page(a)) {
      return false;
    }
  }
  hw->probe(APP_PROBE_PWM, true);
  servo_set_cdeg(hw->servo_pan, (int32_t)s.pan * 10);
  servo_set_cdeg(hw->servo_tilt, (int32_t)s.tilt * 10);
  hw->probe(APP_PROBE_PWM, false);
  a->traj_samples++;
  a->traj_t_last = hw->micros();
  return true;
}

void app_play_end(app_t *a)
{
  app_print(a->hw, "PLAY,DONE,session=%u,samples=%lu,dur_us=%lu\r\n", a->traj_session,
            (unsigned long)a->traj_samples, (unsigned long)(a->traj_t_last - a->traj_t_first));
}

void app_traj_dump(app_t *a)
{
  const app_hw_t *hw = a->hw;
  uint32_t samples = 0;
  uint32_t pages = 0;
  uint16_t session = 0;
  app_evt(a, APP_EV_DUMP_START);
  hw->probe(APP_PROBE_LONG, true);
  for (uint32_t addr = APP_TRAJ_BASE; addr < TRAJ_END; addr += TRAJ_PAGE_SIZE) {
    (void)hw->lock(APP_LOCK_FLASH, APP_WAIT_FOREVER); /* лише на читання сторінки, друк — без м'ютексу флеш */
    dev_status_t st = w25q64_read(hw->flash, addr, a->dump_page, TRAJ_PAGE_SIZE);
    hw->unlock(APP_LOCK_FLASH);
    if (st != DEV_OK || !traj_page_valid(a->dump_page)) {
      break;
    }
    if (addr == APP_TRAJ_BASE) {
      session = traj_page_session(a->dump_page);
    } else if (traj_page_session(a->dump_page) != session || traj_page_seq(a->dump_page) != pages) {
      break;
    }
    traj_dec_begin(&a->dump_dec, a->dump_page);
    traj_sample_t s;
    while (traj_dec_next(&a->dump_dec, &s)) {
      app_print(hw, "DUMP,%lu,%d,%d\r\n", (unsigned long)samples, s.pan, s.tilt);
      samples++;
    }
    pages++;
  }
  hw->probe(APP_PROBE_LONG, false);
  app_evt(a, APP_EV_DUMP_DONE);
  a->long_busy = 0; /* до рядка END: хост може одразу надіслати наступну довгу команду */
  app_print(hw, "DUMP,END,session=%u,samples=%lu,pages=%lu,crc=OK\r\n", session, (unsigned long)samples,
            (unsigned long)pages);
}

void app_traj_erase(app_t *a)
{
  uint32_t n = 0;
  for (uint32_t addr = APP_TRAJ_BASE; addr < TRAJ_END; addr += W25Q_SECTOR_SIZE) {
    n += flash_erase(a, addr) == DEV_OK;
  }
  a->long_busy = 0;
  app_print(a->hw, "ERASE,DONE,sectors=%lu\r\n", (unsigned long)n);
}
