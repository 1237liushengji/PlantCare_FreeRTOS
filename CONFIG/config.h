#ifndef __CONFIG_H
#define __CONFIG_H

/* ==================== 默认阈值参数 ==================== */
#define DEFAULT_TEMP_THRESHOLD      30      /* 环境温度阈值 ℃ */
#define DEFAULT_HUMIDITY_THRESHOLD  60      /* 环境湿度阈值 %RH */
#define DEFAULT_SOIL_THRESHOLD      60      /* 土壤湿度阈值 %(低于此值浇水; 显示: <阈值=过低, 阈值~阈值+20=正常, >阈值+20=过高) */
#define DEFAULT_LIGHT_THRESHOLD     50      /* 光照强度阈值 lux */

/* ==================== WiFi / 云平台参数 ==================== */
/* WiFi名称/密码/UID/主题为敏感信息, 独立存放于 wifi_credentials.h
 * (该文件已加入 .gitignore, 不入版本库; 参见 wifi_credentials.h.template) */
#include "wifi_credentials.h"

#define WIFI_SERVER     "bemfa.com"         /* 巴法云服务器 */
#define WIFI_PORT       8344                /* 巴法云端口 */

/* ==================== 定时器参数 ==================== */
/* TIM2: 72MHz / (PSC+1) / (ARR+1) = 72MHz/7200/1000 = 10Hz -> 100ms */
#define TIMER_ARR       999
#define TIMER_PSC       7199

/* ==================== Error / recovery ==================== */
#define AHT20_INIT_RETRY_MAX  3
#define WIFI_CONNECT_RETRY_MAX 3
#define ADC_OPEN_CIRCUIT_RAW  4090

/* ==================== Log ==================== */
#define LOG_LEVEL_DEBUG  0
#define LOG_LEVEL_INFO   1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_ERROR  3
#ifndef LOG_LEVEL
#define LOG_LEVEL        LOG_LEVEL_INFO
#endif

#endif
