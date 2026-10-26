#ifndef TEST_MOCK_STM32F4XX_HAL_H
#define TEST_MOCK_STM32F4XX_HAL_H

#include <stdint.h>

typedef enum
{
  HAL_OK = 0x00U,
  HAL_ERROR = 0x01U,
  HAL_BUSY = 0x02U,
  HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef struct
{
  void *Instance;
  uint32_t ErrorCode;
  uint32_t gState;
  uint32_t RxState;
} UART_HandleTypeDef;

#define HAL_UART_ERROR_NONE (0x00000000UL)
#define HAL_UART_STATE_READY   (0x20U)
#define HAL_UART_STATE_BUSY_TX (0x21U)
#define HAL_UART_STATE_BUSY_RX (0x22U)
#define CAN_ID_STD          (0x00000000UL)
#define CAN_RTR_DATA        (0x00000000UL)

HAL_StatusTypeDef HAL_UART_Receive_IT(
    UART_HandleTypeDef *huart,
    uint8_t *data,
    uint16_t size);
HAL_StatusTypeDef HAL_UART_Transmit_IT(
    UART_HandleTypeDef *huart,
    const uint8_t *data,
    uint16_t size);
uint32_t HAL_UART_GetError(const UART_HandleTypeDef *huart);

#endif
