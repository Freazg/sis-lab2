/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32c0xx_hal_msp.c
  * @brief   MSP: тактування периферії та alternate functions пінів.
  *          Номери пінів і AF беруться з Platform/board.h (єдине джерело).
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

static void af_pin(GPIO_TypeDef *port, uint16_t pin, uint32_t mode, uint32_t pull, uint32_t speed, uint8_t af)
{
  GPIO_InitTypeDef g = { 0 };
  g.Pin = pin;
  g.Mode = mode;
  g.Pull = pull;
  g.Speed = speed;
  g.Alternate = af;
  HAL_GPIO_Init(port, &g);
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    af_pin(CON_TX_PORT, CON_TX_PIN, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, CON_AF);
    af_pin(CON_RX_PORT, CON_RX_PIN, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, CON_AF);
  }
  else if (huart->Instance == USART1)
  {
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    af_pin(AUX_TX_PORT, AUX_TX_PIN, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, AUX_AF);
    /* RX без внутрішнього pull-up: FT232RL з VCCIO=5 В видає до 4.9 В, а DS13867 Table 24 note 2
       вимагає вимкнених pull-up/down при VIN > VDD+0.3 В */
    af_pin(AUX_RX_PORT, AUX_RX_PIN, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, AUX_AF);
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    __HAL_RCC_USART2_CLK_DISABLE();
    HAL_GPIO_DeInit(CON_TX_PORT, CON_TX_PIN | CON_RX_PIN);
  }
  else if (huart->Instance == USART1)
  {
    __HAL_RCC_USART1_CLK_DISABLE();
    HAL_GPIO_DeInit(AUX_TX_PORT, AUX_TX_PIN | AUX_RX_PIN);
  }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == I2C1)
  {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* open-drain; зовнішні pull-up — на модулі GY-521, внутрішні вимкнені */
    af_pin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_MODE_AF_OD, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, I2C_AF);
    af_pin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_MODE_AF_OD, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, I2C_AF);
    __HAL_RCC_I2C1_CLK_ENABLE();
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == I2C1)
  {
    __HAL_RCC_I2C1_CLK_DISABLE();
    HAL_GPIO_DeInit(I2C_SCL_PORT, I2C_SCL_PIN | I2C_SDA_PIN);
  }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    af_pin(SPI_SCK_PORT, SPI_SCK_PIN, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH, SPI_AF);
    af_pin(SPI_MISO_PORT, SPI_MISO_PIN, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH, SPI_AF);
    af_pin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH, SPI_AF);
  }
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    __HAL_RCC_SPI1_CLK_DISABLE();
    HAL_GPIO_DeInit(SPI_SCK_PORT, SPI_SCK_PIN);
    HAL_GPIO_DeInit(SPI_MISO_PORT, SPI_MISO_PIN | SPI_MOSI_PIN);
  }
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    __HAL_RCC_TIM3_CLK_ENABLE();
  }
}

/** Піни ШІМ налаштовуються після конфігурації каналів, щоб серво не смикались. */
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    af_pin(SERVO_PAN_PORT, SERVO_PAN_PIN | SERVO_TILT_PIN, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW,
           SERVO_AF);
  }
}

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    __HAL_RCC_TIM3_CLK_DISABLE();
  }
}
