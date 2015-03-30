#ifndef		__SOFTUART_H
#define		__SOFTUART_H

#include	"config.h"

#define	UART3_Baudrate	57600UL

void Uart3_Init(void);
char isUart3Running(void);
char Uart3_Putchar(char c);

#if SOFTUART_TX == 1
#define	TX3_BUF_Lenth		16

void TX3_Flush(void);
void TX3_Stopping(void);
#endif

#if SOFTUART_RX == 1
#define	RX3_BUF_Lenth		128

extern u8 Rx3_number;
extern u8 xdata Rx3_BUF[RX3_BUF_Lenth];

void RX3_Start(void);
void RX3_Stop(void);
void RX3_Restart(void);
#endif

#endif
