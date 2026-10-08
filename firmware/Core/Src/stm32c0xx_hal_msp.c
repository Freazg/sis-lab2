/**
  ******************************************************************************
  * @file    stm32c0xx_hal_msp.c
  * @brief   MSP (MCU Support Package): тактування периферії та alternate functions пінів.
  *          HAL викликає ці функції з HAL_xxx_Init()/DeInit().
  ******************************************************************************
  */
#include "main.h"

void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef g = { 0 };
  if (huart->Instance == USART2)
  {
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /* PA2 -> USART2_TX, PA3 -> USART2_RX */
    g.Pin       = VCP_TX_Pin | VCP_RX_Pin;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_USART2;
    HAL_GPIO_Init(GPIOA, &g);
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    __HAL_RCC_USART2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, VCP_TX_Pin | VCP_RX_Pin);
  }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
  GPIO_InitTypeDef g = { 0 };
  if (hi2c->Instance == I2C1)
  {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* PB8 -> I2C1_SCL, PB9 -> I2C1_SDA (open-drain; pull-up на модулі GY-521) */
    g.Pin       = I2C1_SCL_Pin | I2C1_SDA_Pin;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF6_I2C1;
    HAL_GPIO_Init(GPIOB, &g);
    __HAL_RCC_I2C1_CLK_ENABLE();
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == I2C1)
  {
    __HAL_RCC_I2C1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, I2C1_SCL_Pin | I2C1_SDA_Pin);
  }
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
  GPIO_InitTypeDef g = { 0 };
  if (hadc->Instance == ADC1)
  {
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /* PA0 -> ADC1_IN0 */
    g.Pin  = POT_Pin;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(POT_GPIO_Port, &g);
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    __HAL_RCC_ADC_CLK_DISABLE();
    HAL_GPIO_DeInit(POT_GPIO_Port, POT_Pin);
  }
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    __HAL_RCC_TIM3_CLK_ENABLE();
  }
}

/** Піни PWM налаштовуються після конфігурації каналів, щоб не було "смикання" серво. */
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)
{
  GPIO_InitTypeDef g = { 0 };
  if (htim->Instance == TIM3)
  {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* PB4 -> TIM3_CH1 (pan), PB5 -> TIM3_CH2 (tilt) */
    g.Pin       = SERVO_PAN_Pin | SERVO_TILT_Pin;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(GPIOB, &g);
  }
}

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    __HAL_RCC_TIM3_CLK_DISABLE();
  }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
  GPIO_InitTypeDef g = { 0 };
  if (hspi->Instance == SPI1)
  {
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* PB3 -> SPI1_SCK, PA6 -> SPI1_MISO, PA7 -> SPI1_MOSI (усі AF0) */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF0_SPI1;
    g.Pin       = FLASH_SCK_Pin;
    HAL_GPIO_Init(FLASH_SCK_GPIO_Port, &g);
    g.Pin       = FLASH_MISO_Pin | FLASH_MOSI_Pin;
    HAL_GPIO_Init(GPIOA, &g);
  }
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    __HAL_RCC_SPI1_CLK_DISABLE();
    HAL_GPIO_DeInit(FLASH_SCK_GPIO_Port, FLASH_SCK_Pin);
    HAL_GPIO_DeInit(GPIOA, FLASH_MISO_Pin | FLASH_MOSI_Pin);
  }
}
