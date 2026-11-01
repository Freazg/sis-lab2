#include "traj.h"

#include <string.h>

uint16_t traj_crc16(const uint8_t *data, size_t len, uint16_t crc)
{
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static void put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static uint16_t page_crc(const uint8_t *page)
{
  static const uint8_t zero[2] = { 0, 0 };
  uint16_t c = traj_crc16(page, 12, 0xFFFFu);
  c = traj_crc16(zero, 2, c);
  return traj_crc16(page + TRAJ_HDR_SIZE, TRAJ_PAGE_SIZE - TRAJ_HDR_SIZE, c);
}

void traj_enc_begin(traj_enc_t *e, uint8_t *page, uint16_t session, uint16_t seq)
{
  memset(page, 0xFF, TRAJ_PAGE_SIZE);
  page[0] = TRAJ_MAGIC0;
  page[1] = TRAJ_MAGIC1;
  put16(&page[2], session);
  put16(&page[4], seq);
  page[6] = 0;
  page[7] = 0;
  e->page = page;
  e->n = 0;
}

bool traj_enc_add(traj_enc_t *e, traj_sample_t s)
{
  if (e->n == 0) {
    put16(&e->page[8], (uint16_t)s.pan);
    put16(&e->page[10], (uint16_t)s.tilt);
    e->last = s;
    e->n = 1;
    return true;
  }
  int32_t dp = (int32_t)s.pan - e->last.pan;
  int32_t dt = (int32_t)s.tilt - e->last.tilt;
  if (e->n >= TRAJ_PAGE_SAMPLES || dp > 127 || dp < -127 || dt > 127 || dt < -127) {
    return false;
  }
  uint8_t *d = &e->page[TRAJ_HDR_SIZE + 2u * (e->n - 1u)];
  d[0] = (uint8_t)(int8_t)dp;
  d[1] = (uint8_t)(int8_t)dt;
  e->last = s;
  e->n++;
  return true;
}

void traj_enc_finish(traj_enc_t *e)
{
  e->page[6] = (uint8_t)e->n;
  put16(&e->page[12], page_crc(e->page));
}

bool traj_page_valid(const uint8_t *page)
{
  if (page[0] != TRAJ_MAGIC0 || page[1] != TRAJ_MAGIC1 || page[6] == 0 || page[6] > TRAJ_PAGE_SAMPLES) {
    return false;
  }
  return get16(&page[12]) == page_crc(page);
}

uint16_t traj_page_session(const uint8_t *page) { return get16(&page[2]); }
uint16_t traj_page_seq(const uint8_t *page) { return get16(&page[4]); }
uint8_t traj_page_samples(const uint8_t *page) { return page[6]; }

void traj_dec_begin(traj_dec_t *d, const uint8_t *page)
{
  d->page = page;
  d->n = page[6];
  d->idx = 0;
  d->cur.pan = (int16_t)get16(&page[8]);
  d->cur.tilt = (int16_t)get16(&page[10]);
}

bool traj_dec_next(traj_dec_t *d, traj_sample_t *out)
{
  if (d->idx >= d->n) {
    return false;
  }
  if (d->idx > 0) {
    const uint8_t *p = &d->page[TRAJ_HDR_SIZE + 2u * (d->idx - 1u)];
    d->cur.pan = (int16_t)(d->cur.pan + (int8_t)p[0]);
    d->cur.tilt = (int16_t)(d->cur.tilt + (int8_t)p[1]);
  }
  d->idx++;
  *out = d->cur;
  return true;
}
