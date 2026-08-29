#ifndef __AHT20_H
#define __AHT20_H
#include "sys.h"
#include "myiic.h"
#include "delay.h"
 
#define ATH20_SLAVE_ADDRESS		0x38		/* I2C从机地址 */
 
//****************************************
// 定义 AHT20 内部地址
//****************************************
#define	AHT20_STATUS_REG		0x00	//状态字 寄存器地址
#define	AHT20_INIT_REG			0xBE	//初始化 寄存器地址
#define	AHT20_SoftReset			0xBA	//软复位 单指令
#define	AHT20_TrigMeasure_REG	0xAC	//触发测量 寄存器地址
 
// 存储AHT20传感器信息的结构体定义位于 Data/data.h(数据层, 避免硬件依赖业务类型)
struct m_AHT20;
 
 
uint8_t AHT20_Init(void);
uint8_t AHT20_ReadHT(uint32_t *HT);
uint8_t StandardUnitCon(struct m_AHT20* aht);
 
#endif




