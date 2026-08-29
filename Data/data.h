#ifndef __DATA_H
#define __DATA_H

#include "sys.h"
#include "config.h"
#include "app_error.h"

/* AHT20 传感器数据结构(数据层定义, 硬件驱动头文件不反向依赖业务层) */
struct m_AHT20
{
	uint8_t alive;	/* 0-器件不存在; 1-器件存在 */
	uint8_t flag;	/* 读取/计算错误标志位。0-正常; 1-失败 */
	uint32_t HT[2];	/* 湿度、温度 原始传感器的值，20Bit */
	float RH;		/* 湿度，转换单位后的实际值，标准单位% */
	float Temp;		/* 温度，转换单位后的实际值，标准单位°C */
};

typedef struct
{
	u8 temperature;     /* 环境温度 ℃ */
	u8 humidity;        /* 环境湿度 %RH */
	u8 soil_humi;       /* 土壤湿度 % */
	u8 light;           /* 光照强度 % */
} SensorData_t;

typedef struct
{
	u8 temp_h;          /* 环境温度阈值 */
	u8 humi_l;          /* 环境湿度阈值 */
	u8 soil_l;          /* 土壤湿度阈值 */
	u8 light_l;         /* 光照强度阈值 */
} Threshold_t;

typedef struct
{
	u8 soil;            /* 土壤湿度报警 */
	u8 light;           /* 光照报警 */
	u8 temp;            /* 温度报警 */
	u8 humi;            /* 湿度报警 */
} AlarmFlag_t;

typedef struct
{
	u8 run_mode;        /* 0=自动  1=手动(APP) */
	u8 connected;       /* WiFi已连接标志 */
	u8 set_cs;          /* 是否处于参数设置界面 */
	u8 set_cs_number;   /* 当前设置项编号 1~4 */
	ErrorCode_t error;  /* 当前系统错误码 */
	u8 error_page;      /* 1=OLED显示错误页 */
} SystemState_t;

/* 全局业务数据封装 */
typedef struct
{
	SensorData_t  sensor;
	Threshold_t   threshold;
	AlarmFlag_t   alarm;
	SystemState_t sys;
	struct m_AHT20 aht20;
} PlantCare_t;

extern PlantCare_t g_plant;

void Data_Init(void);

#endif
