#include "oled_iic.h"
#include "delay.h"
#include "oledfont.h"
#include "string.h"

#if SYSTEM_SUPPORT_OS
#include "FreeRTOS.h"
#include "semphr.h"
extern SemaphoreHandle_t OLED_Mutex;   /* OLED互斥量(定义于App/app_task.c) */
#endif

unsigned int HZ=0;
//返回字库里汉字个数(UTF-8编码: 每个汉字对应一个首字节)
unsigned int GB16_NUM(void)
{
  unsigned int HZ_NUM = 0;
  const unsigned char *PT;
  PT = hz_index;
  while(*PT != '\0')
  {
  	 if((*PT & 0xC0) == 0xC0)   /* UTF-8 2/3字节编码的首字节 */
  	 {
  	 	HZ_NUM++;
  	 }
  	 PT++;
  }

  return HZ_NUM;
} 

void IIC_GPIO_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(OLED_SCL_RCC|OLED_SDA_RCC, ENABLE);	/* 打开GPIO时钟 */

	GPIO_InitStructure.GPIO_Pin = OLED_SCL_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;  	/* 开漏输出 */
	GPIO_Init(OLED_SCL_GPIO_PORT, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin = OLED_SDA_PIN;
	GPIO_Init(OLED_SDA_GPIO_PORT, &GPIO_InitStructure);

	/* 给一个停止信号, 复位IIC总线上的所有设备到待机模式 */
	IIC_Stop();
}

static void IIC_Delay(void)
{
	uint8_t i;
	for (i = 0; i < 10; i++);
}

void IIC_Start(void)
{
	OLED_SDA=1;
	OLED_SCL=1;
	IIC_Delay();
	OLED_SDA=0;
	IIC_Delay();
	OLED_SCL=0;
	IIC_Delay();
}

void IIC_Stop(void)
{
	OLED_SDA=0;
	OLED_SCL=1;
	IIC_Delay();
	OLED_SDA=1;
}

uint8_t IIC_WaitAck(void)
{
	uint8_t re;

	OLED_SDA=1;
	IIC_Delay();
	OLED_SCL=1;
	IIC_Delay();
	if (OLED_IIC_SDA_READ())
	{
		re = 1;
	}
	else
	{
		re = 0;
	}
	OLED_SCL=0;
	IIC_Delay();
	return re;
}

u8 Write_IIC_Byte(uint8_t _ucByte)
{
  uint8_t i;
  u8 ack;
	for (i = 0; i < 8; i++)
	{		
		if (_ucByte & 0x80)
		{
			OLED_SDA=1;
		}
		else
		{
			OLED_SDA=0;
		}
		IIC_Delay();
		OLED_SCL=1;
		IIC_Delay();	
		OLED_SCL=0;
		if (i == 7)
		{
			 OLED_SDA=1; // 释放总线
		}
		_ucByte <<= 1;
		IIC_Delay();
	}
	ack = IIC_WaitAck();
	return ack;   /* 0=有应答, 1=无应答 */
}

u8 Write_IIC_Command(u8 IIC_Command)
{
	u8 ack = 0;
	IIC_Start();
	ack |= Write_IIC_Byte(OLED_ADDRESS);
	ack |= Write_IIC_Byte(0x00);
	ack |= Write_IIC_Byte(IIC_Command);
	IIC_Stop();
	return ack;
}

u8 Write_IIC_Data(u8 IIC_Data)
{
	u8 ack = 0;
	IIC_Start();
	ack |= Write_IIC_Byte(OLED_ADDRESS);
	ack |= Write_IIC_Byte(0x40);
	ack |= Write_IIC_Byte(IIC_Data);
	IIC_Stop();
	return ack;
}

//OLED全屏填充(128列)
void OLED_Fill(u8 fill_Data)
{
	u8 m,n;
	for(m=0;m<8;m++)
	{
		Write_IIC_Command(0xb0+m);
		Write_IIC_Command(0x00);
		Write_IIC_Command(0x10);
		for(n=0;n<128;n++)
		{
			Write_IIC_Data(fill_Data);
		}
	}
}

void OLED_Set_Pos(u8 x, u8 y) 
{ 
	Write_IIC_Command(0xb0+y);
	Write_IIC_Command((((x)&0xf0)>>4)|0x10);
	Write_IIC_Command(((x)&0x0f)|0x01);
}

//清屏
void OLED_Clear(void)
{
#if SYSTEM_SUPPORT_OS
	if(OLED_Mutex != NULL) xSemaphoreTake(OLED_Mutex, portMAX_DELAY);
#endif
	OLED_Fill(0x00);
#if SYSTEM_SUPPORT_OS
	if(OLED_Mutex != NULL) xSemaphoreGive(OLED_Mutex);
#endif
}

//在指定位置显示一个字符(内部函数, 不加锁)
void OLED_ShowChar(u8 x, u8 y, u8 chr)
{
  u8 c = 0, i = 0;
  c = chr - ' ';
  if(x > 130 - 1)
  {
    x = 0;
    y = y + 2;
  }
	OLED_Set_Pos(x, y);
	for(i = 0; i < 8; i++)
		Write_IIC_Data(zf[c * 16 + i]);
	OLED_Set_Pos(x, y + 1);
	for(i = 0; i < 8; i++)
		Write_IIC_Data(zf[c * 16 + i + 8]);
}

u32 oled_pow(u8 m, u8 n)
{
  u32 result = 1;
  while(n--)result *= m;
  return result;
}

//显示数字
void OLED_ShowNum(u8 x, u8 y, u32 num, u8 len,u8 mode)
{
  u8 t, temp;
#if SYSTEM_SUPPORT_OS
	if(OLED_Mutex != NULL) xSemaphoreTake(OLED_Mutex, portMAX_DELAY);
#endif
	if(num<10)
		len=1;
	else if(num>=10&&num<100)
		len=2;
	else if(num>=100&&num<1000)
		len=3;
	else if(num>=1000&&num<10000)
		len=4;
	else if(num>=10000&&num<100000)
		len=5;
  for(t = 0; t < len; t++)
  {
    temp = (num / oled_pow(10, len - t - 1)) % 10;
		if(temp == 0)
		{
			if(mode)
				OLED_ShowChar(x + 8*t, y, '0');
			else
				OLED_ShowChar(x + 8*t, y, ' ');
			continue;
		}
		else
			OLED_ShowChar(x + 8*t, y, temp + '0');
  }
#if SYSTEM_SUPPORT_OS
	if(OLED_Mutex != NULL) xSemaphoreGive(OLED_Mutex);
#endif
}

//显示中英文字符
void OLED_ShowCH(u8 x, u8 y,u8 *chs)
{
  u32 i=0;
	u32 j;
	char* m;
#if SYSTEM_SUPPORT_OS
	if(OLED_Mutex != NULL) xSemaphoreTake(OLED_Mutex, portMAX_DELAY);
#endif
	while (*chs != '\0')
	{
		if (*chs & 0x80)   /* 多字节(UTF-8中文) */
		{
			u8 step;
			if ((*chs & 0xE0) == 0xC0)
				step = 2;
			else if ((*chs & 0xF0) == 0xE0)
				step = 3;
			else
			{
				chs++;   /* 其他非法/4字节字符: 跳过该字节 */
				continue;
			}
			for (i=0 ;i < HZ;i++)
			{	
				if(x>112)
				{
					x=0;
					y=y+2;
				}
				if (step == 3 &&
				    (*chs == hz_index[i*3]) &&
				    (*(chs+1) == hz_index[i*3+1]) &&
				    (*(chs+2) == hz_index[i*3+2]))
				{
					OLED_Set_Pos(x, y);
					for(j=0;j<16;j++)
						Write_IIC_Data(hz[i*32+j]);
					OLED_Set_Pos(x,y+1);
					for(j=0;j<16;j++)
						Write_IIC_Data(hz[i*32+j+16]);
					x +=16;
					break;
				}
			}
			chs += step;
		}
		else
		{
			if(x>122)
			{
				x=0;
				y=y+2;
			}
			m=strchr(zf_index,*chs);
			if (m!=NULL)
			{
				OLED_Set_Pos(x, y);
				for(j = 0; j < 8; j++)
					Write_IIC_Data(zf[((u8)*m-' ') * 16 + j]);
				OLED_Set_Pos(x, y + 1);
				for(j = 0; j < 8; j++)
					Write_IIC_Data(zf[((u8)*m-' ') * 16 + j + 8]);
				x += 8;
			}
			chs++;
		}
	}
#if SYSTEM_SUPPORT_OS
	if(OLED_Mutex != NULL) xSemaphoreGive(OLED_Mutex);
#endif
}

/* 说明: 本字库仅收录3字节UTF-8汉字, 2字节UTF-8字符(如°é)不在字库中,
 * 显示时会被跳过(见上方step==2分支), 如需显示请扩充oledfont.h */

//初始化SSD1306, 返回0表示成功
u8 OLED_Init(void)
{
	u8 fail = 0;
#if SYSTEM_SUPPORT_OS
	if(OLED_Mutex != NULL) xSemaphoreTake(OLED_Mutex, portMAX_DELAY);
#endif
	IIC_GPIO_Config();
  fail |= Write_IIC_Command(0xAE); //--display off
  fail |= Write_IIC_Command(0x00); //---set low column address
  fail |= Write_IIC_Command(0x10); //---set high column address
  fail |= Write_IIC_Command(0x40); //--set start line address
  fail |= Write_IIC_Command(0xB0); //--set page address
  fail |= Write_IIC_Command(0x81); // contract control
  fail |= Write_IIC_Command(0xFF); //--128
  fail |= Write_IIC_Command(0xA1); //set segment remap
  fail |= Write_IIC_Command(0xA6); //--normal / reverse
  fail |= Write_IIC_Command(0xA8); //--set multiplex ratio(1 to 64)
  fail |= Write_IIC_Command(0x3F); //--1/32 duty
  fail |= Write_IIC_Command(0xC8); //Com scan direction
  fail |= Write_IIC_Command(0xD3); //-set display offset
  fail |= Write_IIC_Command(0x00); //
  fail |= Write_IIC_Command(0xD5); //set osc division
  fail |= Write_IIC_Command(0x80); //
  fail |= Write_IIC_Command(0xD8); //set area color mode off
  fail |= Write_IIC_Command(0x05); //
  fail |= Write_IIC_Command(0xD9); //Set Pre-Charge Period
  fail |= Write_IIC_Command(0xF1); //
  fail |= Write_IIC_Command(0xDA); //set com pin configuartion
  fail |= Write_IIC_Command(0x12); //
  fail |= Write_IIC_Command(0xDB); //set Vcomh
  fail |= Write_IIC_Command(0x30); //
  fail |= Write_IIC_Command(0x8D); //set charge pump enable
  fail |= Write_IIC_Command(0x14); //
  fail |= Write_IIC_Command(0xAF); //--turn on oled panel
#if SYSTEM_SUPPORT_OS
	if(OLED_Mutex != NULL) xSemaphoreGive(OLED_Mutex);
#endif
	return fail ? 1 : 0;
}
