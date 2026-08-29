#ifndef __LED_H
#define __LED_H	 
#include "sys.h"

#define LED_RUN PCout(13)		  	//运行指示灯


#define WATER PAout(7)		  	//水泵
#define FAN PBout(0)		      //风扇
#define LED_zm PBout(1)		    //照明
#define JSQ PBout(11)		      //加湿

void LED_Init(void);//初始化

		 				    
#endif
