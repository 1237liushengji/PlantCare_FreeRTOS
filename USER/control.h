#ifndef __CONTROL_H
#define __CONTROL_H

#include "sys.h"

extern volatile u8 g_wifi_busy;   /* WiFi初始化忙标志(Log任务据此暂停打印) */

u8 WIFI_Init(void);
void Ping(void);
void APP_Control(void);
void Set_Parameters(void);
void Auto_Control(void);
void Sensor_Update(void);
void Threshold_Load(void);   /* 上电从EEPROM恢复阈值(须在IIC_Init之后) */
void Threshold_Save(void);   /* 保存阈值到EEPROM(回读校验) */

#endif
