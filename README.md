# 智能植物照护盆 PlantCare — FreeRTOS 移植版

基于 **STM32F103C8T6 + FreeRTOS V11.3.0** 的智能植物照护系统：自动采集土壤湿度、环境温湿度与光照，按阈值自动控制水泵/风扇/补光灯/加湿器，经 ESP8266 连接巴法云实现远程监控与 APP 指令控制。

## 功能特性

- **分层架构**：USER / CONFIG / Data / App / HARDWARE / SYSTEM 六层，`main()` 仅做初始化和任务创建，业务全部由 FreeRTOS 任务承载
- **状态机驱动**：Boot → Init → Auto / Manual / Alarm / Error，错误按类型分 1s/5s 节奏重试恢复
- **任务模型**：System（状态机，TIM2 100ms 心跳通知）/ Key（按键与阈值设置）/ Cloud（2s 云上报）/ Log（5s 状态日志含栈水位），空闲任务喂看门狗
- **传感器**：AHT20 温湿度（软件 I2C）、ADC 土壤湿度与光照；采集与换算全部**定点化**（Cortex-M3 无 FPU，避免软浮点开销）
- **执行机构**：水泵、风扇、补光灯、加湿器；自动阈值控制 + APP 手动模式
- **云平台**：ESP8266 + 巴法云 TCP 透传，周期上报 18 项数据，支持远程阈值设置、模式切换与执行机构控制
- **人机交互**：0.96" OLED（I2C）+ 三按键阈值设置界面；OLED 绘制收敛到 System 任务，杜绝多任务画面撕裂
- **可靠性**：
  - IWDG 独立看门狗（约 2.5s 超时，空闲任务钩子喂狗，卡死自动复位）
  - 栈溢出 / 内存申请失败钩子（LED 快速闪烁定位）
  - HardFault 等异常捕获故障现场（PC/xPSR/PSP/MSP）
  - 串口接收每字节 NUL 终止、缓冲写满整帧重收，杜绝 `strstr` 越界
  - WiFi 初始化期间 Log 任务静默，保护 AT 命令与透传时序
  - 断线关键字检测（CLOSED / WIFI DISCONNECT）自动触发重连
- **参数持久化**：阈值自动保存至板载 24C02，上电恢复；EEPROM 缺失或数据无效时静默回落默认值

## 硬件清单

| 模块 | 引脚/接口 |
|---|---|
| 主控 | STM32F103C8T6（64KB Flash / 20KB RAM，72MHz） |
| WiFi 模块 | ESP8266，USART1（PA9/PA10，115200） |
| 温湿度传感器 | AHT20，软件 I2C（PB6/PB7） |
| 土壤湿度/光照 | ADC1（PA0/PA1） |
| 执行机构 | 水泵 PA7、风扇 PB0、补光灯 PB1、加湿器 PB11 |
| 显示屏 | 0.96" OLED SSD1306，软件 I2C（PA4/PA5） |
| 按键 | PB5 / PB8 / PB9（上拉，低电平有效） |
| 运行指示灯 | PC13（低电平点亮） |
| EEPROM | 24C02（与 AHT20 共用 I2C 总线） |

## 快速开始

1. 安装 Keil MDK V5.31+（ARM Compiler 5）
2. 复制 `CONFIG/wifi_credentials.h.template` 为 `CONFIG/wifi_credentials.h`，填写 WiFi 名称/密码与巴法云 UID/主题（该文件已被 `.gitignore` 排除，不会入库）
3. 打开 `USER/PlantCare_FreeRTOS.uvprojx`，全量 Rebuild（0 Error / 0 Warning），烧录 `OBJ/PlantCare_FreeRTOS.hex`
4. 上电运行：Boot 分步初始化 → 连接 WiFi → 进入自动模式（运行灯 1s 翻转）

## 目录结构

```
├─ USER/            main.c / 控制逻辑 control.c / 显示 display.c / FreeRTOSConfig.h / Keil 工程
├─ CONFIG/          参数配置 config.h、WiFi 凭据 wifi_credentials.h（不入库）+ 模板
├─ Data/            全局业务数据 g_plant（传感器/阈值/报警/系统状态）
├─ App/             任务层 app_task、状态机 app_state、日志 app_log、错误 app_error
├─ HARDWARE/        外设驱动：LED/KEY/OLED/ADC/AHT20/IIC/TIMER/24CXX（另有 DHT11/HX711 等未编入）
├─ SYSTEM/          delay（FreeRTOS 适配）/ usart / sys（含 IWDG）
├─ FreeRTOS/        FreeRTOS V11.3.0 内核 + ARM_CM3 移植层 + heap_4 内存管理
├─ STM32F10x_FWLib/ STM32F10x 标准外设库
├─ CORE/            CMSIS 内核文件与启动文件（startup_stm32f10x_md.s）
└─ keilkilll.bat    清理 Keil 构建产物脚本
```

## 文档

- [FreeRTOS 移植说明（详细版）](README_FreeRTOS移植说明.md)：任务划分、同步与互斥、中断配置、编译烧录、迭代修复记录

## 说明

- 本工程为学习用途，从裸机重构版移植而来（详见移植说明文档）
- `HARDWARE\` 下 DHT11 / DS18B20 / HX711 / SGP30 / RS485 / RTC 未编入工程；其中 HX711 引脚（PB0/PB1）与风扇/补光灯冲突，启用前必须改引脚
- 巴法云为明文 TCP 透传，APP 指令协议无校验和，请勿用于生产环境敏感场景
