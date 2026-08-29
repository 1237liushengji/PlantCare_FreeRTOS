#ifndef __APP_STATE_H
#define __APP_STATE_H

#include "sys.h"

typedef enum
{
    SYS_BOOT = 0,
    SYS_INIT,
    SYS_AUTO,
    SYS_MANUAL,
    SYS_ALARM,
    SYS_ERROR
} SysState_t;

typedef struct
{
    SysState_t current;
    SysState_t previous;
    u8 init_step;
} StateMachine_t;

extern StateMachine_t g_state_machine;

void StateMachine_Init(void);
void StateMachine_Run(void);
void StateMachine_Set(SysState_t state);

void Boot_Task(void);
void Init_Task(void);
void Auto_Task(void);
void Manual_Task(void);
void Alarm_Task(void);
void Error_Task(void);

#endif
