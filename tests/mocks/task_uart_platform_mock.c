#include "task_uart_platform.h"
#include <assert.h>
static uint32_t depth;
uint32_t TaskUart_PlatformEnter(void) { return depth++; }
void TaskUart_PlatformExit(uint32_t previous) { assert(depth == previous + 1U); depth = previous; }
/* Legacy sequential regression tests supply their own time samples. */
uint32_t TaskUart_PlatformNowMs(uint32_t legacy_tick_ms) { assert(depth != 0U); return legacy_tick_ms; }
