/**
  ******************************************************************************
  * @file    app_main_task_service.h
  * @brief   One behavior-equivalent service iteration for the main application.
  ******************************************************************************
  */

#ifndef __APP_MAIN_TASK_SERVICE_H__
#define __APP_MAIN_TASK_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Run one non-blocking application service iteration. */
void AppMainTaskService_RunOnce(uint32_t now_ms);

/** High-priority owner of CAN, feedback, Safety, and Motion Gate state. */
void AppMainTaskService_RunControlOnce(uint32_t now_ms);

/** Complete a stop record, then consume one validated UART task. */
void AppMainTaskService_RunIngressOnce(uint32_t now_ms);

/** Service the UART receiver and pending transmitter. */
void AppMainTaskService_RunUartOnce(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MAIN_TASK_SERVICE_H__ */
