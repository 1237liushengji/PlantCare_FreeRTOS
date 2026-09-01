#include "timer.h"
#include "app_task.h"
#include "FreeRTOS.h"
#include "task.h"

//通用定时器2中断初始化(100ms心跳, 周期由config.h的TIMER_ARR/TIMER_PSC决定)
void TIM2_Int_Init(u16 arr,u16 psc)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); //时钟使能
	
	//定时器TIM2初始化
	TIM_TimeBaseStructure.TIM_Period = arr; //自动重装载值
	TIM_TimeBaseStructure.TIM_Prescaler = psc; //预分频
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1; //时钟分割
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  //向上计数
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
 
	TIM_ITConfig(TIM2,TIM_IT_Update,ENABLE ); //使能TIM2更新中断

	//FreeRTOS要求调用API的中断优先级数值 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;  //抢占优先级5
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;  //子优先级0
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	//注意:此处不使能TIM2,待FreeRTOS调度器启动后由System任务调用TIM_Cmd(TIM2,ENABLE),
	//避免调度器未启动时中断通知空任务句柄
}

//定时器2中断服务程序: 每100ms累加心跳计数并通知System任务
void TIM2_IRQHandler(void)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
	{
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
		SysTick_Count++;
		vTaskNotifyGiveFromISR(SystemTask_Handle, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
}

/* ==================== TIM3_CH3(PB0) PWM: 风扇无级调速 ==================== */
#define FAN_PWM_ARR  999    /* 自动重装载值: 配合PSC得到1kHz PWM */
#define FAN_PWM_PSC  71     /* 预分频: 72MHz/72 = 1MHz计数 */

void TIM3_PWM_Init(void)
{
	GPIO_InitTypeDef GPIOInitStructure;
	TIM_TimeBaseInitTypeDef TIMInitStructure;
	TIM_OCInitTypeDef TIMOCInitStructure;

	/* 使能GPIOB(风扇引脚PB0)、TIM3、AFIO时钟 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

	/* TIM3_CH3 = PB0(风扇), 复用推挽输出 */
	GPIOInitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
	GPIOInitStructure.GPIO_Pin   = GPIO_Pin_0;
	GPIOInitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIOInitStructure);

	/* 定时器基础配置 */
	TIMInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIMInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
	TIMInitStructure.TIM_Period        = FAN_PWM_ARR;
	TIMInitStructure.TIM_Prescaler     = FAN_PWM_PSC;
	TIM_TimeBaseInit(TIM3, &TIMInitStructure);

	/* PWM模式1, 高电平有效 */
	TIMOCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
	TIMOCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIMOCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
	TIM_OC3Init(TIM3, &TIMOCInitStructure);
	TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(TIM3, ENABLE);
	TIM_Cmd(TIM3, ENABLE);
	TIM_SetCompare3(TIM3, 0);   /* 初始停转 */
}

/* 风扇转速控制: duty 0~100(%) */
void Fan_SetSpeed(u8 duty)
{
	u32 cmp = ((u32)duty * FAN_PWM_ARR) / 100u;

	if(cmp > FAN_PWM_ARR)
		cmp = FAN_PWM_ARR;
	TIM_SetCompare3(TIM3, (u16)cmp);
}
