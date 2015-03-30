#include "SOFTUART.h"

#define	PCA_P12_P11_P10_P37	(0<<4)
#define	PCA_P34_P35_P36_P37	(1<<4)
#define	PCA_P24_P25_P26_P27	(2<<4)
#define	PCA_Mode_Capture	0
#define	PCA_Mode_SoftTimer	0x48
#define	PCA_Clock_1T		(4<<1)
#define	PCA_Clock_2T		(1<<1)
#define	PCA_Clock_4T		(5<<1)
#define	PCA_Clock_6T		(6<<1)
#define	PCA_Clock_8T		(7<<1)
#define	PCA_Clock_12T		(0<<1)
#define	PCA_Clock_ECI		(3<<1)
#define	PCA_Rise_Active		(1<<5)
#define	PCA_Fall_Active		(1<<4)
#define	PCA_PWM_8bit		(0<<6)
#define	PCA_PWM_7bit		(1<<6)
#define	PCA_PWM_6bit		(2<<6)

#define	UART3_BitTime	(MAIN_Fosc / UART3_Baudrate)

#define		ENABLE		1
#define		DISABLE		0

sbit P_RX3 = P3^5;		//定义模拟串口接收IO
sbit P_TX3 = P3^6;		//定义模拟串口发送IO

#if SOFTUART_TX == 1
static u16 CCAP1_tmp;
static u8	Tx3_head, Tx3_end;
static u8 idata	Tx3_BUF[TX3_BUF_Lenth];
static u8 Tx3_DAT;		// 发送移位变量, 用户不可见
static u8 Tx3_BitCnt;	// 发送数据的位计数器, 用户不可见
static bit Tx3_Stop;
#endif

#if SOFTUART_RX == 1
static u16 CCAP0_tmp;
static u8 Rx3_DAT;		// 接收移位变量, 用户不可见
static u8 Rx3_BitCnt;	// 接收数据的位计数器, 用户不可见
static bit Rx3_Ring;	// 正在接收标志, 低层程序使用, 用户程序不可见
u8	Rx3_number;
u8 	xdata	Rx3_BUF[RX3_BUF_Lenth];
#endif

#if SOFTUART_RX == 1
void RX3_Start(void)
{
	if (!(CCAPM0 & ENABLE))
	{
		Rx3_Ring  = 0;
		CCF0 = 0;			//清PCA模块0中断标志
		CCAPM0 = (PCA_Mode_Capture | PCA_Fall_Active | ENABLE);	//16位下降沿捕捉中断模式
		CR = 1;
	}
}

void RX3_Stop(void)
{
	CCAPM0 &= ~ENABLE;
	if (!(CCAPM1 & ENABLE)) CR = 0;
}

void RX3_Restart(void)
{
	Rx3_number = 0;
	RX3_Start();
}
#endif

#if SOFTUART_TX == 1
static void TX3_Start(void)
{
	Tx3_Stop = 0;
	if (!(CCAPM1 & ENABLE))
	{
		Tx3_BitCnt = 0;
		CCF1 = 0;			//清PCA模块1中断标志
		CCAP1_tmp = ((u16)CH << 8) + CL;	//读捕捉寄存器
		CCAP1_tmp += UART3_BitTime;				//+ 一个数据位
		CCAP1L    = (u8)CCAP1_tmp;				//将影射寄存器写入捕获寄存器，先写CCAP0L
		CCAP1H    = (u8)(CCAP1_tmp >> 8);	//后写CCAP0H
		CCAPM1 = PCA_Mode_SoftTimer | ENABLE;
		CR = 1;
	}
}

void TX3_Stopping(void)
{
	Tx3_Stop = 1;
}

void TX3_Flush(void)
{
	Tx3_end = Tx3_head = 0;
}
#endif

char Uart3_Putchar (char c)
{
#if SOFTUART_TX == 1
	while ((Tx3_end + 1) % TX3_BUF_Lenth == Tx3_head);
	Tx3_BUF[Tx3_end++] = c;
	if (Tx3_end == TX3_BUF_Lenth) Tx3_end = 0;
	TX3_Start();
#endif
	return c;
}

void Uart3_Init(void)
{
#if (SOFTUART_RX == 1) || (SOFTUART_TX == 1)
	CR = 0;
	AUXR1 = (AUXR1 & ~(3<<4)) | PCA_P34_P35_P36_P37;

	CH = 0;
	CL = 0;
	CMOD  = (CMOD  & ~(7<<1)) | PCA_Clock_1T;
	PPCA  = 1;	// 高优先级中断
#endif
}

char isUart3Running(void)
{
	return (CR == 1);
}

void	PCA_Handler (void) interrupt PCA_VECTOR
{

	if(CCF0)		//PCA模块0中断
	{
		CCF0 = 0;			//清PCA模块0中断标志
#if SOFTUART_RX == 1
		if(Rx3_Ring)		//已收到起始位
		{
			if (--Rx3_BitCnt == 0)		//接收完一帧数据
			{
				Rx3_Ring = 0;
				Rx3_BUF[Rx3_number++] = Rx3_DAT;		//存储数据到缓冲区
				if (Rx3_number == RX3_BUF_Lenth) Rx3_number = 0;
				CCAPM0 = (PCA_Mode_Capture | PCA_Fall_Active | ENABLE);	//16位下降沿捕捉中断模式
			}
			else
			{
				Rx3_DAT >>= 1;			  		//把接收的单b数据 暂存到 RxShiftReg(接收缓冲)
				if(P_RX3) Rx3_DAT |= 0x80;  	//shift RX data to RX buffer
				CCAP0_tmp += UART3_BitTime;		//数据位时间
				CCAP0L = (u8)CCAP0_tmp;			//将影射寄存器写入捕获寄存器，先写CCAP0L
				CCAP0H = (u8)(CCAP0_tmp >> 8);	//后写CCAP0H
			}
		}
		else
		{
			CCAP0_tmp = ((u16)CCAP0H << 8) + CCAP0L;	//读捕捉寄存器
			CCAP0_tmp += (UART3_BitTime / 2 + UART3_BitTime);	//起始位 + 半个数据位
			CCAP0L = (u8)CCAP0_tmp;			//将影射寄存器写入捕获寄存器，先写CCAP0L
			CCAP0H = (u8)(CCAP0_tmp >> 8);	//后写CCAP0H
			CCAPM0 = (PCA_Mode_SoftTimer | ENABLE);	//16位软件定时中断模式
			Rx3_Ring = 1;					//标志已收到起始位
			Rx3_BitCnt = 9;					//初始化接收的数据位数(8个数据位+1个停止位)
		}
#endif
	}

	if(CCF1)	//PCA模块1中断, 16位软件定时中断模式
	{
		CCF1 = 0;						//清PCA模块1中断标志
#if SOFTUART_TX == 1
		CCAP1_tmp += UART3_BitTime;
		CCAP1L = (u8)CCAP1_tmp;			//将影射寄存器写入捕获寄存器，先写CCAP0L
		CCAP1H = (u8)(CCAP1_tmp >> 8);	//后写CCAP0H

		if(Tx3_BitCnt == 0)			//发送计数器为0 表明单字节发送还没开始
		{
			if (Tx3_head == Tx3_end || Tx3_Stop)
			{
				CCAPM1 &= ~ENABLE;
				if (!(CCAPM0 & ENABLE)) CR = 0;
			}
			else
			{
				P_TX3 = 0;				//发送开始位
				Tx3_DAT = Tx3_BUF[Tx3_head++];		//把缓冲的数据放到发送的buff
				if (Tx3_head == TX3_BUF_Lenth) Tx3_head = 0;
				Tx3_BitCnt = 9;			//发送数据位数 (8数据位+1停止位)
			}
		}
		else						//发送计数器为非0 正在发送数据
		{
			if (--Tx3_BitCnt == 0)	//发送计数器减为0 表明单字节发送结束
			{
				P_TX3 = 1;			//送停止位数据
			}
			else
			{
				Tx3_DAT >>= 1;		//把最低位送到 CY(益处标志位)
				P_TX3 = CY;			//发送一个bit数据
			}
		}
#endif
	}
}

//void main(void)
//{
//	while (1)		//user's function
//	{
//		u8 i = 0;
//		//TX3_Flush();
//		printf("What's your name?\n");
//		RX3_Restart();
//		while(Rx3_number == 0) ;
//		do
//		{
//			if (i != Rx3_number)
//			{
//				if (Rx3_BUF[i] != '\r' && Rx3_BUF[i] != '\n') putchar(Rx3_BUF[i]);
//				i++;
//			}
//		}
//		while(Rx3_BUF[Rx3_number - 1] != '\n');
//		putchar('\n');
//		RX3_Stop();
//		if (Rx3_BUF[Rx3_number-2] == '\r') --Rx3_number;
//		Rx3_BUF[Rx3_number - 1] = 0;
//		printf("Hello, %s\n", Rx3_BUF);
//	}
//}

