#include "task_uart_platform.h"
#include "stm32f4xx_hal.h"

/* Masks configurable IRQs for ONE bounded adapter operation.
 * Does not mask NMI/HardFault. Measure latency before production acceptance.
 * No RTOS calls, waiting, logging, or business execution inside this region.
 */
uint32_t TaskUart_PlatformEnter(void)
{
  uint32_t saved = __get_PRIMASK();
  __disable_irq();
  __DMB();
  return saved;
}

void TaskUart_PlatformExit(uint32_t saved_mask)
{
  __DMB();
  __set_PRIMASK(saved_mask);
  __ISB();
}

uint32_t TaskUart_PlatformNowMs(uint32_t legacy_tick_ms)
{
  (void)legacy_tick_ms;
  return HAL_GetTick();
}
