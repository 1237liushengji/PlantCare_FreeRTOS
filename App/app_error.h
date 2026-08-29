#ifndef __APP_ERROR_H
#define __APP_ERROR_H

#include "sys.h"

typedef enum
{
	ERR_OK = 0,
	ERR_AHT20_INIT,
	ERR_SOIL_SENSOR,
	ERR_LIGHT_SENSOR,
	ERR_ADC_FAIL,
	ERR_WIFI_TIMEOUT,
	ERR_TCP_CONNECT,
	ERR_CLOUD_OFFLINE,
	ERR_UART_TIMEOUT,
	ERR_PARAMETER_INVALID,
	ERR_OLED_INIT,
	ERR_EEPROM_FAIL,
	ERR_SENSOR_TIMEOUT,
	ERR_UNKNOWN
} ErrorCode_t;

void Error_Init(void);
void Error_Set(ErrorCode_t code);
void Error_Clear(ErrorCode_t code);
ErrorCode_t Error_Get(void);
const char *Error_ToString(ErrorCode_t code);

#endif
