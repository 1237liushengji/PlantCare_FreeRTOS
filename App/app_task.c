#include "app_task.h"
#include "app_state.h"
#include "control.h"
#include "data.h"
#include "app_log.h"
#include "app_error.h"
#include "timer.h"
#include "usart.h"
#include "led.h"
#include "stdio.h"

volatile u32 SysTick_Count = 0;          /* 100ms心跳计数(由TIM2中断累加) */
SemaphoreHandle_t USART1_TxMutex;        /* 串口打印互斥量 */
SemaphoreHandle_t OLED_Mutex;            /* OLED互斥量 */
TaskHandle_t SystemTask_Handle;          /* 状态机任务句柄 */
TaskHandle_t KeyTask_Handle;             /* 按键任务句柄 */
TaskHandle_t CloudTask_Handle;           /* 云平台任务句柄 */
TaskHandle_t LogTask_Handle;             /* 日志任务句柄 */

/* 故障停车: 不可恢复错误时LED快速闪烁, 便于定位(而不是无声死循环) */
static void Fault_Loop(void)
{
	for(;;)
	{
		LED_RUN = !LED_RUN;
		{
			volatile u32 i;
			for(i = 0; i < 200000; i++);
		}
	}
}

/*****************************************************************************
 * 创建全部FreeRTOS任务(在main中、调度器启动前调用)
 *   System: 状态机任务(启动流程/自动/手动/报警/错误恢复), 100ms心跳驱动
 *   Key   : 按键扫描与阈值设置
 *   Cloud : 每2秒向云平台上报数据
 *   Log   : 每5秒打印系统状态
 *****************************************************************************/
void App_Task_Init(void)
{
	USART1_TxMutex = xSemaphoreCreateMutex();   /* 串口打印互斥量 */
	OLED_Mutex = xSemaphoreCreateMutex();       /* OLED互斥量 */

	if(USART1_TxMutex == NULL || OLED_Mutex == NULL)
		Fault_Loop();

	StateMachine_Init();                        /* 状态机初始状态复位 */

	if(xTaskCreate(System_Task, "System", 512, NULL, 2, &SystemTask_Handle) != pdPASS)
		Fault_Loop();
	if(xTaskCreate(Key_Task,    "Key",    320, NULL, 1, &KeyTask_Handle) != pdPASS)
		Fault_Loop();
	if(xTaskCreate(Cloud_Task,  "Cloud",  384, NULL, 1, &CloudTask_Handle) != pdPASS)
		Fault_Loop();
	if(xTaskCreate(Log_Task,    "Log",    384, NULL, 1, &LogTask_Handle) != pdPASS)
		Fault_Loop();
}

/* Boot流程步骤3调用(裸机重构版为协作调度器初始化, FreeRTOS下任务已在App_Task_Init创建) */
void Boot_Confirm(void)
{
	LOG_INFO("FreeRTOS Tasks Ready");
}

/*****************************************************************************
 * 状态机任务: 由TIM2每100ms通知一次, 驱动StateMachine_Run()
 *****************************************************************************/
void System_Task(void *arg)
{
	TIM_Cmd(TIM2, ENABLE);   /* 调度器启动后再使能TIM2(100ms心跳) */

	for(;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* 等待100ms心跳通知 */
		StateMachine_Run();
	}
}

/*****************************************************************************
 * 按键任务: 100ms扫描一次, 处理阈值设置界面
 *****************************************************************************/
void Key_Task(void *arg)
{
	for(;;)
	{
		Set_Parameters();
		vTaskDelay(PERIOD_KEY * TICK_MS);
	}
}

/*****************************************************************************
 * 云平台任务: 每2秒上报一次数据
 *****************************************************************************/
void Cloud_Task(void *arg)
{
	for(;;)
	{
		vTaskDelay(PERIOD_CLOUD * TICK_MS);

		/* 仅在运行状态(AUTO/MANUAL)消费断线标志与上报,
		 * 避免初始化/错误恢复期间ESP8266复位输出"CLOSED"误报离线 */
		if(g_state_machine.current == SYS_AUTO || g_state_machine.current == SYS_MANUAL)
		{
			if(usart_link_lost)
			{
				usart_link_lost = 0;
				g_plant.sys.connected = 0;
				Error_Set(ERR_CLOUD_OFFLINE);
				LOG_ERROR("ESP8266 Link Lost");
			}

			Ping();
		}
	}
}

/*****************************************************************************
 * 日志任务: 每5秒打印一次系统状态
 *****************************************************************************/
void Log_Task(void *arg)
{
	for(;;)
	{
		/* WiFi初始化期间暂停日志输出, 避免日志插入AT命令/透传时序 */
		if(!g_wifi_busy)
		{
			LOG_INFOF("State=%u SysTick=%lu Error=%u",
				g_state_machine.current, SysTick_Count, g_plant.sys.error);
			LOG_INFOF("Temp=%u Hum=%u Soil=%u Light=%u",
				g_plant.sensor.temperature, g_plant.sensor.humidity,
				g_plant.sensor.soil_humi, g_plant.sensor.light);
			LOG_INFOF("StackHWM System=%u Key=%u Cloud=%u Log=%u",
				uxTaskGetStackHighWaterMark(SystemTask_Handle),
				uxTaskGetStackHighWaterMark(KeyTask_Handle),
				uxTaskGetStackHighWaterMark(CloudTask_Handle),
				uxTaskGetStackHighWaterMark(LogTask_Handle));
		}
		vTaskDelay(PERIOD_LOG * TICK_MS);
	}
}

/*****************************************************************************
 * FreeRTOS钩子函数
 *****************************************************************************/
/* 栈溢出钩子(调试用) */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	Fault_Loop();
}
/* 内存申请失败钩子 */
void vApplicationMallocFailedHook(void)
{
	Fault_Loop();
}

/* 空闲任务钩子: 喂独立看门狗(系统卡死/死循环时无空闲任务运行, 触发复位自恢复) */
void vApplicationIdleHook(void)
{
	IWDG_Feed();
}
