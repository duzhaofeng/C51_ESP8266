#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ESP8266.h"

#define	TIMER0_COUNT	(65536 - (MAIN_Fosc / TIMER0_HZ / 12))
#define	UART0_COUNT		(65536 - (MAIN_Fosc / 4 / UART0_Baudrate))
#define	TX_BUF_Lenth	(16)

static uint8_t	Tx_head, Tx_end;
static volatile uint16_t timer_tick;
static uint8_t idata Tx_BUF[TX_BUF_Lenth];
static bit Tx_Running;
static bit Rx_Nopadding;

uint8_t	Rx_number;
uint8_t xdata	Rx_BUF[RX_BUF_Lenth];

/*Timer0 Function*/
void timer0_int (void) interrupt TIMER0_VECTOR
{
	timer_tick++;
}

static void initTimer(void)
{
	PT0 = 1;
	TMOD &= ~0x03;			//工作模式: 16位自动重装
	AUXR &= ~0x80;			//12T
	TMOD &= ~0x04;			//定时
	INT_CLKO &= ~0x01;	//不输出时钟

	TH0 = (uint8_t)(TIMER0_COUNT >> 8);
	TL0 = (uint8_t)TIMER0_COUNT;
}

void startTimer(void)
{
	if (!TR0)
	{
		timer_tick = 0;
		ET0 = 1;					//允许中断
		TR0 = 1;					//开始运行
	}
}

void stopTimer(void)
{
	TR0 = 0;
	ET0 = 0;
}

uint16_t millis(void)
{
	return timer_tick * (1000 / TIMER0_HZ);
}

void idle(void)
{
	PCON |= 0x01;
	_nop_();
	_nop_();
	_nop_();
	_nop_();
}

void delay(uint16_t ms)
{
	uint16_t start;
	uint8_t timerunning = TR0;
	ms /= (1000 / TIMER0_HZ);
	if (!timerunning)
	{
		startTimer();
	}
	start = timer_tick;
	while (timer_tick - start < ms)
	{
		idle();
	}
	if (!timerunning)
	{
		stopTimer();
	}
}

/*Uart1 Function*/
void UART_int (void) interrupt UART1_VECTOR
{
	if(RI)
	{
		RI = 0;
		Rx_BUF[Rx_number] = SBUF;
		if (!Rx_Nopadding || Rx_BUF[Rx_number] != '\0')
		{
			if (++Rx_number == RX_BUF_Lenth) Rx_number = 0;
		}
	}

	if(TI)
	{
		TI = 0;
		if (Tx_head == Tx_end)
		{
			Tx_Running = 0;
			if (!REN) ES = 0;
		}
		else
		{
			Tx_Running = 1;
			SBUF = Tx_BUF[Tx_head++];
			if (Tx_head == TX_BUF_Lenth) Tx_head = 0;	
		}
	}
}

static void initUart(void)
{
	PS = 0;													//低优先级中断
	SCON = (SCON & 0x3f) | (1<<6);	//8位数据,可变波特率
	AUXR &= ~(1<<4);								//Timer stop
	AUXR |= 0x01;										//S1 BRT Use Timer2;
	AUXR &= ~(1<<3);								//Timer2 set As Timer
	AUXR |=  (1<<2);								//Timer2 set as 1T mode
	TH2 = (uint8_t)(UART0_COUNT>>8);
	TL2 = (uint8_t)UART0_COUNT;
	IE2  &= ~(1<<2);								//禁止中断
	AUXR &= ~(1<<3);								//定时
	AUXR |=  (1<<4);								//Timer run enable
	P_SW1 = (P_SW1 & 0x3f) | (0 & 0xc0);//切换IO
	PCON2 &= ~(1<<4);
	P3M1 = 0x00;
	P3M0 = 0x03;

	REN = 0;
	ES = 0;
	Tx_Running = 0;
	TX_Flush();
}

static void RX_Start(void)
{
	ES = 1;
	REN = 1;
}

static void RX_Stop(void)
{
	REN = 0;
	if (!Tx_Running) ES = 0;
}

static void RX_Restart(uint8_t padding)
{
	if (padding) Rx_Nopadding = 0;
	else Rx_Nopadding = 1;
	Rx_number = 0;
	RX_Start();
}

uint8_t RX_IndexOf(uint8_t* target, uint8_t rx_offset)
{
	uint8_t i;
	uint8_t len, tlen;
	tlen = strlen(target);
	if (Rx_number <= rx_offset) return 0xFF;
	for (len = Rx_number - rx_offset; len >= tlen; rx_offset++, len--)
	{
		for (i = 0; i < tlen && Rx_BUF[rx_offset + i] == target[i]; i++);
		if (i == tlen) return rx_offset;
	}
	return 0xFF;
}

static void TX_Start(void)
{
	if (!Tx_Running)
	{
		ES = 1;
		TI = 1;					//触发发送中断
		Tx_Running = 1;
	}
}

bool isUartRunning(void)
{
	return (ES == 1);
}

void TX_Flush(void)
{
	Tx_end = Tx_head = 0;
}

char Uart_Putchar (char c)
{
	while ((Tx_end + 1) % TX_BUF_Lenth == Tx_head);
	Tx_BUF[Tx_end++] = c;
	if (Tx_end == TX_BUF_Lenth) Tx_end = 0;
	TX_Start();
	return c;
}

/*ESP8266 Function*/
/*----------------------------------------------------------------------------*/
/* +IPD,<id>,<len>:<data> */
/* +IPD,<len>:<data> */
static uint16_t recvPkg(uint8_t *mux_id, uint16_t timeout)
{
	uint8_t index_PIPDcomma;
	uint8_t index_colon;
	uint8_t index_comma;
	uint16_t len = 0;
	uint8_t id = -1;
	uint16_t start;
	uint8_t rxlen;
	RX_Restart(1);
	startTimer();
	start = millis();
	while (millis() - start < timeout) {
		index_PIPDcomma = RX_IndexOf("+IPD,", 0);
		if (index_PIPDcomma != 0xFF) {
			index_colon = RX_IndexOf(":", index_PIPDcomma + 5);
			if (index_colon != 0xFF) {
				index_comma = RX_IndexOf(",", index_PIPDcomma + 5);
				/* +IPD,id,len:data */
				if (index_comma != 0xFF && index_comma < index_colon) {
					Rx_BUF[index_comma] = '\0';
					Rx_BUF[index_colon] = '\0';
					id = atoi(Rx_BUF + index_PIPDcomma + 5);
					if (id < 0 || id > 4) {
						len = 0;
						break;
					}
					len = atoi(Rx_BUF + index_comma + 1);
					if (len <= 0) {
						len = 0;
					}
				} else { /* +IPD,len:data */
					Rx_BUF[index_colon] = '\0';
					len = atoi(Rx_BUF + index_PIPDcomma + 5);
					if (len <= 0) {
						len = 0;
					}
				}
				break;
			}
		}
		idle();
	}
	if (len) {
		RX_Restart(1);
		rxlen = len > RX_BUF_Lenth ? RX_BUF_Lenth : len;
		start = millis();
		while (millis() - start < 3000) {
			if (Rx_number == rxlen) {
				break;
			}
			idle();
		}
	}
	stopTimer();
	RX_Stop();
	if (mux_id) {
		*mux_id = id;
	}
	return len;
}
static uint8_t recvString(uint8_t** ptarget, uint8_t target_num, uint16_t timeout)
{
	uint16_t start;
	RX_Restart(0);
	startTimer();
	start = millis();
	while (millis() - start < timeout) {
		uint8_t i;
		for (i = 0; i < target_num; i++)
		{
			if (RX_IndexOf(ptarget[i], 0) != 0xFF)
			{
				goto rtn;
			}
		}
		idle();
	}
rtn:
	stopTimer();
	RX_Stop();
	Rx_BUF[Rx_number] = '\0';
	return Rx_number;
}

static void recvLineAndProcess(uint8_t* linebreak, line_process func, uint16_t timeout)
{
	uint8_t i, j, tlen;
	uint16_t start;
	tlen = strlen(linebreak);
	RX_Restart(0);
	startTimer();
	start = millis();
	i = j = 0;
	while (millis() - start < timeout) {
		if (i != Rx_number - j)
		{
			ES = 0; 						//关闭串口中断
			i = Rx_number - j;
			if (i >= tlen && !strncmp(linebreak, Rx_BUF + Rx_number - tlen, tlen))
			{
				Rx_BUF[Rx_number - tlen] = '\0';
				if (j)
				{
					Rx_number = 0;
				}
				i = j;
				j = Rx_number;
				ES = 1;						//打开串口中断

				if (!(*func)(Rx_BUF + i))
				{
					break;
				}
				i = 0;
			}
			ES = 1; 						//打开串口中断
		}
		idle();
	}
	stopTimer();
	RX_Stop();
}
static bool recvFind(uint8_t* target, uint16_t timeout)
{
	recvString(&target, 1, timeout);
	if (RX_IndexOf(target, 0) != 0xFF) {
		return true;
	}
	return false;
}
static uint8_t* recvFindAndFilter(uint8_t** ptarget, uint16_t timeout)
{
	recvString(ptarget, 1, timeout);
	if (RX_IndexOf(*ptarget, 0) != 0xFF) {
		uint8_t index1 = RX_IndexOf(ptarget[1], 0);
		uint8_t index2 = RX_IndexOf(ptarget[2], index1);
		if (index1 != 0xFF && index2 != 0xFF) {
			index1 += strlen(ptarget[1]);
			Rx_BUF[index2] = '\0';
			return Rx_BUF + index1;
		}
	}
	return NULL;
}
static bool eAT(void)
{
	printf("AT\n");
	return recvFind("OK", 1000);
}
static bool eATRST(void)
{
	printf("AT+RST\n");
	return recvFind("OK", 1000);
}
static uint8_t* eATGMR(void)
{
	uint8_t* code ptarget[] = {"OK", "\r\r\n", "\r\n\r\nOK"};
	printf("AT+GMR\n");
	return recvFindAndFilter(ptarget, 1000);
}
static sint8_t qATCWMODE(void)
{
	uint8_t* str_mode;
	uint8_t* code ptarget[] = {"OK", "+CWMODE:", "\r\n\r\nOK"};
	printf("AT+CWMODE?\n");
	str_mode = recvFindAndFilter(ptarget, 1000);
	if (str_mode) {
		return (sint8_t)atoi(str_mode);
	} else {
		return -1;
	}
}
static bool sATCWMODE(uint8_t mode)
{
	uint8_t* code ptarget[] = {"OK", "no change"};
	printf("AT+CWMODE=%d\n", mode);
	recvString(ptarget, 2, 1000);
	if (RX_IndexOf("OK", 0) != 0xFF || RX_IndexOf("no change", 0) != 0xFF) {
		return true;
	}
	return false;
}
static bool sATCWJAP(uint8_t* ssid, uint8_t* pwd)
{
	uint8_t* code ptarget[] = {"OK", "FAIL"};
	printf("AT+CWJAP=\"%s\",\"%s\"\n", ssid, pwd);
	recvString(ptarget, 2, 10000);
	if (RX_IndexOf("OK", 0) != 0xFF) {
		return true;
	}
	return false;
}
static void eATCWLAP(line_process func)
{
	printf("AT+CWLAP\n");
	recvLineAndProcess("\r\n", func, 10000);
}
static bool eATCWQAP(void)
{
	printf("AT+CWQAP\n");
	return recvFind("OK", 1000);
}
static bool sATCWSAP(uint8_t* ssid, uint8_t* pwd, uint8_t chl, uint8_t ecn)
{
	uint8_t* code ptarget[] = {"OK", "FAIL"};
	printf("AT+CWSAP=\"%s\",\"%s\",%d,%d\n", ssid, pwd, chl, ecn);
	recvString(ptarget, 2, 10000);
	if (RX_IndexOf("OK", 0) != 0xFF) {
		return true;
	}
	return false;
}
static uint8_t* eATCWLIF(void)
{
	uint8_t* code ptarget[] = {"OK", "\r\r\n", "\r\n\r\nOK"};
	printf("AT+CWLIF\n");
	return recvFindAndFilter(ptarget, 1000);
}
static uint8_t* eATCIPSTATUS(void)
{
	uint8_t* code ptarget[] = {"OK", "\r\r\n", "\r\n\r\nOK"};
	delay(100);
	printf("AT+CIPSTATUS\n");
	return recvFindAndFilter(ptarget, 1000);
}
static bool sATCIPSTARTSingle(uint8_t* type, uint8_t* addr, uint8_t* port)
{
	uint8_t* code ptarget[] = {"OK", "ERROR", "ALREADY CONNECT"};
	printf("AT+CIPSTART=\"%s\",\"%s\",%s\n", type, addr, port);
	recvString(ptarget, 3, 10000);
	if (RX_IndexOf("OK", 0) != 0xFF || RX_IndexOf("ALREADY CONNECT", 0) != 0xFF) {
		return true;
	}
	return false;
}
static bool sATCIPSTARTMultiple(uint8_t mux_id, uint8_t* type, uint8_t* addr, uint8_t* port)
{
	uint8_t* code ptarget[] = {"OK", "ERROR", "ALREADY CONNECT"};
	printf("AT+CIPSTART=%d,\"%s\",\"%s\",%s\n", mux_id, type, addr, port);
	recvString(ptarget, 3, 10000);
	if (RX_IndexOf("OK", 0) != 0xFF || RX_IndexOf("ALREADY CONNECT", 0) != 0xFF) {
		return true;
	}
	return false;
}
static bool sATCIPSENDSingle(const uint8_t *buffer, uint16_t len)
{
	printf("AT+CIPSEND=%d\n", len);
	if (recvFind(">", 5000)) {
		uint16_t i;
		for (i = 0; i < len; i++) {
			Uart_Putchar(buffer[i]);
		}
		return recvFind("SEND OK", 10000);
	}
	return false;
}
static bool sATCIPSENDMultiple(uint8_t mux_id, const uint8_t *buffer, uint16_t len)
{
	printf("AT+CIPSEND=%d,%d\n", mux_id, len);
	if (recvFind(">", 5000)) {
		uint16_t i;
		for (i = 0; i < len; i++) {
			Uart_Putchar(buffer[i]);
		}
		return recvFind("SEND OK", 10000);
	}
	return false;
}
static bool sATCIPCLOSEMulitple(uint8_t mux_id)
{
	uint8_t* code ptarget[] = {"OK", "link is not"};
	printf("AT+CIPCLOSE=%d", mux_id);
	recvString(ptarget, 2, 5000);
	if (RX_IndexOf("OK", 0) != 0xFF || RX_IndexOf("link is not", 0) != 0xFF) {
		return true;
	}
	return false;
}
static bool eATCIPCLOSESingle(void)
{
	printf("AT+CIPCLOSE\n");
	return recvFind("OK", 5000);
}
static uint8_t* eATCIFSR(void)
{
	uint8_t* code ptarget[] = {"OK", "\r\r\n", "\r\n\r\nOK"};
	printf("AT+CIFSR\n");
	return recvFindAndFilter(ptarget, 1000);
}
static bool sATCIPMUX(uint8_t mode)
{
	uint8_t* code ptarget[] = {"OK", "Link is builded"};
	printf("AT+CIPMUX=%d\n", mode);
	recvString(ptarget, 2, 1000);
	if (RX_IndexOf("OK", 0) != 0xFF) {
		return true;
	}
	return false;
}
static bool sATCIPSERVER(uint8_t mode, uint8_t* port)
{
	if (mode) {
		uint8_t* code ptarget[] = {"OK", "no change"};
		printf("AT+CIPSERVER=1,%s\n", port);
		recvString(ptarget, 2, 1000);
		if (RX_IndexOf("OK", 0) != 0xFF || RX_IndexOf("no change", 0) != 0xFF) {
			return true;
		}
		return false;
	} else {
		printf("AT+CIPSERVER=0\n");
		return recvFind("\r\r\n", 1000);
	}
}
static bool sATCIPSTO(uint16_t timeout)
{
	printf("AT+CIPSTO=%d\n", timeout);
	return recvFind("OK", 1000);
}
void ESP8266_init(void)
{
	initTimer();
	stopTimer();
	initUart();
}
bool ESP8266_kick(void)
{
	return eAT();
}
bool ESP8266_restart(void)
{
	uint16_t start;
	if (eATRST()) {
		startTimer();
		delay(2000);
		start = millis();
		while (millis() - start < 3000) {
			if (eAT()) {
				delay(1500); /* Waiting for stable */
				stopTimer();
				return true;
			}
			delay(100);
		}
	}
	stopTimer();
	return false;
}
uint8_t* ESP8266_getVersion(void)
{
	return eATGMR();
}
bool ESP8266_setOprToStation(void)
{
	uint8_t mode = qATCWMODE();
	if (mode == -1) {
		return false;
	}
	if (mode == 1) {
		return true;
	} else {
		if (sATCWMODE(1) && ESP8266_restart()) {
			return true;
		} else {
			return false;
		}
	}
}
bool ESP8266_setOprToSoftAP(void)
{
	uint8_t mode = qATCWMODE();
	if (mode == -1) {
		return false;
	}
	if (mode == 2) {
		return true;
	} else {
		if (sATCWMODE(2) && ESP8266_restart()) {
			return true;
		} else {
			return false;
		}
	}
}
bool ESP8266_setOprToStationSoftAP(void)
{
	uint8_t mode = qATCWMODE();
	if (mode == -1) {
		return false;
	}
	if (mode == 3) {
		return true;
	} else {
		if (sATCWMODE(3) && ESP8266_restart()) {
			return true;
		} else {
			return false;
		}
	}
}
void ESP8266_getAPList(line_process func)
{
	eATCWLAP(func);
}
bool ESP8266_joinAP(uint8_t* ssid, uint8_t* pwd)
{
	return sATCWJAP(ssid, pwd);
}
bool ESP8266_leaveAP(void)
{
	return eATCWQAP();
}
bool ESP8266_setSoftAPParam(uint8_t* ssid, uint8_t* pwd, uint8_t chl, uint8_t ecn)
{
	return sATCWSAP(ssid, pwd, chl, ecn);
}
uint8_t* ESP8266_getJoinedDeviceIP(void)
{
	return eATCWLIF();
}
uint8_t* ESP8266_getIPStatus(void)
{
	return eATCIPSTATUS();
}
uint8_t* ESP8266_getLocalIP(void)
{
	return eATCIFSR();
}
bool ESP8266_enableMUX(void)
{
	return sATCIPMUX(1);
}
bool ESP8266_disableMUX(void)
{
	return sATCIPMUX(0);
}
bool ESP8266_createSingleTCP(uint8_t* addr, uint8_t* port)
{
	return sATCIPSTARTSingle("TCP", addr, port);
}
bool ESP8266_releaseSingleTCP(void)
{
	return eATCIPCLOSESingle();
}
bool ESP8266_registerSingleUDP(uint8_t* addr, uint8_t* port)
{
	return sATCIPSTARTSingle("UDP", addr, port);
}
bool ESP8266_unregisterSingleUDP(void)
{
	return eATCIPCLOSESingle();
}
bool ESP8266_createMultipleTCP(uint8_t mux_id, uint8_t* addr, uint8_t* port)
{
	return sATCIPSTARTMultiple(mux_id, "TCP", addr, port);
}
bool ESP8266_releaseMulitpleTCP(uint8_t mux_id)
{
	return sATCIPCLOSEMulitple(mux_id);
}
bool ESP8266_registerMultipleUDP(uint8_t mux_id, uint8_t* addr, uint8_t* port)
{
	return sATCIPSTARTMultiple(mux_id, "UDP", addr, port);
}
bool ESP8266_unregisterMulitpleUDP(uint8_t mux_id)
{
	return sATCIPCLOSEMulitple(mux_id);
}
bool ESP8266_setTCPServerTimeout(uint16_t timeout)
{
	return sATCIPSTO(timeout);
}
static bool startTCPServer(uint8_t* port)
{
	if (sATCIPSERVER(1, port)) {
		return true;
	}
	return false;
}
static bool stopTCPServer(void)
{
	sATCIPSERVER(0, 0);
	ESP8266_restart();
	return false;
}
bool ESP8266_startServer(uint8_t* port)
{
	return startTCPServer(port);
}
bool ESP8266_stopServer(void)
{
	return stopTCPServer();
}
bool ESP8266_send(const uint8_t *buffer, uint16_t len)
{
	return sATCIPSENDSingle(buffer, len);
}
bool ESP8266_sendM(uint8_t mux_id, const uint8_t *buffer, uint16_t len)
{
	return sATCIPSENDMultiple(mux_id, buffer, len);
}
uint16_t ESP8266_recv(uint16_t timeout)
{
	return recvPkg(NULL, timeout);
}
uint16_t ESP8266_recvM1(uint8_t mux_id, uint16_t timeout)
{
	uint8_t id;
	uint16_t ret;
	ret = recvPkg(&id, timeout);
	if (ret > 0 && id == mux_id) {
		return ret;
	}
	return 0;
}
uint16_t ESP8266_recvM2(uint8_t *coming_mux_id, uint16_t timeout)
{
	return recvPkg(coming_mux_id, timeout);
}
