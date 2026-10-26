#ifndef TASK_UART_PLATFORM_H
#define TASK_UART_PLATFORM_H
#include <stdint.h>
/* Caller supplies balanced, nesting-safe hooks. No permissive fallback. */
uint32_t TaskUart_PlatformEnter(void);
void TaskUart_PlatformExit(uint32_t saved_mask);
/* Read one authoritative clock AFTER entering the protected operation. */
uint32_t TaskUart_PlatformNowMs(uint32_t legacy_tick_ms);
#endif
