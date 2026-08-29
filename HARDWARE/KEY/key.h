#ifndef __KEY_H
#define __KEY_H	 
#include "sys.h"

#define KEY_set    GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_9)		//读取按键
#define KEY_add    GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_8)		//读取按键
#define KEY_del  	 GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_5)		//读取按键


void KEY_Init(void);//IO初始化				    
#endif
