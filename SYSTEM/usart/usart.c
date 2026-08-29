#include "sys.h"
#include "usart.h"
#include "led.h"

//FreeRTOS:串口发送互斥量,防止多个任务同时printf导致乱码
#if SYSTEM_SUPPORT_OS
#include "FreeRTOS.h"
#include "semphr.h"
extern SemaphoreHandle_t USART1_TxMutex;   //定义于App/app_task.c
#endif

////////////////////////////////////////////////////////////////////////////////// 	 
#if 1
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
	int handle; 

}; 

FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
void _sys_exit(int x) 
{ 
	x = x; 
} 
//重定义fputc函数 
int fputc(int ch, FILE *f)
{      
#if SYSTEM_SUPPORT_OS
	if(USART1_TxMutex != NULL)
	{
		xSemaphoreTake(USART1_TxMutex, portMAX_DELAY);   //互斥量保护,保证多任务printf不交错
	}
#endif
	while((USART1->SR&0X40)==0);//循环发送,直到发送完毕   
	USART1->DR = (u8) ch;      
#if SYSTEM_SUPPORT_OS
	if(USART1_TxMutex != NULL)
	{
		xSemaphoreGive(USART1_TxMutex);
	}
#endif
	return ch;
}
#endif 

/* 整串发送: 一次持锁发完整条字符串, 避免日志任务插入AT命令/上报数据中间 */
void USART1_SendString(const char *str)
{
#if SYSTEM_SUPPORT_OS
	if(USART1_TxMutex != NULL)
	{
		xSemaphoreTake(USART1_TxMutex, portMAX_DELAY);
	}
#endif
	if(str != NULL)
	{
		while(*str != '\0')
		{
			while((USART1->SR & 0x40) == 0);   /* 等待TXE */
			USART1->DR = (u8)*str++;
		}
	}
#if SYSTEM_SUPPORT_OS
	if(USART1_TxMutex != NULL)
	{
		xSemaphoreGive(USART1_TxMutex);
	}
#endif
}

#if EN_USART1_RX   //如果使能了接收
//串口1中断服务程序
//注意,读取USARTx->SR能避免莫名其妙的错误   	
volatile u8 reception;                  /* ISR/任务共享, 必须volatile */
char USART_RX_BUF[USART_REC_LEN];     //接收缓冲,最大USART_REC_LEN个字节.
//接收状态
//bit15，	接收完成标志
//bit14，	接收到0x40('@')
//bit13~0，	接收到的有效字节数目
volatile u16 USART_RX_STA=0;           /* ISR/任务共享, 必须volatile */
volatile u8 usart_link_lost = 0;      /* ESP8266透明模式下断线提示(CLOSED/WIFI DISCONNECT) */

/* 关键字匹配状态机(逐字节, 开销小, 适合在ISR中运行) */
static const char s_closed_pat[]    = "CLOSED";
static const char s_wifi_disc_pat[] = "WIFI DISCONNECT";
static u8 s_closed_idx = 0;
static u8 s_wifi_disc_idx = 0;
  
void uart1_init(u32 bound){
  //GPIO端口设置
  GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1|RCC_APB2Periph_GPIOA, ENABLE);	//使能USART1，GPIOA时钟
  
	//USART1_TX   GPIOA.9
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //PA.9
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.9
   
  //USART1_RX	  GPIOA.10初始化
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;//PA10
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.10  

  //Usart1 NVIC 配置
  NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	//FreeRTOS要求调用API的中断优先级数值 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=6 ;//抢占优先级6
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		//子优先级0
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
  
   //USART 初始化设置

	USART_InitStructure.USART_BaudRate = bound;//串口波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

  USART_Init(USART1, &USART_InitStructure); //初始化串口1
  USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启串口接收中断
  USART_Cmd(USART1, ENABLE);                    //使能串口1 

}

void USART1_IRQHandler(void)                	//串口1中断服务程序
{
	u8 Res;
	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)  //接收中断(接收到的数据必须以@*结尾)
	{
		Res = USART_ReceiveData(USART1);	//读取接收到的数据

		/* 断线关键字检测: ESP8266透明模式下TCP断开会输出 CLOSED / WIFI DISCONNECT */
		if(Res == s_closed_pat[s_closed_idx])
		{
			s_closed_idx++;
			if(s_closed_idx >= (sizeof(s_closed_pat) - 1))
			{
				usart_link_lost = 1;
				s_closed_idx = 0;
			}
		}
		else
		{
			s_closed_idx = (Res == s_closed_pat[0]) ? 1 : 0;
		}
		if(Res == s_wifi_disc_pat[s_wifi_disc_idx])
		{
			s_wifi_disc_idx++;
			if(s_wifi_disc_idx >= (sizeof(s_wifi_disc_pat) - 1))
			{
				usart_link_lost = 1;
				s_wifi_disc_idx = 0;
			}
		}
		else
		{
			s_wifi_disc_idx = (Res == s_wifi_disc_pat[0]) ? 1 : 0;
		}
		
		if((USART_RX_STA&0x8000)==0)		//接收未完成
		{
			if(USART_RX_STA&0x4000) 		//接收到了0x40('@')
			{
				if(Res!=0x2A)  				//如果下一位不是0x2A('*'),接收错误,重新开始
					USART_RX_STA=0;
				else 
				{
					USART_RX_STA|=0x8000;	//接收完成了
					reception=1;
					//关闭RXNE中断,防止APP任务处理接收缓冲期间被新数据覆盖
					USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
				}
			}
			else 							//还没收到0x40('@')
			{	
				if(Res==0x40)   			//接收到了0x40('@')
					USART_RX_STA|=0x4000;
				else
				{
					/* 缓冲写满(无空闲位置写NUL): 丢弃整帧重新开始,
					 * 保证缓冲内始终存在NUL终止符, 避免strstr越界读 */
					if(USART_RX_STA >= (USART_REC_LEN - 1))
					{
						USART_RX_STA = 0;
						USART_RX_BUF[0] = Res;
						USART_RX_BUF[1] = '\0';
						USART_RX_STA = 1;
					}
					else
					{
						USART_RX_BUF[USART_RX_STA & 0X3FFF] = Res;
						USART_RX_BUF[(USART_RX_STA & 0X3FFF) + 1] = '\0';	/* 每字节后补NUL */
						USART_RX_STA++;
					}
				}		 
			}
		}   		 
	} 
}
#endif	
