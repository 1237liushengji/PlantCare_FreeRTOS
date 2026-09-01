# 智能植物照护盆 PlantCare — FreeRTOS 移植版

> STM32F103C8T6 + FreeRTOS V11.3.0 的物联网植物养护系统：自动采集土壤湿度/环境温湿度/光照，
> 按阈值自动控制水泵/风扇/补光灯/加湿器，经 ESP8266 + 巴法云实现远程监控与 APP 指令控制。

---

## 目录

- [1. 项目简介](#1-项目简介)
- [2. 硬件组成](#2-硬件组成)
- [3. 软件架构](#3-软件架构)
- [4. 构建指南](#4-构建指南)
- [5. 使用说明](#5-使用说明)
- [6. 测试过程与结果](#6-测试过程与结果)
- [7. 开发问题记录](#7-开发问题记录)
- [8. 版本历史](#8-版本历史)
- [9. 许可与声明](#9-许可与声明)

---

## 1. 项目简介

| 项目 | 内容 |
|---|---|
| 主控 | STM32F103C8T6（64KB Flash / 20KB RAM，72MHz） |
| 内核 | FreeRTOS V11.3.0（ARM_CM3 移植 + heap_4，8KB 堆） |
| 工具链 | Keil MDK V5.31 + ARM Compiler 5（AC5，C99，-O0） |
| 通信 | ESP8266（USART1，115200）+ 巴法云 TCP 透传 |
| 文档 | 详细移植说明见 [README_FreeRTOS移植说明.md](README_FreeRTOS移植说明.md) |

**核心功能**
- 四路传感器采集：土壤湿度（ADC）、光照（ADC）、AHT20 温湿度（I2C），全部定点化计算
- 四路执行机构：水泵、风扇（**PWM 无级调速**）、补光灯、加湿器
- 自动/手动双模式，APP 远程阈值设置与执行机构控制
- 阈值自动持久化到板载 24C02，掉电不丢失
- 独立看门狗 + 故障现场捕获 + 栈水位监控，具备自恢复能力

**技术亮点**
- 分层架构：USER / CONFIG / Data / App / HARDWARE / SYSTEM 六层
- 状态机驱动：Boot → Init → Auto / Manual / Alarm / Error，错误分类重试恢复
- 单任务状态机写入者 + 三类互斥量（串口 / OLED / I2C 总线）的并发安全设计
- 传感器故障仅显示异常（`--`），不干扰其余功能与显示

---

## 2. 硬件组成

### 引脚分配表

| 功能 | 引脚 | 说明 |
|---|---|---|
| 光照传感器 | PA0 | ADC1 通道0，0~100 亮度 |
| 土壤湿度传感器 | PA1 | ADC1 通道1，开路(>4090)判异常 |
| OLED SDA / SCL | PA4 / PA5 | SSD1306，软件 I2C，地址 0x78 |
| 水泵 WATER | PA7 | 推挽输出 |
| USART1 TX / RX | PA9 / PA10 | ESP8266，115200 8N1 |
| 风扇 FAN | **PB0** | **TIM3_CH3 PWM（25kHz）**，无级调速 |
| 补光灯 LED_zm | PB1 | 推挽输出 |
| 按键 减/加/设 | PB5 / PB8 / PB9 | 上拉输入，低电平有效 |
| AHT20 + 24C02 | PB6 / PB7 | 软件 I2C **共用总线**（互斥保护） |
| 加湿器 JSQ | PB11 | 推挽输出 |
| 运行指示灯 LED_RUN | PC13 | 低电平点亮；1Hz 正常 / 慢闪=错误 / 快闪=故障 |

> ⚠️ HX711 驱动未编入工程（其引脚 PB0/PB1 与风扇/补光灯冲突，启用前必须改引脚）。

### 供电建议
- **ESP8266 独立供电**（3.3V ≥500mA，配 100µF+0.1µF 电容），与 STM32 仅共地——避免发射电流拉垮系统
- STM32 板 3.3V 由 USB/板载 LDO 提供

---

## 3. 软件架构

### 3.1 分层结构

```
main() ── 仅初始化 + App_Task_Init() + vTaskStartScheduler()
├─ CONFIG/     参数配置(config.h) + WiFi凭据(wifi_credentials.h, 不入库)
├─ Data/       全局业务数据 g_plant(传感器/阈值/报警/系统状态)
├─ App/        任务层/状态机/日志/错误
│   ├─ app_task   任务创建、互斥量、钩子(栈溢出/堆失败/空闲喂狗)
│   ├─ app_state  状态机: Boot→Init→Auto/Manual/Alarm/Error
│   ├─ app_log    分级日志(整串持锁发送)
│   └─ app_error  错误码
├─ USER/       control(采集/控制/WiFi/指令/阈值持久化) + display(统一显示入口)
├─ SYSTEM/     delay(FreeRTOS版) / usart / sys(含IWDG)
└─ HARDWARE/   LED/KEY/OLED/ADC/AHT20/IIC/TIMER(风扇PWM)/24CXX
```

### 3.2 RTOS 任务模型

| 任务 | 优先级 | 栈(字) | 周期 | 功能 |
|---|---|---|---|---|
| System | 2 | 512 | TIM2 100ms 通知 | 状态机调度（唯一状态机写入者） |
| Key | 1 | 320 | 100ms | 按键消抖、阈值设置（仅改数据） |
| Cloud | 1 | 384 | 2s | 巴法云数据上报、断线检测 |
| Log | 1 | 384 | 5s | 状态日志 + 四任务栈高水位 |
| idle | 0 | 128 | — | 空闲任务（**喂 IWDG**） |

### 3.3 状态机

```
Boot(5步) → Init(AHT20重试→WiFi重试→终检) → Auto ──报警──→ Alarm ──恢复──→ Auto/Manual
                                              └──错误──→ Error ──重试成功──→ Auto/Manual
```

- **Auto**：500ms 采集 + 阈值自动控制（浇水/风扇调速/补光/加湿）+ 状态显示
- **Manual**：APP 手动控制执行机构，仍采集与显示
- **Alarm**：任一指标越限进入，恢复后按原模式返回
- **Error**：按错误类型 1s（传感器）/ 5s（WiFi/云）重试；进入时**关闭全部执行机构**

### 3.4 同步与互斥

| 资源 | 保护方式 |
|---|---|
| 串口输出 | `USART1_TxMutex`（整串持锁，日志不插入 AT 命令） |
| OLED 绘制 | `OLED_Mutex` + 显示收敛到 System 任务单任务绘制 |
| 软件 I2C 总线（PB6/7） | `IIC_Mutex`（AHT20 采集与 24C02 阈值保存分属不同任务） |
| 阈值读-改-写 | `taskENTER_CRITICAL` 临界区 |
| 100ms 心跳 | TIM2 ISR → `vTaskNotifyGiveFromISR` → System 任务 |
| ESP8266 接收 | 帧收满关 RXNE → 处理 → 重开；每字节补 NUL 防越界 |
| 看门狗 | IWDG 2.5s，空闲任务钩子喂狗 |

### 3.5 中断配置

NVIC 分组 **4**（4 位抢占）。可调 FreeRTOS API 的中断优先级数值必须 ≥ 5：

| 中断 | 优先级 | 说明 |
|---|---|---|
| TIM2 | 5 | 100ms 心跳（ISR 调 vTaskNotifyGiveFromISR） |
| USART1 | 6 | ESP8266 接收 |
| SysTick / SVC / PendSV | 内核 | 由 FreeRTOS 移植层提供 |

---

## 4. 构建指南

1. 安装 **Keil MDK V5.31+**（含 ARM Compiler 5、STM32F1xx_DFP 2.3.0 包）
2. 复制 `CONFIG/wifi_credentials.h.template` 为 `CONFIG/wifi_credentials.h`，填写 WiFi/云凭据
   （该文件已被 `.gitignore` 排除，不入库）
3. 打开 `USER/PlantCare_FreeRTOS.uvprojx`（目标名 `PlantCare_FreeRTOS`）
4. **Rebuild all** → 应 0 Error / 0 Warning
5. ST-Link 下载（**Verify OK**），烧录 `OBJ/PlantCare_FreeRTOS.hex`
6. 调试建议：ST-Link 设 Port=SW、1MHz；`main.c:24` 的 `IWDG_Init()` 可在调试期临时注释

---

## 5. 使用说明

### 按键
| 按键 | 功能 |
|---|---|
| 设(PB9) | 进入/切换阈值设置项（土壤→光照→温度→湿度→退出） |
| 加(PB8) / 减(PB5) | 调整当前阈值（1~100） |

### APP（巴法云）
- 控制主题 `WIFI_TOPIC_LED` 下发指令：`mode` 切自动/手动；手动下 `water`/`led`/`fan`/`jsq` 开关执行机构
- 阈值设置指令：`A##SZ`（土壤）/ `B##SZ`（光照）/ `C##SZ`（温度）/ `D##SZ`（湿度），`##` 为 0~99 十进制
- 数据主题 `WIFI_TOPIC_DATA`：`cmd=2&uid=...&msg=#soil;soilAlarm;soilTh;light;...;error;#`（18 项）

### 显示
- 土壤行：`数值% + 状态`（`过低` < 阈值 / `正常` 阈值~阈值+20 / `过高` > 阈值+20；**0%=异常显示 `--`**）
- 光照/TEMP/湿度行：数值 + `超标`/`正常`
- 传感器故障仅故障项显示 `--`，其余照常显示；系统级错误（WiFi/云/OLED）显示全屏错误页

### 风扇调速（自动模式）
| 温度 | 转速 |
|---|---|
| ≤ 阈值(30℃) | 0%（停） |
| 阈值~阈值+5℃（30~35℃） | 50% |
| > 阈值+5℃（35℃以上） | 100% |

---

## 6. 测试过程与结果

> 固件在真实硬件上迭代验证；以下为当前版本的验证清单与预期结果。

| 类别 | 用例 | 预期结果 | 状态 |
|---|---|---|---|
| 编译 | Rebuild all | 0 Error / 0 Warning | ✅（本机实测） |
| 启动 | 上电 | PC13 1Hz、OLED 初始化→Connecting WiFi→主页面 | ✅ |
| 采集 | 传感器接入 | 土壤/光照/温湿度数值随环境变化 | ✅ |
| 控制 | 土壤<阈值 | 水泵开启，回升后关闭 | ✅ |
| 控制 | 温度 30~35℃ | 风扇 50% 转速（无高频啸叫） | ✅ |
| 控制 | 温度 >35℃ | 风扇 100% | ✅ |
| 显示 | 土壤 0%（拔传感器） | 土壤行 `--`，温度/湿度/光照照常，无 ErrorADC | ✅ |
| 持久化 | 改阈值→断电→上电 | 阈值恢复（24C02 回读校验） | ✅ |
| 云平台 | 2s 周期 | 数据上报、APP 指令响应 | ✅ |
| 看门狗 | 人为卡死 | 2.5s 内自动复位恢复 | ✅ |
| 边界 | 中断风暴/长时间运行 | 栈水位监控无异常、无复位 | ⏳ 建议 72h 长稳 |

**待补充**：长稳压测（72h）、WiFi 断线重连压测、极端温湿度边界。

---

## 7. 开发问题记录

> 本工程从裸机重构版移植到 FreeRTOS 过程中踩过的重要坑，均已修复并归档，供后续开发参考。

### 7.1 FreeRTOS 中断优先级校验失败（configASSERT at port.c）
- **现象**：上电后 PC13 不亮、OLED 黑屏，系统"毫无反应"；调试停在 `port.c` 的 `configASSERT(ucCurrentPriority >= ucMaxSysCallPriority)`
- **原因**：TIM2 中断设抢占优先级 5，但 NVIC 分组 2 只有 2 位抢占位（0~3）；标准库算出 `(5<<2|0)<<4 = 0x40`，小于 `configMAX_SYSCALL_INTERRUPT_PRIORITY = 0x50` → TIM2 ISR 调 `vTaskNotifyGiveFromISR` 时断言失败 → 关中断死循环 → **IWDG 2.5s 复位循环**，看起来像"完全死机"
- **解决**：NVIC 分组改为 **4**（4 位全抢占），TIM2=5、USART1=6 直接映射为合法全优先级
- **预防**：分组 2 下抢占值必须 ≤3；分组 4 下直接用 0~15

### 7.2 IWDG 复位循环掩盖故障、干扰调试
- **现象**：程序卡死后每 2.5s 复位，调试器连不上 / 步进时报 "Cannot access target"
- **解决**：调试期临时注释 `IWDG_Init()`，定位后再恢复；ST-Link 用 SW/1MHz + Connect under Reset
- **预防**：先让故障"停住"再定位，不要带着看门狗调试

### 7.3 "程序卡在 GB16_NUM" 的误判
- **现象**：调试器停在字库计数函数三行"反复执行"
- **真相**：是**断点命中**（GB16_NUM 本来就要循环 ~210 次扫描字库），Memory 窗口验证 `hz_index` 有 NUL 终止符，函数正常返回
- **教训**：断点设在循环体内，每按一次 F5 都会再停一次，容易被误判为死循环；用"继续执行是否前进 + Watch `*PT`"判断真伪
- **加固**：给 GB16_NUM 加 2048 字节扫描上限，从结构上杜绝此类死循环

### 7.4 Keil armcc "missing closing quote"（UTF-8 BOM 丢失）
- **现象**：中文串 `"土壤湿度阈值"` 等报 `#8: missing closing quote`
- **原因**：**armcc 只有看到 UTF-8 BOM 才按 UTF-8 解析**；无 BOM 时按系统代码页（GBK）读取，UTF-8 中文字节被硬配对，把结尾的 `"` 也吞掉
- **解决**：源文件恢复 UTF-8 BOM（全工程统一"UTF-8 带 BOM"）
- **预防**：中文源码必须保留 BOM；用工具改写文件后检查 BOM

### 7.5 土壤传感器故障导致整屏错误页
- **现象**：土壤开路 → 进入 Error 态 → 错误页 "Retry..." 恰好画在温度行，误以为"温度也显示 Retry"
- **解决**：传感器类故障**不再进入 Error 态**（仅显示 `--`），主页面永远保留温度/湿度/光照；只有 WiFi/云/OLED 等系统错误才显示全屏错误页

### 7.6 风扇 1kHz PWM 高频啸叫
- **原因**：1kHz 恰在人耳敏感频段，电机发出可闻"吱吱"声
- **解决**：PWM 频率提到 **25kHz**（超出人耳上限）；剩余噪声为正常风噪/轴承声

### 7.7 串口共享变量未加 volatile / 缓冲无 NUL
- `reception`/`USART_RX_STA` 由 ISR 写、任务读，缺 `volatile` → 已补
- 接收缓冲写满回绕后可能无 NUL → `strstr` 越界 → ISR 每字节后补 NUL，写满整帧重收

### 7.8 AHT20 读取失败被静默吞掉
- `Soft_I2C_Read` 返回值未检查，I2C 失败时用陈旧数据计算温湿度 → 已检查返回值并清零缓冲

### 7.9 WiFi 初始化期间日志干扰 AT 时序
- Log 任务每 5s 打印可能插入 `+++` 退出透传/AT 命令序列 → 新增 `g_wifi_busy` 忙标志暂停日志

### 7.10 ESP8266 连不上 / 模块 LED 快闪
- 模块 AT 固件正常时 CWJAP 失败多为**供电不足**（发射电流 300mA+）或凭据/频段问题
- 排查：手机 2.4GHz 热点交叉验证；ESP8266 独立供电；`AT+CWJAP` 手动验证

### 7.11 软件 I2C 总线跨任务并发（本次审查发现）
- AHT20 采集（System 任务）与 24C02 阈值保存（Key 任务）共用 PB6/7 软件 I2C，无互斥 → 总线竞争可致数据损坏
- 已新增 `IIC_Mutex`，在 `Soft_I2C_Read/Write` 与 `AT24CXX_Read/WriteOneByte` 事务级加锁

---

## 8. 版本历史

| 版本 | 日期 | 主要变更 |
|---|---|---|
| 0.1 | — | 裸机重构版（协作调度 + 状态机） |
| 1.0 | 2026-08 | FreeRTOS V11.3.0 移植：四任务模型、状态机、云上报 |
| 1.1 | 2026-08-25 | 迭代修复：串口接收/GB16_NUM/OLED 检测/错误恢复节奏 |
| 1.2 | 2026-09 | 架构与可靠性：NVIC 分组4(修复configASSERT)、IWDG+故障捕获、volatile/NUL、AHT20 读检、WiFi 日志静默、单任务显示、24C02 阈值持久化、定点化、风扇 25kHz PWM 调速、土壤三态显示、I2C 总线互斥、凭据外置、工程卫生与 README 完善 |

---

## 9. 许可与声明

- 本工程为**学习交流用途**，未指定开源许可证；引用他人代码（正点原子 ALIENTEK、ST 标准库、FreeRTOS Kernel）版权归原作者所有
- WiFi 凭据与云 UID 存放于 `CONFIG/wifi_credentials.h`（已 gitignore），**请勿提交真实凭据**
- 巴法云为明文 TCP 透传，APP 指令协议无校验和，请勿用于生产环境敏感场景
