#ifndef __APP_TASK_H
#define __APP_TASK_H

#include "sys.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

extern volatile u32 SysTick_Count;          /* 100ms心跳计数(由TIM2中断累加) */
extern SemaphoreHandle_t USART1_TxMutex;    /* 串口打印互斥量(fputc内逐字符加锁) */
extern SemaphoreHandle_t OLED_Mutex;        /* OLED互斥量(OLED驱动层使用) */
extern TaskHandle_t SystemTask_Handle;      /* 状态机任务句柄(TIM2通知) */
extern TaskHandle_t KeyTask_Handle;         /* 按键任务句柄 */
extern TaskHandle_t CloudTask_Handle;       /* 云平台任务句柄 */
extern TaskHandle_t LogTask_Handle;         /* 日志任务句柄 */

#define TICK_MS         100

#define PERIOD_KEY      1
#define PERIOD_SENSOR   5
#define PERIOD_CLOUD    20
#define PERIOD_LOG      50

void App_Task_Init(void);   /* 创建全部FreeRTOS任务(调度器启动前调用) */
void Boot_Confirm(void);    /* Boot流程步骤3调用 */

void System_Task(void *arg);
void Key_Task(void *arg);
void Cloud_Task(void *arg);
void Log_Task(void *arg);

#endif
