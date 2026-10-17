/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.h
  * @brief   Header for main.c (структура, як генерує STM32CubeMX).
  *          Піни і хендли НЕ визначаються тут: єдине джерело — Platform/board.h.
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32c0xx_hal.h"

/* USER CODE BEGIN Includes */
#include "board.h"
/* USER CODE END Includes */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
