#ifndef __USART_H
#define __USART_H

#include "sys.h"  
#include "stdio.h"	
#include "stm32f10x.h"
#include "delay.h"

#define USART_REC_LEN  			400  	//定义最大接收字节数 200
#define EN_USART1_RX 			1		//使能（1）/禁止（0）串口1接收

extern volatile u8 reception;
extern char  USART_RX_BUF[USART_REC_LEN]; //接收缓冲,最大USART_REC_LEN个字节.末字节为换行符 
extern volatile u16 USART_RX_STA;         		//接收状态标记	
extern volatile u8 usart_link_lost;     /* ESP8266断线提示(CLOSED/WIFI DISCONNECT) */
//如果想串口中断接收，请不要注释以下宏定义
void uart1_init(u32 bound);
void USART1_SendString(const char *str); /* 整串发送(带互斥量, 保证不与其他任务printf交错) */
#endif


