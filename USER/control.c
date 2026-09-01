#include "control.h"
#include "config.h"
#include "data.h"
#include "display.h"
#include "oled_iic.h"
#include "led.h"
#include "key.h"
#include "usart.h"
#include "adc.h"
#include "AHT20.h"
#include "delay.h"
#include "app_log.h"
#include "app_error.h"
#include "app_task.h"
#include "app_state.h"
#include "stdio.h"
#include "string.h"
#include "24cxx.h"

/* WiFi初始化忙标志: 初始化期间Log任务暂停打印, 避免日志插入AT/透传时序 */
volatile u8 g_wifi_busy = 0;

/* 阈值EEPROM存储地址(24C02, 0~255) */
#define EEPROM_THRESHOLD_ADDR   0x00

static void Log_ActuatorEdge(u8 *last, u8 now, const char *on_msg, const char *off_msg)
{
	if(now == *last)
		return;
	*last = now;
	if(now)
		LOG_INFO(on_msg);
	else
		LOG_INFO(off_msg);
}

static void Log_AlarmEdge(u8 *last, u8 now, const char *warn_msg)
{
	if(now && !(*last))
		LOG_WARN(warn_msg);
	*last = now;
}

/* 清空串口接收缓冲/状态并重新使能接收中断(FreeRTOS下防止与ISR竞争, 丢失未处理帧可接受) */
static void USART_RX_Reset(void)
{
	__disable_irq();
	memset(USART_RX_BUF, 0, sizeof(USART_RX_BUF));
	USART_RX_STA = 0;
	reception = 0;
	__enable_irq();
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
}

/* 匹配独立成行的应答(避免 "BROKEN" 之类包含 "OK" 的串误判) */
static u8 Wifi_BufHasLine(const char *pat)
{
	const char *p = USART_RX_BUF;
	u16 plen = strlen(pat);

	while((p = strstr(p, pat)) != NULL)
	{
		if((p == USART_RX_BUF || p[-1] == '\r' || p[-1] == '\n') &&
		   (p[plen] == '\0' || p[plen] == '\r' || p[plen] == '\n'))
			return 1;
		p++;
	}
	return 0;
}

static u8 WiFi_CheckResponse(u8 timeout)
{
	u8 i;
	for(i = 0; i < timeout; i++)
	{
		delay_ms(100);
		if(Wifi_BufHasLine("OK"))
		{
			USART_RX_Reset();
			return 0;
		}
		if(Wifi_BufHasLine("ERROR") || Wifi_BufHasLine("FAIL"))
		{
			USART_RX_Reset();
			return 1;
		}
	}
	return 1;
}

/* Show the module's actual reply on OLED when an AT step fails */
static void Wifi_FailShow(const char *stage)
{
	char disp[17];
	u8 i;

	for(i = 0; i < 16 && USART_RX_BUF[i] != '\0'; i++)
		disp[i] = ((u8)USART_RX_BUF[i] >= 0x20) ? USART_RX_BUF[i] : '.';
	disp[i] = '\0';

	OLED_Clear();
	OLED_ShowCH(0, 2, (u8*)stage);
	OLED_ShowCH(0, 4, (u8*)disp);
	delay_ms(1500);
}

/* Send an AT command, retry up to 3 times on timeout / busy */
static u8 Wifi_SendCheck(const char *cmd, u8 timeout)
{
	u8 i;
	for(i = 0; i < 3; i++)
	{
		USART1_SendString(cmd);   /* 整串持锁发送, 避免日志任务插入AT命令中间 */
		if(WiFi_CheckResponse(timeout) == 0)
			return 0;
		USART_RX_Reset();
	}
	return 1;
}

/* WiFi模块初始化(带应答检测与重试) */
static u8 WIFI_Init_Inner(void)
{
	u8 result = 1;
	char cmd[160];

	USART_RX_Reset();
	usart_link_lost = 0;

	LOG_INFO("ESP8266 exit transparent mode");
	DelayS(1);              /* 进入透传退出序列前保持1s静默(+++保护时间) */
	USART1_SendString("+++");
	DelayS(1);

	LOG_INFO("ESP8266 software reset");
	USART1_SendString("AT+RST\r\n");
	delay_ms(2000);
	USART_RX_Reset();

	LOG_INFO("ESP8266 disable echo");
	OLED_Clear();
	OLED_ShowCH(0, 2, (u8*)"AT CMD");
	if(Wifi_SendCheck("ATE0\r\n", 10) != 0)
	{
		LOG_ERROR("ESP8266 ATE0 Error");
		Wifi_FailShow("AT CMD");
		return 1;
	}
	USART_RX_Reset();

	if(Wifi_SendCheck("AT\r\n", 10) != 0)
	{
		LOG_ERROR("ESP8266 AT Error");
		Wifi_FailShow("AT CMD");
		return 1;
	}
	LOG_INFO("ESP8266 AT OK");

	LOG_INFO("ESP8266 set CWMODE=3");
	OLED_Clear();
	OLED_ShowCH(0, 2, (u8*)"SET MODE");
	if(Wifi_SendCheck("AT+CWMODE=3\r\n", 15) != 0)
	{
		LOG_ERROR("CWMODE Error");
		Wifi_FailShow("SET MODE");
		return 1;
	}
	LOG_INFO("CWMODE OK");

	LOG_INFO("Connecting WiFi");
	OLED_Clear();
	OLED_ShowCH(0, 2, (u8*)"JOIN AP");
	sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
	if(Wifi_SendCheck(cmd, 50) != 0)   /* CWJAP单次超时5s(路由器响应慢时更容易成功) */
	{
		LOG_ERROR("WiFi Connect Error");
		Wifi_FailShow("JOIN AP");
		return 1;
	}
	LOG_INFO("WiFi Connected");

	/* let the module finish DHCP before sending more commands */
	delay_ms(1000);
	USART_RX_Reset();

	LOG_INFO("ESP8266 set CIPMUX=0");
	OLED_Clear();
	OLED_ShowCH(0, 2, (u8*)"SET MUX");
	if(Wifi_SendCheck("AT+CIPMUX=0\r\n", 15) != 0)
	{
		LOG_ERROR("CIPMUX Error");
		Wifi_FailShow("SET MUX");
		return 1;
	}
	LOG_INFO("CIPMUX OK");

	LOG_INFOF("TCP connect %s:%d", WIFI_SERVER, WIFI_PORT);
	OLED_Clear();
	OLED_ShowCH(0, 2, (u8*)"TCP LINK");
	sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", WIFI_SERVER, WIFI_PORT);
	if(Wifi_SendCheck(cmd, 30) != 0)
	{
		LOG_ERROR("TCP Connect Error");
		Wifi_FailShow("TCP LINK");
		return 1;
	}
	LOG_INFO("TCP Connected");

	LOG_INFO("ESP8266 enable transparent mode");
	OLED_Clear();
	OLED_ShowCH(0, 2, (u8*)"TCP MODE");
	if(Wifi_SendCheck("AT+CIPMODE=1\r\n", 15) != 0)
	{
		LOG_ERROR("CIPMODE Error");
		Wifi_FailShow("TCP MODE");
		return 1;
	}
	LOG_INFO("CIPMODE OK");

	LOG_INFO("ESP8266 CIPSEND");
	OLED_Clear();
	OLED_ShowCH(0, 2, (u8*)"SEND");
	if(Wifi_SendCheck("AT+CIPSEND\r\n", 15) != 0)
	{
		LOG_ERROR("CIPSEND Error");
		Wifi_FailShow("SEND");
		return 1;
	}
	LOG_INFO("CIPSEND OK");

	sprintf(cmd, "cmd=1&uid=%s&topic=%s\r\n", WIFI_UID, WIFI_TOPIC_LED);
	USART1_SendString(cmd);

	g_plant.sys.connected = 1;
	usart_link_lost = 0;
	result = 0;
	LOG_INFO("Cloud subscribe OK");

	return result;
}

/* WiFi初始化入口: 设置忙标志(Log任务据此暂停打印, 保护AT/透传时序) */
u8 WIFI_Init(void)
{
	u8 r;

	g_wifi_busy = 1;
	r = WIFI_Init_Inner();
	g_wifi_busy = 0;
	return r;
}

/* 向云平台上报传感器与执行机构状态 */
void Ping(void)
{
	char postData[200];

	if(!g_plant.sys.connected)
	{
		Error_Set(ERR_CLOUD_OFFLINE);
		LOG_ERROR("Cloud Disconnect");
		return;
	}
	else
	{
		Error_Clear(ERR_CLOUD_OFFLINE);
	}

	sprintf(postData,
		"cmd=2&uid=%s&topic=%s&msg=#%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;#\r\n",
		WIFI_UID, WIFI_TOPIC_DATA,
		g_plant.sensor.soil_humi, g_plant.alarm.soil, g_plant.threshold.soil_l,
		g_plant.sensor.light, g_plant.alarm.light, g_plant.threshold.light_l,
		g_plant.sensor.temperature, g_plant.alarm.temp, g_plant.threshold.temp_h,
		g_plant.sensor.humidity, g_plant.alarm.humi, g_plant.threshold.humi_l,
		g_plant.sys.run_mode, WATER, LED_zm, FAN, JSQ,
		g_plant.sys.error);
	USART1_SendString(postData);   /* 整串发送, 保证一行数据不被日志拆散 */

	LOG_DEBUG("Cloud data uploaded");
}

#define CMD_HEAD 'A'
#define CMD_LIGHT 'B'
#define CMD_TEMP 'C'
#define CMD_HUMI 'D'
#define CMD_END1 'S'
#define CMD_END2 'Z'

/* 解析两位十进制数; 含非数字字符返回-1 */
static int Parse2Digits(const char *p)
{
	if(p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9')
		return -1;
	return (p[0] - '0') * 10 + (p[1] - '0');
}

/* 整词匹配: 要求前后都不是字母/数字, 避免 "waterfall" 之类误触发 */
static u8 Str_HasWord(const char *hay, const char *word)
{
	const char *p = hay;
	u16 wlen = strlen(word);

	while((p = strstr(p, word)) != NULL)
	{
		if((p == hay || !(((p[-1] >= 'a') && (p[-1] <= 'z')) ||
		                  ((p[-1] >= 'A') && (p[-1] <= 'Z')) ||
		                  ((p[-1] >= '0') && (p[-1] <= '9')))) &&
		   (p[wlen] == '\0' || !(((p[wlen] >= 'a') && (p[wlen] <= 'z')) ||
		                         ((p[wlen] >= 'A') && (p[wlen] <= 'Z')) ||
		                         ((p[wlen] >= '0') && (p[wlen] <= '9')))))
			return 1;
		p += wlen;
	}
	return 0;
}

/* APP远程指令: 设置阈值 / 模式切换 / 手动执行机构 */
void APP_Control(void)
{
	u16 len;
	u16 reg;
	int value;
	u8 threshold_dirty = 0;

	if(reception == 1)
	{
		LOG_DEBUG("APP CMD received");

		len = USART_RX_STA & 0x3fff;
		if(len >= sizeof(USART_RX_BUF))
			len = sizeof(USART_RX_BUF) - 1;
		USART_RX_BUF[len] = '\0';	/* NUL-terminate for strstr() */
		for(reg = 0; reg + 4 < len; reg++)
		{
			if(USART_RX_BUF[reg] == CMD_HEAD
			&& USART_RX_BUF[reg + 3] == CMD_END1
			&& USART_RX_BUF[reg + 4] == CMD_END2)
			{
				value = Parse2Digits(&USART_RX_BUF[reg + 1]);
				if(value < 0)
				{
					LOG_WARN("Soil Threshold Format Invalid");
				}
				else if(value > 100)
				{
					LOG_WARN("Soil Threshold Invalid");
				}
				else
				{
					g_plant.threshold.soil_l = value;
					threshold_dirty = 1;
					LOG_INFOF("APP Set Soil Threshold=%u", g_plant.threshold.soil_l);
				}
			}
			if(USART_RX_BUF[reg] == CMD_LIGHT
			&& USART_RX_BUF[reg + 3] == CMD_END1
			&& USART_RX_BUF[reg + 4] == CMD_END2)
			{
				value = Parse2Digits(&USART_RX_BUF[reg + 1]);
				if(value < 0)
				{
					LOG_WARN("Light Threshold Format Invalid");
				}
				else if(value > 100)
				{
					LOG_WARN("Light Threshold Invalid");
				}
				else
				{
					g_plant.threshold.light_l = value;
					threshold_dirty = 1;
					LOG_INFOF("APP Set Light Threshold=%u", g_plant.threshold.light_l);
				}
			}
			if(USART_RX_BUF[reg] == CMD_TEMP
			&& USART_RX_BUF[reg + 3] == CMD_END1
			&& USART_RX_BUF[reg + 4] == CMD_END2)
			{
				value = Parse2Digits(&USART_RX_BUF[reg + 1]);
				if(value < 0)
				{
					LOG_WARN("Temp Threshold Format Invalid");
				}
				else if(value > 100)
				{
					LOG_WARN("Temp Threshold Invalid");
				}
				else
				{
					g_plant.threshold.temp_h = value;
					threshold_dirty = 1;
					LOG_INFOF("APP Set Temp Threshold=%u", g_plant.threshold.temp_h);
				}
			}
			if(USART_RX_BUF[reg] == CMD_HUMI
			&& USART_RX_BUF[reg + 3] == CMD_END1
			&& USART_RX_BUF[reg + 4] == CMD_END2)
			{
				value = Parse2Digits(&USART_RX_BUF[reg + 1]);
				if(value < 0)
				{
					LOG_WARN("Humi Threshold Format Invalid");
				}
				else if(value > 100)
				{
					LOG_WARN("Humi Threshold Invalid");
				}
				else
				{
					g_plant.threshold.humi_l = value;
					threshold_dirty = 1;
					LOG_INFOF("APP Set Humi Threshold=%u", g_plant.threshold.humi_l);
				}
			}
		}

		if(threshold_dirty)
			Threshold_Save();	/* APP远程修改阈值后持久化到EEPROM */

		if(Str_HasWord(USART_RX_BUF, "mode"))
		{
			if(g_state_machine.current == SYS_AUTO)
			{
				StateMachine_Set(SYS_MANUAL);
				LOG_INFO("Switch to MANUAL Mode");
				Display_ModeTip(1);
			}
			else if(g_state_machine.current == SYS_MANUAL)
			{
				StateMachine_Set(SYS_AUTO);
				LOG_INFO("Switch to AUTO Mode");
				Display_ModeTip(0);
			}
		}

		if(g_state_machine.current == SYS_MANUAL)
		{
			if(Str_HasWord(USART_RX_BUF, "water"))
			{
				WATER = !WATER;
				LOG_INFO(WATER ? "APP Control Pump ON" : "APP Control Pump OFF");
			}
			if(Str_HasWord(USART_RX_BUF, "led"))
			{
				LED_zm = !LED_zm;
				LOG_INFO(LED_zm ? "APP Control LED ON" : "APP Control LED OFF");
			}
			if(Str_HasWord(USART_RX_BUF, "fan"))
			{
				FAN = !FAN;
				LOG_INFO(FAN ? "APP Control Fan ON" : "APP Control Fan OFF");
			}
			if(Str_HasWord(USART_RX_BUF, "jsq"))
			{
				JSQ = !JSQ;
				LOG_INFO(JSQ ? "APP Control Humidifier ON" : "APP Control Humidifier OFF");
			}
		}

		USART_RX_Reset();
	}
}

/* 按键设置阈值参数 - 裸机重构版
 * 仅更新数据(OLED绘制统一由System任务Display_Update完成, 避免多任务撕裂) */
void Set_Parameters(void)
{
    static u32 last_add_tick = 0;
    static u32 last_del_tick = 0;
    static u32 last_set_tick = 0;
    static u8 key_set_pressed = 0;

    if(g_plant.sys.set_cs == 1)
    {
        if(KEY_add == 0 && SysTick_Count - last_add_tick >= 2)
        {
            last_add_tick = SysTick_Count;
            /* 临界区保护读-改-写, 避免与System任务(APP远程改阈值)并发丢更新 */
            taskENTER_CRITICAL();
            if(g_plant.sys.set_cs_number == 1 && g_plant.threshold.soil_l < 100)
                g_plant.threshold.soil_l++;
            if(g_plant.sys.set_cs_number == 2 && g_plant.threshold.light_l < 100)
                g_plant.threshold.light_l++;
            if(g_plant.sys.set_cs_number == 3 && g_plant.threshold.temp_h < 100)
                g_plant.threshold.temp_h++;
            if(g_plant.sys.set_cs_number == 4 && g_plant.threshold.humi_l < 100)
                g_plant.threshold.humi_l++;
            taskEXIT_CRITICAL();
        }

        if(KEY_del == 0 && SysTick_Count - last_del_tick >= 2)
        {
            last_del_tick = SysTick_Count;
            taskENTER_CRITICAL();
            if(g_plant.sys.set_cs_number == 1 && g_plant.threshold.soil_l > 1)
                g_plant.threshold.soil_l--;
            if(g_plant.sys.set_cs_number == 2 && g_plant.threshold.light_l > 1)
                g_plant.threshold.light_l--;
            if(g_plant.sys.set_cs_number == 3 && g_plant.threshold.temp_h > 1)
                g_plant.threshold.temp_h--;
            if(g_plant.sys.set_cs_number == 4 && g_plant.threshold.humi_l > 1)
                g_plant.threshold.humi_l--;
            taskEXIT_CRITICAL();
        }
    }

    if(KEY_set == 0)
    {
        if(SysTick_Count - last_set_tick >= 1)
        {
            last_set_tick = SysTick_Count;
            if(!key_set_pressed)
            {
                key_set_pressed = 1;
                g_plant.sys.set_cs = 1;
                g_plant.sys.set_cs_number++;
                if(g_plant.sys.set_cs_number > 4)
                {
                    g_plant.sys.set_cs = 0;
                    g_plant.sys.set_cs_number = 0;
                    Threshold_Save();   /* 退出设置界面: 阈值持久化到EEPROM */
                }
            }
        }
    }
    else
    {
        key_set_pressed = 0;
    }
}

/* 采集土壤湿度/光照/AHT20温湿度 */
void Sensor_Update(void)
{
	u32 adc_raw;
	u32 soil_v;   /* 电压值×100, 定点计算(STM32F103无FPU, 避免软浮点) */

	adc_raw = Get_Adc_Average(ADC_Channel_1, 10);

	if(adc_raw > ADC_OPEN_CIRCUIT_RAW)
	{
		Error_Set(ERR_ADC_FAIL);
		g_plant.sensor.soil_humi = 0;
		LOG_WARN("ADC Open Circuit");
	}
	else
	{
		Error_Clear(ERR_ADC_FAIL);
		/* 定点化原浮点公式: soil = ((4095-adc)*3.3/4096*100 - 84) / 1.53 */
		soil_v = ((4095u - adc_raw) * 330u) / 4096u;
		if(soil_v > 84u)
			g_plant.sensor.soil_humi = (u8)(((soil_v - 84u) * 100u) / 153u);
		else
			g_plant.sensor.soil_humi = 0;
		if(g_plant.sensor.soil_humi >= 100)
			g_plant.sensor.soil_humi = 100;
	}

	g_plant.sensor.light = 100 - Lsens_Get_Val();
	/* Lsens_Get_Val() returns 0..100, so the difference needs no clamping */

	if(g_plant.aht20.alive)
	{
		static u8 last_read_fail = 0;
		u8 read_fail;

		g_plant.aht20.flag = AHT20_ReadHT(g_plant.aht20.HT);
		read_fail = (g_plant.aht20.flag != 0) ? 1 : 0;
		if(read_fail && !last_read_fail)
		{
			Error_Set(ERR_SENSOR_TIMEOUT);
			LOG_ERROR("AHT20 Read Failed");
		}
		else if(!read_fail)
		{
			Error_Clear(ERR_SENSOR_TIMEOUT);
		}
		last_read_fail = read_fail;

		if(!read_fail)
		{
			StandardUnitCon(&g_plant.aht20);
			g_plant.sensor.temperature = (u8)g_plant.aht20.Temp;
			g_plant.sensor.humidity = (u8)g_plant.aht20.RH;
			LOG_DEBUGF("Temperature=%u Humidity=%u", g_plant.sensor.temperature, g_plant.sensor.humidity);
		}
	}
	else
	{
		g_plant.sensor.temperature = 0;
		g_plant.sensor.humidity = 0;
	}
}

/* 自动模式根据阈值控制执行机构并记录报警标志 */
void Auto_Control(void)
{
	static u8 last_fan = 0xFF;
	static u8 last_jsq = 0xFF;
	static u8 last_water = 0xFF;
	static u8 last_led = 0xFF;
	static u8 last_warn_soil = 0;
	static u8 last_warn_light = 0;
	static u8 last_warn_temp = 0;
	static u8 last_warn_humi = 0;
	u8 fan_out;
	u8 jsq_out;
	u8 water_out;
	u8 led_out;

	/*    */
	if(g_plant.sensor.temperature > g_plant.threshold.temp_h)
	{
		g_plant.alarm.temp = 1;
		fan_out = (g_plant.sys.run_mode == 0) ? 1 : (u8)FAN;
		Log_AlarmEdge(&last_warn_temp, 1, "Temperature High");
	}
	else
	{
		g_plant.alarm.temp = 0;
		fan_out = (g_plant.sys.run_mode == 0) ? 0 : (u8)FAN;
		Log_AlarmEdge(&last_warn_temp, 0, "Temperature High");
	}
	FAN = fan_out;
	if(g_plant.sys.run_mode == 0)
		Log_ActuatorEdge(&last_fan, fan_out, "Fan ON", "Fan OFF");

	/*    */
	if(g_plant.sensor.humidity < g_plant.threshold.humi_l)
	{
		g_plant.alarm.humi = 1;
		jsq_out = (g_plant.sys.run_mode == 0) ? 1 : (u8)JSQ;
		Log_AlarmEdge(&last_warn_humi, 1, "Humidity Low");
	}
	else
	{
		g_plant.alarm.humi = 0;
		jsq_out = (g_plant.sys.run_mode == 0) ? 0 : (u8)JSQ;
		Log_AlarmEdge(&last_warn_humi, 0, "Humidity Low");
	}
	JSQ = jsq_out;
	if(g_plant.sys.run_mode == 0)
		Log_ActuatorEdge(&last_jsq, jsq_out, "Humidifier ON", "Humidifier OFF");

	/*    */
	if(g_plant.sensor.soil_humi < g_plant.threshold.soil_l && g_plant.sensor.soil_humi > 0)
	{
		g_plant.alarm.soil = 1;
		water_out = (g_plant.sys.run_mode == 0) ? 1 : (u8)WATER;
		Log_AlarmEdge(&last_warn_soil, 1, "Soil Humidity Low");
	}
	else
	{
		g_plant.alarm.soil = 0;
		water_out = (g_plant.sys.run_mode == 0) ? 0 : (u8)WATER;
		Log_AlarmEdge(&last_warn_soil, 0, "Soil Humidity Low");
	}
	WATER = water_out;
	if(g_plant.sys.run_mode == 0)
		Log_ActuatorEdge(&last_water, water_out, "Pump ON", "Pump OFF");

	/*    */
	if(g_plant.sensor.light < g_plant.threshold.light_l)
	{
		g_plant.alarm.light = 1;
		led_out = (g_plant.sys.run_mode == 0) ? 1 : (u8)LED_zm;
		Log_AlarmEdge(&last_warn_light, 1, "Light Too Low");
	}
	else
	{
		g_plant.alarm.light = 0;
		led_out = (g_plant.sys.run_mode == 0) ? 0 : (u8)LED_zm;
		Log_AlarmEdge(&last_warn_light, 0, "Light Too Low");
	}
	LED_zm = led_out;
	if(g_plant.sys.run_mode == 0)
		Log_ActuatorEdge(&last_led, led_out, "LED ON", "LED OFF");
}

/* ==================== 阈值EEPROM持久化(24C02, 可选) ==================== */

static u8 s_eeprom_ok = 0;   /* EEPROM可用标志(缺失/未初始化时静默使用默认值) */

/* 上电恢复阈值(须在IIC_Init之后调用) */
void Threshold_Load(void)
{
	u8 buf[4];

	s_eeprom_ok = 0;
	AT24CXX_Read(EEPROM_THRESHOLD_ADDR, buf, 4);

	/* 校验范围: 全部在1~100之间才采信(EEPROM缺失时读出0xFF, 自动回落默认值) */
	if(buf[0] >= 1 && buf[0] <= 100 &&
	   buf[1] >= 1 && buf[1] <= 100 &&
	   buf[2] >= 1 && buf[2] <= 100 &&
	   buf[3] >= 1 && buf[3] <= 100)
	{
		g_plant.threshold.soil_l  = buf[0];
		g_plant.threshold.light_l = buf[1];
		g_plant.threshold.temp_h  = buf[2];
		g_plant.threshold.humi_l  = buf[3];
		s_eeprom_ok = 1;
		LOG_INFO("Threshold restored from EEPROM");
	}
	else
	{
		/* EEPROM缺失/数据无效: 保持默认阈值, 不进入错误状态 */
		LOG_WARN("EEPROM threshold invalid, use defaults");
	}
}

/* 保存阈值到EEPROM(带回读校验) */
void Threshold_Save(void)
{
	u8 buf[4];

	if(!s_eeprom_ok)
		return;

	buf[0] = g_plant.threshold.soil_l;
	buf[1] = g_plant.threshold.light_l;
	buf[2] = g_plant.threshold.temp_h;
	buf[3] = g_plant.threshold.humi_l;

	AT24CXX_Write(EEPROM_THRESHOLD_ADDR, buf, 4);
	AT24CXX_Read(EEPROM_THRESHOLD_ADDR, buf, 4);   /* 回读校验 */

	if(buf[0] != g_plant.threshold.soil_l ||
	   buf[1] != g_plant.threshold.light_l ||
	   buf[2] != g_plant.threshold.temp_h ||
	   buf[3] != g_plant.threshold.humi_l)
	{
		LOG_WARN("EEPROM write verify fail");
	}
}
