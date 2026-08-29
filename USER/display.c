#include "display.h"
#include "oled_iic.h"
#include "data.h"
#include "app_error.h"
#include "app_log.h"
#include "stdio.h"
#include "delay.h"

/* 统一显示入口: 由System任务每500ms调用一次, 所有OLED绘制收敛到单任务,
 * 避免Key任务与System任务分帧绘制导致画面撕裂 */
void Display_Update(void)
{
	static u8 s_last_set_cs = 0;

	if(g_plant.sys.error_page)
	{
		/* OLED不可用: 只记录日志 */
		if(g_plant.sys.error != ERR_OK)
			LOG_ERROR(Error_ToString(g_plant.sys.error));
		return;
	}

	if(g_plant.sys.set_cs)
	{
		if(!s_last_set_cs)
		{
			OLED_Clear();          /* 进入设置界面: 清屏 */
			s_last_set_cs = 1;
		}
		Display_SetParameters();
	}
	else
	{
		s_last_set_cs = 0;
		if(g_plant.sys.error != ERR_OK)
			Display_ErrorPage();
		else
			Display_MainPage();
	}
}

void Display_SetParameters(void)
{
	char buf[24];

	if(g_plant.sys.set_cs_number == 1)
	{
		OLED_ShowCH(0, 0, (u8*)"土壤湿度阈值");
		sprintf(buf, "%d %%  ", g_plant.threshold.soil_l);
		OLED_ShowCH(48, 4, (u8*)buf);
	}
	if(g_plant.sys.set_cs_number == 2)
	{
		OLED_ShowCH(0, 0, (u8*)"光照阈值");
		sprintf(buf, "%d lux  ", g_plant.threshold.light_l);
		OLED_ShowCH(48, 4, (u8*)buf);
	}
	if(g_plant.sys.set_cs_number == 3)
	{
		OLED_ShowCH(0, 0, (u8*)"环境TEMP阈值");
		sprintf(buf, "%d C  ", g_plant.threshold.temp_h);
		OLED_ShowCH(48, 4, (u8*)buf);
	}
	if(g_plant.sys.set_cs_number == 4)
	{
		OLED_ShowCH(0, 0, (u8*)"环境湿度阈值");
		sprintf(buf, "%d %%RH  ", g_plant.threshold.humi_l);
		OLED_ShowCH(48, 4, (u8*)buf);
	}
}

void Display_MainPage(void)
{
	OLED_ShowCH(0, 0, (u8*)"土壤:");
	OLED_ShowNum(40, 0, g_plant.sensor.soil_humi, 2, 1);
	if(g_plant.sensor.soil_humi < 10)
		OLED_ShowCH(48, 0, (u8*)"%  ");
	else if(g_plant.sensor.soil_humi >= 10 && g_plant.sensor.soil_humi < 100)
		OLED_ShowCH(56, 0, (u8*)"% ");
	else if(g_plant.sensor.soil_humi >= 100 && g_plant.sensor.soil_humi < 1000)
		OLED_ShowCH(64, 0, (u8*)"%");

	OLED_ShowCH(0, 2, (u8*)"光照:");
	OLED_ShowNum(40, 2, g_plant.sensor.light, 2, 1);
	if(g_plant.sensor.light < 10)
		OLED_ShowCH(48, 2, (u8*)"Lux  ");
	else if(g_plant.sensor.light >= 10 && g_plant.sensor.light < 100)
		OLED_ShowCH(56, 2, (u8*)"Lux ");
	else if(g_plant.sensor.light >= 100 && g_plant.sensor.light < 1000)
		OLED_ShowCH(64, 2, (u8*)"Lux");

	OLED_ShowCH(0, 4, (u8*)"TEMP:");
	OLED_ShowNum(40, 4, g_plant.sensor.temperature, 2, 1);
	if(g_plant.sensor.temperature < 10)
		OLED_ShowCH(48, 4, (u8*)"C  ");
	else if(g_plant.sensor.temperature >= 10 && g_plant.sensor.temperature < 100)
		OLED_ShowCH(56, 4, (u8*)"C ");
	else if(g_plant.sensor.temperature >= 100 && g_plant.sensor.temperature < 1000)
		OLED_ShowCH(64, 4, (u8*)"C");

	OLED_ShowCH(0, 6, (u8*)"湿度:");
	OLED_ShowNum(40, 6, g_plant.sensor.humidity, 2, 1);
	if(g_plant.sensor.humidity < 10)
		OLED_ShowCH(48, 6, (u8*)"%RH  ");
	else if(g_plant.sensor.humidity >= 10 && g_plant.sensor.humidity < 100)
		OLED_ShowCH(56, 6, (u8*)"%RH ");
	else if(g_plant.sensor.humidity >= 100 && g_plant.sensor.humidity < 1000)
		OLED_ShowCH(54, 6, (u8*)"%RH");

	if(g_plant.alarm.soil)
		OLED_ShowCH(96, 0, (u8*)"超标");
	else
		OLED_ShowCH(96, 0, (u8*)"正常");

	if(g_plant.alarm.light)
		OLED_ShowCH(96, 2, (u8*)"超标");
	else
		OLED_ShowCH(96, 2, (u8*)"正常");

	if(g_plant.alarm.temp)
		OLED_ShowCH(96, 4, (u8*)"超标");
	else
		OLED_ShowCH(96, 4, (u8*)"正常");

	if(g_plant.alarm.humi)
		OLED_ShowCH(96, 6, (u8*)"超标");
	else
		OLED_ShowCH(96, 6, (u8*)"正常");
}

void Display_ModeTip(u8 mode)
{
	OLED_Clear();
	if(mode == 1)
		OLED_ShowCH(8, 2, (u8*)"切换APP模式");
	else
		OLED_ShowCH(8, 2, (u8*)"自动模式");
	delay_ms(500);
	OLED_Clear();
}

void Display_ErrorPage(void)
{
	static ErrorCode_t last_error = ERR_OK;

	if(last_error != g_plant.sys.error)
	{
		OLED_Clear();
		last_error = g_plant.sys.error;
	}
	
	OLED_ShowCH(0, 0, (u8*)"Error:");
	
	switch(g_plant.sys.error)
	{
		case ERR_AHT20_INIT:
			OLED_ShowCH(40, 0, (u8*)"AHT20");
			break;
		case ERR_SOIL_SENSOR:
			OLED_ShowCH(40, 0, (u8*)"Soil");
			break;
		case ERR_LIGHT_SENSOR:
			OLED_ShowCH(40, 0, (u8*)"Light");
			break;
		case ERR_ADC_FAIL:
			OLED_ShowCH(40, 0, (u8*)"ADC");
			break;
		case ERR_WIFI_TIMEOUT:
			OLED_ShowCH(40, 0, (u8*)"WiFi");
			break;
		case ERR_TCP_CONNECT:
			OLED_ShowCH(40, 0, (u8*)"TCP");
			break;
		case ERR_CLOUD_OFFLINE:
			OLED_ShowCH(40, 0, (u8*)"Cloud");
			break;
		case ERR_PARAMETER_INVALID:
			OLED_ShowCH(40, 0, (u8*)"Param");
			break;
		case ERR_OLED_INIT:
			OLED_ShowCH(40, 0, (u8*)"OLED");
			break;
		case ERR_SENSOR_TIMEOUT:
			OLED_ShowCH(40, 0, (u8*)"Sensor");
			break;
		default:
			OLED_ShowCH(40, 0, (u8*)"Unknown");
			break;
	}
	
	OLED_ShowCH(0, 4, (u8*)"Retry...");
}
