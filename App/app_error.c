#include "app_error.h"
#include "data.h"
#include "app_log.h"

void Error_Init(void)
{
	g_plant.sys.error = ERR_OK;
}

const char *Error_ToString(ErrorCode_t code)
{
	switch(code)
	{
		case ERR_OK:                  return "OK";
		case ERR_AHT20_INIT:          return "AHT20 Init Fail";
		case ERR_SOIL_SENSOR:         return "Soil Sensor Fail";
		case ERR_LIGHT_SENSOR:        return "Light Sensor Fail";
		case ERR_ADC_FAIL:            return "ADC Error";
		case ERR_WIFI_TIMEOUT:        return "WiFi Timeout";
		case ERR_TCP_CONNECT:         return "TCP Connect Fail";
		case ERR_CLOUD_OFFLINE:       return "Cloud Offline";
		case ERR_UART_TIMEOUT:        return "UART Timeout";
		case ERR_PARAMETER_INVALID:   return "Parameter Invalid";
		case ERR_OLED_INIT:           return "OLED Init Fail";
		case ERR_EEPROM_FAIL:         return "EEPROM Fail";
		case ERR_SENSOR_TIMEOUT:      return "Sensor Timeout";
		default:                      return "Unknown Error";
	}
}

void Error_Set(ErrorCode_t code)
{
	if(code == ERR_OK)
		return;

	g_plant.sys.error = code;
	LOG_ERROR(Error_ToString(code));
}

void Error_Clear(ErrorCode_t code)
{
	if(g_plant.sys.error == code)
		g_plant.sys.error = ERR_OK;
}

ErrorCode_t Error_Get(void)
{
	return g_plant.sys.error;
}
