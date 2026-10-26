#ifndef OPERATOR_PLATFORM_H
#define OPERATOR_PLATFORM_H
#include <stdint.h>
/* Nonblocking startup source. Return 1 with a fresh nonzero boot identity.
 * Return 0 while unavailable, 2 on permanent failure. No fallback to UID/tick. */
uint8_t OperatorPlatform_PollBootId(uint32_t *low, uint32_t *high);
#endif
