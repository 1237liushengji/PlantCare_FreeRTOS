#include "app_log.h"
#include "stdio.h"
#include "stdarg.h"
#include "string.h"
#include "usart.h"

static const char *Log_LevelTag(u8 level)
{
	switch(level)
	{
		case LOG_LEVEL_DEBUG: return "DEBUG";
		case LOG_LEVEL_INFO:  return "INFO";
		case LOG_LEVEL_WARN:  return "WARN";
		case LOG_LEVEL_ERROR: return "ERROR";
		default:              return "LOG";
	}
}

/* Strip directory prefix from __FILE__ for shorter output */
static const char *Log_FileBase(const char *path)
{
	const char *p;

	p = strrchr(path, '\\');
	if(p != 0)
		return p + 1;
	p = strrchr(path, '/');
	if(p != 0)
		return p + 1;
	return path;
}

void Log_Print(u8 level, const char *file, u16 line, const char *msg)
{
	char buf[192];

	if(level < LOG_LEVEL)
		return;
	if(msg == 0)
		msg = "";

	sprintf(buf, "[%s][%s][%u] %s\r\n",
		Log_LevelTag(level), Log_FileBase(file), (unsigned int)line, msg);
	USART1_SendString(buf);   /* 整行持锁发送, 保证日志不会插进AT命令/上报数据中间 */
}

void Log_Printf(u8 level, const char *file, u16 line, const char *fmt, ...)
{
	char buf[128];
	va_list ap;

	if(level < LOG_LEVEL)
		return;
	if(fmt == 0)
		return;

	va_start(ap, fmt);
#if defined(__CC_ARM)
	vsprintf(buf, fmt, ap);
#else
	vsnprintf(buf, sizeof(buf), fmt, ap);
#endif
	va_end(ap);
	buf[sizeof(buf) - 1] = '\0';

	Log_Print(level, file, line, buf);
}

void Log_Debug(const char *msg)
{
	Log_Print(LOG_LEVEL_DEBUG, "app_log.c", 0, msg);
}

void Log_Info(const char *msg)
{
	Log_Print(LOG_LEVEL_INFO, "app_log.c", 0, msg);
}

void Log_Warn(const char *msg)
{
	Log_Print(LOG_LEVEL_WARN, "app_log.c", 0, msg);
}

void Log_Error(const char *msg)
{
	Log_Print(LOG_LEVEL_ERROR, "app_log.c", 0, msg);
}
