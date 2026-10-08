/**
  ******************************************************************************
  * @file    stm32c0xx_it.c
  * @brief   Обробники переривань Cortex-M0+.
  ******************************************************************************
  */
#include "main.h"
#include "stm32c0xx_it.h"

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/** SysTick 1 kHz — база часу HAL (HAL_GetTick / HAL_Delay). */
void SysTick_Handler(void)
{
  HAL_IncTick();
}
