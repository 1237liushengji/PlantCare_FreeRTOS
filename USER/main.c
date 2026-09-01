#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "timer.h"
#include "config.h"
#include "app_task.h"
#include "oled_iic.h"

/*****************************************************************************
 * 主函数: 仅完成系统初始化与FreeRTOS任务创建, 业务逻辑全部封装在各层任务中
 *   - CONFIG/Data : 参数与全局数据
 *   - App层       : 状态机(Boot->Init->Auto/Manual/Alarm/Error) + 任务创建
 *   - control层   : 传感器采集/自动控制/WIFI/APP指令
 *   - display层   : OLED界面
 *****************************************************************************/
int main(void)
{
	HZ = GB16_NUM();                    //初始化中文字库索引
	delay_init();                       //延时函数初始化(FreeRTOS模式, SysTick 1ms)
	delay_ms(100);
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);   //中断优先级分组4(4位全为抢占优先级)
	                 //TIM2抢占=5、USART1抢占=6 直接对应全优先级5/6, 满足FreeRTOS">=5"要求
	                 //(原分组2只有2位抢占位, 抢占优先级只能0~3, 5/6无效导致FreeRTOS校验失败)
	uart1_init(115200);                 //串口1(ESP8266)
	TIM2_Int_Init(TIMER_ARR, TIMER_PSC);  //100ms心跳定时器(调度器启动后由System任务使能)
	IWDG_Init();                        //独立看门狗约2.5s超时(空闲任务钩子喂狗)

	App_Task_Init();                    //创建全部FreeRTOS任务(唯一入口)
	vTaskStartScheduler();              //启动FreeRTOS调度器, 任务接管

	while(1);                           //正常情况下不会执行到这里
}
