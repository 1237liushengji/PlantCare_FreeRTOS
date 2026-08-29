#ifndef __APP_LOG_H
#define __APP_LOG_H

#include "sys.h"
#include "config.h"

void Log_Print(u8 level, const char *file, u16 line, const char *msg);
void Log_Printf(u8 level, const char *file, u16 line, const char *fmt, ...);

void Log_Debug(const char *msg);
void Log_Info(const char *msg);
void Log_Warn(const char *msg);
void Log_Error(const char *msg);

#if (LOG_LEVEL <= LOG_LEVEL_DEBUG)
#define LOG_DEBUG(msg)        Log_Print(LOG_LEVEL_DEBUG, __FILE__, __LINE__, (msg))
#define LOG_DEBUGF(fmt, ...)  Log_Printf(LOG_LEVEL_DEBUG, __FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#else
#define LOG_DEBUG(msg)        ((void)0)
#define LOG_DEBUGF(fmt, ...)    ((void)0)
#endif

#if (LOG_LEVEL <= LOG_LEVEL_INFO)
#define LOG_INFO(msg)         Log_Print(LOG_LEVEL_INFO, __FILE__, __LINE__, (msg))
#define LOG_INFOF(fmt, ...)   Log_Printf(LOG_LEVEL_INFO, __FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#else
#define LOG_INFO(msg)         ((void)0)
#define LOG_INFOF(fmt, ...)   ((void)0)
#endif

#if (LOG_LEVEL <= LOG_LEVEL_WARN)
#define LOG_WARN(msg)         Log_Print(LOG_LEVEL_WARN, __FILE__, __LINE__, (msg))
#define LOG_WARNF(fmt, ...)   Log_Printf(LOG_LEVEL_WARN, __FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#else
#define LOG_WARN(msg)         ((void)0)
#define LOG_WARNF(fmt, ...)   ((void)0)
#endif

#if (LOG_LEVEL <= LOG_LEVEL_ERROR)
#define LOG_ERROR(msg)        Log_Print(LOG_LEVEL_ERROR, __FILE__, __LINE__, (msg))
#define LOG_ERRORF(fmt, ...)  Log_Printf(LOG_LEVEL_ERROR, __FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#else
#define LOG_ERROR(msg)        ((void)0)
#define LOG_ERRORF(fmt, ...)  ((void)0)
#endif

#endif
