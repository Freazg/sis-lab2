/**
 * @file    traj.h
 * @brief   Формат траєкторії у W25Q64 (FR-07, FR-08): Δ-кодування + keyframe на кожну сторінку.
 *
 * Сторінка 256 байт — одиниця запису і цілісності:
 *   0-1  магічне "TJ"
 *   2-3  сесія запису, 4-5 номер сторінки в сесії (LE)
 *   6    кількість семплів у сторінці (1..TRAJ_PAGE_SAMPLES)
 *   7    резерв (0)
 *   8-9, 10-11  keyframe pan/tilt, одиниці 0.1°, int16 LE
 *   12-13 CRC-16/CCITT-FALSE усієї сторінки, де байти 12-13 рахуються як 0
 *   14.. пари int8: Δpan, Δtilt до попереднього семпла, одиниці 0.1°
 * 300 °/с при 50 Гц дають до 6° = 60 одиниць на крок, тобто int8 вистачає з запасом. Більший
 * стрибок закриває сторінку, і наступний семпл стає новим keyframe. Сторінка з невірною CRC
 * (обрив живлення під час запису) відкидається цілком, попередні лишаються цілими.
 */
#ifndef TRAJ_H
#define TRAJ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRAJ_PAGE_SIZE     256u
#define TRAJ_HDR_SIZE      14u
#define TRAJ_PAGE_SAMPLES  (1u + (TRAJ_PAGE_SIZE - TRAJ_HDR_SIZE) / 2u) /* keyframe + дельти */
#define TRAJ_MAGIC0 'T'
#define TRAJ_MAGIC1 'J'

typedef struct {
  int16_t pan;  /**< 0.1° */
  int16_t tilt;
} traj_sample_t;

typedef struct {
  uint8_t *page;
  traj_sample_t last;
  uint16_t n;
} traj_enc_t;

typedef struct {
  const uint8_t *page;
  traj_sample_t cur;
  uint16_t n;
  uint16_t idx;
} traj_dec_t;

uint16_t traj_crc16(const uint8_t *data, size_t len, uint16_t crc);

void traj_enc_begin(traj_enc_t *e, uint8_t *page, uint16_t session, uint16_t seq);
/** Додати семпл. false — у сторінку він не вміщається: закрити її і почати нову. */
bool traj_enc_add(traj_enc_t *e, traj_sample_t s);
/** Записати кількість семплів і CRC. Решта сторінки — 0xFF (стан стертої флеш). */
void traj_enc_finish(traj_enc_t *e);

/** Перевірити сторінку: магічне слово, кількість, CRC. */
bool traj_page_valid(const uint8_t *page);
uint16_t traj_page_session(const uint8_t *page);
uint16_t traj_page_seq(const uint8_t *page);
uint8_t traj_page_samples(const uint8_t *page);

void traj_dec_begin(traj_dec_t *d, const uint8_t *page);
/** Наступний семпл сторінки; false, якщо сторінка закінчилась. */
bool traj_dec_next(traj_dec_t *d, traj_sample_t *out);

#endif /* TRAJ_H */
