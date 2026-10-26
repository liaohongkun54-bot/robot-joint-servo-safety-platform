#include "operator_platform.h"
#include "stm32f4xx_hal.h"

/* This port never changes generated clock/peripheral configuration.
 * CubeMX must provide an enabled, correctly clocked RNG. Until then it fails
 * closed. Clock configuration is checked, not silently patched here. */
uint8_t OperatorPlatform_PollBootId(uint32_t *low, uint32_t *high)
{
  static uint32_t first;
  static uint8_t have_first, failed;
  uint32_t value, pll, m, n, q, input_hz, clock_hz;
  if (low == 0 || high == 0 || failed != 0U) { return 2U; }
  if ((RCC->AHB2ENR & RCC_AHB2ENR_RNGEN) == 0U ||
      (RNG->CR & RNG_CR_RNGEN) == 0U) { failed = 1U; return 2U; }
  pll = RCC->PLLCFGR;
  m = pll & RCC_PLLCFGR_PLLM;
  n = (pll & RCC_PLLCFGR_PLLN) >> 6U;
  q = (pll & RCC_PLLCFGR_PLLQ) >> 24U;
  input_hz = ((pll & RCC_PLLCFGR_PLLSRC) != 0U) ? HSE_VALUE : HSI_VALUE;
  if (m == 0U || q < 2U || (RCC->CR & RCC_CR_PLLRDY) == 0U)
  { failed = 1U; return 2U; }
  clock_hz = (uint32_t)(((uint64_t)input_hz * n) / ((uint64_t)m * q));
  if (clock_hz != 48000000UL ||
      (RNG->SR & (RNG_SR_CECS | RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS)) != 0U)
  { failed = 1U; return 2U; }
  if ((RNG->SR & RNG_SR_DRDY) == 0U) { return 0U; }
  value = RNG->DR;
  if (have_first == 0U) { first = value; have_first = 1U; return 0U; }
  if (value == first || (value == 0U && first == 0U)) { failed = 1U; return 2U; }
  *low = first; *high = value;
  return 1U;
}
