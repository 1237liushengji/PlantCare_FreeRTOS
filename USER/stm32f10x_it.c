/**
  ******************************************************************************
  * @file    GPIO/IOToggle/stm32f10x_it.c 
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   Main Interrupt Service Routines.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_it.h"
#include "sys.h"
#include "led.h"

/* ==================== 故障现场记录(供调试器/日志观察) ====================
 * HardFault_Handler 中读取的是任务栈(PSP)上的异常帧, 因此不受C函数
 * 压栈影响; MSP帧因处理器现场已压栈, 仅记录MSP值供参考。
 * 独立看门狗约2.5s后复位系统(Fault_Halt期间无空闲任务喂狗)。 */
volatile u32 g_fault_pc   = 0;   /* 出错PC(任务上下文) */
volatile u32 g_fault_xpsr = 0;   /* 出错xPSR(任务上下文) */
volatile u32 g_fault_psp  = 0;   /* 任务栈指针 */
volatile u32 g_fault_msp  = 0;   /* 主栈指针(处理器上下文, 已含压栈偏移) */

/* 故障停车: LED快速闪烁, 便于定位(区别于运行/错误态的闪烁节奏) */
static void Fault_Halt(void)
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

void NMI_Handler(void)
{
	Fault_Halt();
}

void HardFault_Handler(void)
{
	/* 异常帧布局: [0]=r0 [1]=r1 [2]=r2 [3]=r3 [4]=r12 [5]=lr [6]=pc [7]=xpsr */
	g_fault_psp = __get_PSP();
	g_fault_msp = __get_MSP();
	if(g_fault_psp != 0)
	{
		g_fault_pc   = ((u32 *)g_fault_psp)[6];
		g_fault_xpsr = ((u32 *)g_fault_psp)[7];
	}
	Fault_Halt();
}

void MemManage_Handler(void)
{
	g_fault_psp = __get_PSP();
	g_fault_msp = __get_MSP();
	Fault_Halt();
}

void BusFault_Handler(void)
{
	g_fault_psp = __get_PSP();
	g_fault_msp = __get_MSP();
	Fault_Halt();
}

void UsageFault_Handler(void)
{
	g_fault_psp = __get_PSP();
	g_fault_msp = __get_MSP();
	Fault_Halt();
}

/* 以下异常服务函数在FreeRTOS模式下由内核移植层提供:
 *   vPortSVCHandler   -> SVC_Handler
 *   xPortPendSVHandler-> PendSV_Handler
 *   SysTick_Handler   在delay.c中调用xPortSysTickHandler
 * 因此使用OS时不再在此定义,避免重复定义 */
#if !SYSTEM_SUPPORT_OS
void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
}
#endif

/******************************************************************************/
/*                 STM32F10x Peripherals Interrupt Handlers                   */
/******************************************************************************/
