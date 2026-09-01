#ifndef __TIMER_H
#define __TIMER_H
#include "sys.h"

void TIM2_Int_Init(u16 arr,u16 psc);

void TIM3_PWM_Init(void);   /* TIM3_CH3(PB0) PWM, 驱动风扇无级调速 */
void Fan_SetSpeed(u8 duty); /* 风扇转速 0~100% */

#endif

