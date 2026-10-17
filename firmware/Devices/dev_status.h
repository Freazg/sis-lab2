/**
 * @file    dev_status.h
 * @brief   Коди результату для драйверів пристроїв і шин.
 *          Шар Devices не знає про HAL_StatusTypeDef: адаптери Platform
 *          перекладають помилки HAL у ці коди.
 */
#ifndef DEV_STATUS_H
#define DEV_STATUS_H

typedef enum {
  DEV_OK = 0,
  DEV_ERR_NACK,     /**< пристрій не відповів ACK (адреса/дані) */
  DEV_ERR_TIMEOUT,  /**< операція шини не завершилась вчасно */
  DEV_ERR_BUS,      /**< інша помилка шини (arbitration lost, busy, ...) */
  DEV_ERR_ID,       /**< неочікуваний ідентифікатор чіпа */
  DEV_ERR_PARAM,    /**< некоректні аргументи */
  DEV_ERR_BUSY,     /**< пристрій зайнятий довше за допустимий час */
  DEV_ERR_VERIFY    /**< перевірка (read-back / правдоподібність) не пройшла */
} dev_status_t;

/** Коротка назва коду для логів самоперевірки. */
const char *dev_status_str(dev_status_t st);

#endif /* DEV_STATUS_H */
