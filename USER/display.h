#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "sys.h"

void Display_Update(void);       /* 统一显示入口: 主页面/错误页/参数设置页(仅System任务调用) */
void Display_SetParameters(void);
void Display_MainPage(void);
void Display_ModeTip(u8 mode);
void Display_ErrorPage(void);

#endif
