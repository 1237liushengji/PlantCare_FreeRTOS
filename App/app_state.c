#include "app_state.h"
#include "data.h"
#include "control.h"
#include "display.h"
#include "app_log.h"
#include "app_error.h"
#include "app_task.h"
#include "oled_iic.h"
#include "AHT20.h"
#include "adc.h"
#include "key.h"
#include "led.h"
#include "delay.h"
#include "usart.h"
#include "timer.h"
#include "string.h"

StateMachine_t g_state_machine;

void StateMachine_Init(void)
{
    g_state_machine.current = SYS_BOOT;
    g_state_machine.previous = SYS_BOOT;
    g_state_machine.init_step = 0;
    LOG_INFO("State Machine Init OK");
}

void StateMachine_Set(SysState_t state)
{
    if(g_state_machine.current == state)
        return;
    
    LOG_INFOF("State Change: %u -> %u", g_state_machine.current, state);
    g_state_machine.previous = g_state_machine.current;
    g_state_machine.current = state;
    g_state_machine.init_step = 0;

    /* 模式与业务数据同步, 供Auto_Control/APP上报使用 */
    if(state == SYS_AUTO)
        g_plant.sys.run_mode = 0;    /* 自动 */
    else if(state == SYS_MANUAL)
        g_plant.sys.run_mode = 1;    /* 手动(APP) */
}

void StateMachine_Run(void)
{
    switch(g_state_machine.current)
    {
        case SYS_BOOT:
            Boot_Task();
            break;
        case SYS_INIT:
            Init_Task();
            break;
        case SYS_AUTO:
            Auto_Task();
            break;
        case SYS_MANUAL:
            Manual_Task();
            break;
        case SYS_ALARM:
            Alarm_Task();
            break;
        case SYS_ERROR:
            Error_Task();
            break;
        default:
            StateMachine_Set(SYS_BOOT);
            break;
    }
}

void Boot_Task(void)
{
    static u8 boot_step = 0;
    static u32 step_tick = 0;
    
    if(SysTick_Count - step_tick < 5)
        return;
    step_tick = SysTick_Count;
    
    switch(boot_step)
    {
        case 0:
            LOG_INFO("System Boot - Step 0: Basic Init");
            Data_Init();
            boot_step++;
            break;
        case 1:
            LOG_INFO("System Boot - Step 1: GPIO Init");
            KEY_Init();
            LED_Init();
            Adc_Init();
            IIC_Init();
            Threshold_Load();   /* EEPROM阈值恢复(依赖IIC_Init完成) */
            boot_step++;
            break;
        case 2:
            LOG_INFO("System Boot - Step 2: OLED Init");
            g_plant.sys.error_page = (OLED_Init() != 0) ? 1 : 0;
            if(g_plant.sys.error_page)
                Error_Set(ERR_OLED_INIT);
            else
            {
                OLED_Clear();
                OLED_ShowCH(40, 2, (u8*)"初始化");
            }
            boot_step++;
            break;
        case 3:
            LOG_INFO("System Boot - Step 3: Task Init");
            Boot_Confirm();
            boot_step++;
            break;
        case 4:
            LOG_INFO("Boot Complete -> Enter INIT");
            StateMachine_Set(SYS_INIT);
            break;
    }
}

void Init_Task(void)
{
    static u8 init_step = 0;
    static u8 retry_count = 0;
    static u32 wait_tick = 0;
    
    if(SysTick_Count - wait_tick < 2)
        return;
    
    switch(init_step)
    {
        case 0:
            LOG_INFO("System Init - Step 0: AHT20");
            wait_tick = SysTick_Count;
            retry_count = 0;
            if(AHT20_Init() == 0)
            {
                g_plant.aht20.alive = 1;
                Error_Clear(ERR_AHT20_INIT);
                LOG_INFO("AHT20 Init OK");
                init_step++;
                retry_count = 0;
            }
            else
            {
                retry_count++;
                if(retry_count >= AHT20_INIT_RETRY_MAX)
                {
                    g_plant.aht20.alive = 0;
                    Error_Set(ERR_AHT20_INIT);
                    LOG_ERROR("AHT20 Init Failed");
                    init_step++;
                }
                else
                {
                    LOG_WARNF("AHT20 Retry %u/%u", retry_count, AHT20_INIT_RETRY_MAX);
                }
            }
            break;
        case 1:
            LOG_INFO("System Init - Step 1: WiFi");
            wait_tick = SysTick_Count;
            retry_count = 0;
            if(!g_plant.sys.error_page)
            {
                OLED_Clear();
                OLED_ShowCH(0, 2, (u8*)"Connecting WiFi");
            }
            if(WIFI_Init() == 0)
            {
                Error_Clear(ERR_WIFI_TIMEOUT);
                LOG_INFO("WiFi Connected");
                init_step++;
                retry_count = 0;
            }
            else
            {
                retry_count++;
                if(retry_count >= WIFI_CONNECT_RETRY_MAX)
                {
                    Error_Set(ERR_WIFI_TIMEOUT);
                    LOG_ERROR("WiFi Connect Timeout");
                    init_step++;
                }
                else
                {
                    LOG_WARNF("WiFi Retry %u/%u", retry_count, WIFI_CONNECT_RETRY_MAX);
                }
            }
            break;
        case 2:
            LOG_INFO("System Init - Step 2: Final Check");
            if(!g_plant.sys.error_page)
                OLED_Clear();
            memset(USART_RX_BUF, '\0', sizeof(USART_RX_BUF));
            reception = 0;
            USART_RX_STA = 0;
            
            if(g_plant.sys.error == ERR_OK)
            {
                LOG_INFO("Init Complete -> Enter AUTO");
                StateMachine_Set(SYS_AUTO);
            }
            else
            {
                LOG_INFO("Init Has Errors -> Enter ERROR");
                StateMachine_Set(SYS_ERROR);
            }
            break;
    }
}

void Auto_Task(void)
{
    static u32 last_tick = 0;
    
    APP_Control();
    
    if(SysTick_Count - last_tick >= PERIOD_SENSOR)
    {
        last_tick = SysTick_Count;
        
        Sensor_Update();
        Auto_Control();
        
        Display_Update();       /* 统一显示入口(单任务绘制, 防撕裂) */
        
        LED_RUN = !LED_RUN;
    }
    
    if(g_plant.alarm.soil || g_plant.alarm.light || g_plant.alarm.temp || g_plant.alarm.humi)
    {
        StateMachine_Set(SYS_ALARM);
    }
    
    if(g_plant.sys.error != ERR_OK)
    {
        StateMachine_Set(SYS_ERROR);
    }
}

void Manual_Task(void)
{
    static u32 last_tick = 0;
    
    APP_Control();
    
    if(SysTick_Count - last_tick >= PERIOD_SENSOR)
    {
        last_tick = SysTick_Count;
        
        Sensor_Update();
        
        Display_Update();       /* 统一显示入口(单任务绘制, 防撕裂) */
        
        LED_RUN = !LED_RUN;
    }
    
    if(g_plant.alarm.soil || g_plant.alarm.light || g_plant.alarm.temp || g_plant.alarm.humi)
    {
        StateMachine_Set(SYS_ALARM);
    }
    
    if(g_plant.sys.error != ERR_OK)
    {
        StateMachine_Set(SYS_ERROR);
    }
}

void Alarm_Task(void)
{
    static u32 last_tick = 0;
    
    APP_Control();
    
    if(SysTick_Count - last_tick >= PERIOD_SENSOR)
    {
        last_tick = SysTick_Count;
        
        Sensor_Update();
        Auto_Control();
        
        if(!g_plant.alarm.soil && !g_plant.alarm.light && 
           !g_plant.alarm.temp && !g_plant.alarm.humi)
        {
            if(g_state_machine.previous == SYS_AUTO)
                StateMachine_Set(SYS_AUTO);
            else
                StateMachine_Set(SYS_MANUAL);
            return;
        }
        
        Display_Update();       /* 统一显示入口(单任务绘制, 防撕裂) */
        
        LED_RUN = !LED_RUN;
    }
    
    if(g_plant.sys.error != ERR_OK)
    {
        StateMachine_Set(SYS_ERROR);
    }
}

static void StateMachine_Recover(void);   /* 错误恢复后回到之前的运行状态 */

void Error_Task(void)
{
    static u32 last_retry_tick = 0;
    static u8 retry_count = 0;
    u16 retry_interval;

    /* WiFi/云端类错误慢速重试(5s), 传感器/ADC/OLED等快速重试(1s) */
    retry_interval = (g_plant.sys.error == ERR_WIFI_TIMEOUT ||
                      g_plant.sys.error == ERR_CLOUD_OFFLINE) ? 50 : 10;
    
    if(SysTick_Count - last_retry_tick >= retry_interval)
    {
        last_retry_tick = SysTick_Count;
        LED_RUN = !LED_RUN;               /* 错误态: 运行灯按重试节奏闪烁指示 */
        if(!g_plant.sys.error_page && !g_plant.sys.set_cs)
            Display_ErrorPage();
        
        LOG_ERRORF("Error Recovery Attempt %u", retry_count + 1);
        
        switch(g_plant.sys.error)
        {
            case ERR_WIFI_TIMEOUT:
            case ERR_CLOUD_OFFLINE:
                if(WIFI_Init() == 0)
                {
                    Error_Clear(ERR_WIFI_TIMEOUT);
                    Error_Clear(ERR_CLOUD_OFFLINE);
                    LOG_INFO("WiFi Recovery Success");
                    StateMachine_Recover();
                    return;
                }
                break;
            case ERR_AHT20_INIT:
            case ERR_SENSOR_TIMEOUT:
                if(AHT20_Init() == 0)
                {
                    g_plant.aht20.alive = 1;
                    Error_Clear(ERR_AHT20_INIT);
                    Error_Clear(ERR_SENSOR_TIMEOUT);
                    LOG_INFO("AHT20 Recovery Success");
                    StateMachine_Recover();
                    return;
                }
                break;
            case ERR_ADC_FAIL:
                Adc_Init();
                if(Get_Adc_Average(ADC_Channel_1, 4) < ADC_OPEN_CIRCUIT_RAW)
                {
                    Error_Clear(ERR_ADC_FAIL);
                    LOG_INFO("ADC Recovery Success");
                    StateMachine_Recover();
                    return;
                }
                break;
            case ERR_OLED_INIT:
                if(OLED_Init() == 0)
                {
                    g_plant.sys.error_page = 0;
                    Error_Clear(ERR_OLED_INIT);
                    LOG_INFO("OLED Recovery Success");
                    StateMachine_Recover();
                    return;
                }
                break;
        }
        
        retry_count++;
        if(retry_count >= 10)
        {
            retry_count = 0;
            LOG_WARN("Recovery retries exhausted, keep retrying");
        }
    }
    
    if(g_plant.sys.error == ERR_OK)
    {
        StateMachine_Recover();
    }
}

/* 错误恢复后回到之前的运行状态(自动/手动) */
static void StateMachine_Recover(void)
{
    if(g_state_machine.previous == SYS_MANUAL)
        StateMachine_Set(SYS_MANUAL);
    else
        StateMachine_Set(SYS_AUTO);
}
