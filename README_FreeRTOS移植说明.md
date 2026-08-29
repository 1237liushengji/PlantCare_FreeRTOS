# 智能植物照护盆 FreeRTOS 移植版说明（分层架构）

本工程参照裸机重构版 `源程序 - 重构` 的分层结构移植到 FreeRTOS：
各层封装为独立 .c/.h，`main()` 只做硬件初始化和调用任务创建函数，业务全部由任务承载。

## 1. 工程信息

- 主控：STM32F103C8T6（64KB Flash / 20KB RAM），目标器件已修正为 C8
- 工具链：Keil MDK V5.31 + ARM Compiler V5.06（armcc），C99
- 内核：FreeRTOS V11.3.0（位于本树 `FreeRTOS\` 目录）
- 移植层：`FreeRTOS\portable\RVDS\ARM_CM3`；内存管理 `heap_4.c`，堆 8KB
- 系统节拍：SysTick 1ms；业务心跳：TIM2 100ms（驱动状态机）
- 看门狗：IWDG 约 2.5s 超时（LSI 40kHz/64 分频/1562 重装），由空闲任务钩子喂狗，系统卡死自动复位

## 2. 分层结构

```
USER/main.c        仅初始化 + App_Task_Init() + vTaskStartScheduler()
├─ CONFIG/         参数配置(阈值/WiFi/定时器/日志等级)
├─ Data/           全局数据 g_plant(传感器/阈值/报警/系统状态)
├─ App/
│   ├─ app_task     FreeRTOS任务层: 创建System/Key/Cloud/Log四个任务 + 互斥量
│   ├─ app_state    状态机: Boot->Init->Auto/Manual/Alarm/Error
│   ├─ app_log      日志输出(printf互斥)
│   └─ app_error    错误码与错误队列
├─ USER/control.c  传感器采集/自动控制/WiFi初始化/APP指令解析/阈值EEPROM持久化
├─ USER/display.c  OLED各页面(统一显示入口Display_Update, 单任务绘制)
├─ SYSTEM/         delay(FreeRTOS版) / usart(printf互斥+接收修复) / sys(含IWDG)
└─ HARDWARE/       驱动(OLED驱动层自带互斥, 24CXX阈值存储, AHT20等)
```

`main()` 全文：

```c
int main(void)
{
    HZ = GB16_NUM();
    delay_init();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    uart1_init(115200);
    TIM2_Int_Init(TIMER_ARR, TIMER_PSC);
    App_Task_Init();       // 创建全部任务
    vTaskStartScheduler();
    while(1);
}
```

## 3. 任务划分

| 任务 | 优先级 | 栈(Word) | 周期 | 功能 |
|------|--------|----------|------|------|
| System | 2 | 512 | TIM2 100ms 通知 | 运行状态机：启动流程/自动/手动/报警/错误恢复 |
| Key | 1 | 256 | 100ms 扫描 | 按键消抖与阈值设置界面 |
| Cloud | 1 | 256 | 2s | 向巴法云上报传感器与执行机构状态 |
| Log | 1 | 256 | 5s | 打印系统状态日志 |
| idle | 0 | 128 | - | FreeRTOS 空闲任务 |

状态机内部仍按重构版节奏工作：Boot 分步初始化（GPIO→OLED→任务确认），
Init 阶段带重试地初始化 AHT20 与 WiFi，之后进入 Auto/Manual；
传感器采集与自动控制在 Auto 状态下每 500ms 执行一次（`PERIOD_SENSOR`）。

## 4. 同步与互斥

- **TIM2 → System 任务**：任务通知（`vTaskNotifyGiveFromISR` / `ulTaskNotifyTake`）
- **串口打印**：`USART1_TxMutex` 在 `fputc` 内逐字符加锁，`sprintf/vsprintf`
  在 control/display/log 层加锁，多任务 printf 不乱码
- **OLED**：互斥量放在 OLED 驱动层（`OLED_Clear/ShowCH/ShowNum/Init` 内部加锁），
  显示任务、按键任务、WiFi 提示互不干扰
- **串口接收**：帧收满（`...@*`）后关闭 RXNE，处理完再打开，避免缓冲被覆盖；
  `WiFi_CheckResponse` 用 `USART_RX_Reset()` 统一清缓冲并重新使能；
  ISR 每字节后补 NUL 终止符，缓冲写满整帧重收，杜绝 `strstr` 越界
- **看门狗**：IWDG 由空闲任务钩子喂狗，任何任务死循环/故障停车都会触发复位自恢复
- **WiFi 初始化**：`g_wifi_busy` 忙标志使 Log 任务暂停打印，避免日志插入 AT/透传时序

## 5. 中断配置（FreeRTOS 要求）

NVIC 分组 2。可调用 FreeRTOS API 的中断优先级数值必须 ≥ 5：

| 中断 | 抢占优先级 | 说明 |
|------|-----------|------|
| TIM2 | 5 | 100ms 心跳，ISR 中仅累加 SysTick_Count + 任务通知 |
| USART1 | 6 | ESP8266 接收中断 |
| SysTick | 内核 | 1ms 节拍（delay.c 调 xPortSysTickHandler） |
| SVC/PendSV | 内核 | 由 port.c 提供（FreeRTOSConfig.h 映射到 SVC_Handler/PendSV_Handler） |

## 6. 编译与烧录

1. Keil 打开 `USER\PlantCare_FreeRTOS.uvprojx`
2. 编译目标 `PlantCare_FreeRTOS`，烧录 `OBJ\PlantCare_FreeRTOS.hex`
3. 本机实测：`0 Error(s), 0 Warning(s)`

```
Program Size: Code=49526 RO-data=5838 RW-data=240 ZI-data=10760
Flash ≈ 54.3KB / 64KB
RAM   ≈ 10.7KB / 20KB（含 FreeRTOS 8KB 堆）
```

## 7. 需要按实际修改的配置

参数集中在 `CONFIG\config.h`，**WiFi 凭据/云 UID 单独存放于
`CONFIG\wifi_credentials.h`**（该文件已加入 `.gitignore`，不入版本库；
新建工程请复制 `CONFIG\wifi_credentials.h.template` 填写）：

```c
#define WIFI_SSID       "your_wifi_ssid"     // WiFi 名称
#define WIFI_PASSWORD   "your_wifi_password" // WiFi 密码
#define WIFI_SERVER     "bemfa.com"          // 巴法云(在config.h)
#define WIFI_UID        "your_bemfa_uid"     // 用户UID
#define WIFI_TOPIC_LED  "your_topic"         // 控制主题
#define WIFI_TOPIC_DATA "your_topic"         // 数据主题
```

阈值默认值、TIM2 周期（`TIMER_ARR/TIMER_PSC`）、日志等级在 `CONFIG\config.h`。
阈值修改后自动写入板载 24C02（`HARDWARE\24CXX`），上电自动恢复（EEPROM 缺失
或数据无效时静默使用默认值，不影响运行）。

## 8. 与裸机重构版的差异

1. `app_task.c`：原协作式调度器（TaskList/Task_Run）替换为 FreeRTOS 任务，
   `Boot_Confirm()` 保留为 Boot 流程确认点；`SysTick_Count`（100ms）由 TIM2 中断累加。
2. `main.c`：删去 `StateMachine_Run()`/`Task_Run()` 大循环，只创建任务并启动调度器。
3. 状态机、控制、显示、错误、日志各层逻辑保持不变，仅增加并发保护。
4. 芯片配置修正为 STM32F103C8 + `startup_stm32f10x_md.s`（裸机重构版仍是 C6 内存配置+HD 启动文件）。
5. OLED 字库加 `const` 移入 Flash，释放约 2.7KB RAM。
6. 串口接收修复：去掉无谓的双读、帧处理期间关 RXNE、解析越界保护、len/reg 改 16 位。
7. 中断优先级调整满足 FreeRTOS 要求；`OLED_Fill` 每页 130 列修正为 128。

## 9. 2026-08 迭代修复记录

### 修复的 Bug
- `control.c`：`USART_RX_Reset()` 原实现无限递归（调用自身），一进 WiFi 初始化即栈溢出死机；
  现改为关中断后清空 `USART_RX_BUF`、`USART_RX_STA`、`reception` 并重新使能 RXNE。
- `oled_iic.c`：`GB16_NUM()` 使用未初始化局部变量 `HZ_NUM`，导致 OLED 中文不显示或越界；
  已初始化并改为按 UTF-8 首字节计数。
- 日志/AT 命令/云上报改为整串持锁发送（`USART1_SendString`），
  避免 Log 任务字节级插入破坏 AT 命令；`Display_SetParameters` 去掉错误的 `USART1_TxMutex`，
  改用局部缓冲，不再依赖共享 `display_buf`（该字段已从 `PlantCare_t` 移除）。
- `app_state.c`：`run_mode` 随状态机切换（AUTO/MANUAL）同步更新，修复 APP 始终显示自动、
  手动模式报警态被自动逻辑覆盖的问题；错误恢复后回到进入错误前的模式。
- `app_state.c`：WiFi/云端类错误改为 5 秒慢速重试，其余错误 1 秒快速重试。
- `usart.c`：ISR 内新增 ESP8266 断线关键字检测（`CLOSED`/`WIFI DISCONNECT`），
  Cloud 任务据此置 `ERR_CLOUD_OFFLINE` 并触发重连。
- `oled_iic.c`：`Write_IIC_Command/Data/Byte` 返回 ACK 状态，`OLED_Init()` 真实检测 OLED 是否存在。
- `control.c`：WiFi 应答改为"独立成行"匹配（避免 `BROKEN` 误判为 OK）；
  APP 指令值只接受十进制数字，`mode/water/led/fan/jsq` 改为整词匹配（避免 `waterfall` 误触发）。
- `AHT20.c`：状态字读取失败时返回 0xFF，不再使用未初始化数据。
- `timer.h`：删除未定义的 `TIM3_Int_Init`/`SG90_Init`，补上 `TIM3_PWM_Init` 声明。
- `adc.c`：单通道扫描模式修正为 `DISABLE`。

### 可靠性/可维护性
- 任务栈加大：Key 320 字、Cloud/Log 384 字；Log 任务每 5 秒打印四个任务的栈高水位
  （`uxTaskGetStackHighWaterMark`），方便实测校准。
- 栈溢出/内存申请失败钩子改为 LED 快速闪烁（`Fault_Loop`），不再无声死循环。
- `App_Task_Init()` 现在会调用 `StateMachine_Init()`，并检查互斥量/任务创建结果。
- 全部自有源码统一为 UTF-8（带 BOM，Keil AC5 兼容），字库/显示代码已按 UTF-8 适配；
  旧重复目录 `HARDWARE\_legacy_HARDWARE_duplicate`、`SYSTEM\_legacy_SYSTEM_duplicate` 已归档改名。
- WiFi 初始化各步骤超时缩短（CWJAP 单次 3 秒等），失败总耗时明显下降。

### 待办/说明
- 阈值已接入 24CXX（24C02）持久化：上电恢复、修改即存（带回读校验）；EEPROM 缺失时自动回落默认值。
- `HARDWARE\` 下 DHT11/DS18B20/HX711/SGP30/RS485/RTC 未编入工程；
  其中 HX711 引脚（PB0/PB1）与 FAN/LED_zm 冲突，启用前必须改引脚。
- APP 控制指令仍为"子串+固定格式"协议，无校验和；如需更强的可靠性可加帧校验
  （需 APP 端同步修改协议）。
- 编译器优化级别为 `-O0`（`reception`/`USART_RX_STA` 已加 volatile，可安全尝试 -O1+）。
