/* Тести формату траєкторії: Δ-кодування, keyframe, CRC, розрив сторінки (FR-07, FR-08). */
#include <string.h>

#include "unity.h"

#include "traj.h"

static uint8_t page[TRAJ_PAGE_SIZE];

void setUp(void) { memset(page, 0, sizeof page); }
void tearDown(void) {}

static void test_crc16_ccitt_false_check_value(void)
{
  /* контрольне значення CRC-16/CCITT-FALSE для "123456789" */
  TEST_ASSERT_EQUAL_HEX16(0x29B1, traj_crc16((const uint8_t *)"123456789", 9, 0xFFFFu));
}

static void test_page_holds_122_samples_for_50hz(void)
{
  TEST_ASSERT_EQUAL_UINT32(122, TRAJ_PAGE_SAMPLES);
  /* 60 с × 50 Гц = 3000 семплів = 25 сторінок = 6.4 КБ: FR-07 вміщується в 2 сектори */
  TEST_ASSERT_TRUE(3000u / TRAJ_PAGE_SAMPLES + 1u <= 2u * 16u);
}

static void test_roundtrip_with_deltas(void)
{
  traj_enc_t e;
  traj_enc_begin(&e, page, 7, 3);
  traj_sample_t s = { 900, 450 };
  int n = 0;
  while (traj_enc_add(&e, s)) {
    n++;
    s.pan = (int16_t)(s.pan + (n % 2 ? 60 : -59));
    s.tilt = (int16_t)(s.tilt - 3);
  }
  TEST_ASSERT_EQUAL((int)TRAJ_PAGE_SAMPLES, n);
  traj_enc_finish(&e);
  TEST_ASSERT_TRUE(traj_page_valid(page));
  TEST_ASSERT_EQUAL_UINT16(7, traj_page_session(page));
  TEST_ASSERT_EQUAL_UINT16(3, traj_page_seq(page));
  TEST_ASSERT_EQUAL_UINT8(TRAJ_PAGE_SAMPLES, traj_page_samples(page));

  traj_dec_t d;
  traj_dec_begin(&d, page);
  traj_sample_t o;
  traj_sample_t want = { 900, 450 };
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_TRUE(traj_dec_next(&d, &o));
    TEST_ASSERT_EQUAL_INT16(want.pan, o.pan);
    TEST_ASSERT_EQUAL_INT16(want.tilt, o.tilt);
    want.pan = (int16_t)(want.pan + ((i + 1) % 2 ? 60 : -59));
    want.tilt = (int16_t)(want.tilt - 3);
  }
  TEST_ASSERT_FALSE(traj_dec_next(&d, &o));
}

static void test_big_jump_requests_new_keyframe(void)
{
  traj_enc_t e;
  traj_enc_begin(&e, page, 1, 0);
  TEST_ASSERT_TRUE(traj_enc_add(&e, (traj_sample_t){ 900, 900 }));
  TEST_ASSERT_TRUE(traj_enc_add(&e, (traj_sample_t){ 1027, 773 })); /* ±127 ще вміщується */
  TEST_ASSERT_FALSE(traj_enc_add(&e, (traj_sample_t){ 1155, 773 }));
  TEST_ASSERT_FALSE(traj_enc_add(&e, (traj_sample_t){ 1027, 645 }));
}

static void test_negative_keyframe_values(void)
{
  traj_enc_t e;
  traj_enc_begin(&e, page, 1, 0);
  traj_enc_add(&e, (traj_sample_t){ -5, -300 });
  traj_enc_finish(&e);
  traj_dec_t d;
  traj_sample_t o;
  traj_dec_begin(&d, page);
  TEST_ASSERT_TRUE(traj_dec_next(&d, &o));
  TEST_ASSERT_EQUAL_INT16(-5, o.pan);
  TEST_ASSERT_EQUAL_INT16(-300, o.tilt);
}

static void test_any_flipped_bit_invalidates_page(void)
{
  traj_enc_t e;
  traj_enc_begin(&e, page, 2, 1);
  traj_enc_add(&e, (traj_sample_t){ 100, 200 });
  traj_enc_add(&e, (traj_sample_t){ 101, 201 });
  traj_enc_finish(&e);
  for (unsigned i = 0; i < TRAJ_PAGE_SIZE; i++) {
    page[i] ^= 0x10u;
    TEST_ASSERT_FALSE_MESSAGE(traj_page_valid(page), "пошкодження не виявлено");
    page[i] ^= 0x10u;
  }
  TEST_ASSERT_TRUE(traj_page_valid(page));
}

static void test_erased_and_torn_pages_are_invalid(void)
{
  memset(page, 0xFF, sizeof page);
  TEST_ASSERT_FALSE(traj_page_valid(page));
  traj_enc_t e;
  traj_enc_begin(&e, page, 2, 1);
  traj_enc_add(&e, (traj_sample_t){ 100, 200 });
  traj_enc_finish(&e);
  memset(&page[100], 0xFF, TRAJ_PAGE_SIZE - 100u); /* обрив після 100 байт */
  page[20] = 0x12;
  TEST_ASSERT_FALSE(traj_page_valid(page));
  page[6] = 0;
  TEST_ASSERT_FALSE(traj_page_valid(page));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_crc16_ccitt_false_check_value);
  RUN_TEST(test_page_holds_122_samples_for_50hz);
  RUN_TEST(test_roundtrip_with_deltas);
  RUN_TEST(test_big_jump_requests_new_keyframe);
  RUN_TEST(test_negative_keyframe_values);
  RUN_TEST(test_any_flipped_bit_invalidates_page);
  RUN_TEST(test_erased_and_torn_pages_are_invalid);
  return UNITY_END();
}
