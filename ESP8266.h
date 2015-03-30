#ifndef		__ESP8266_H
#define		__ESP8266_H

#include	"config.h"

typedef unsigned char	bool;
typedef unsigned char	uint8_t;
typedef signed char sint8_t;
typedef unsigned int	uint16_t;
typedef unsigned long	uint32_t;
typedef uint8_t (*line_process)(uint8_t* linestr);

#define false (0)
#define true  (1)

#define TIMER0_HZ (50)
#define	UART0_Baudrate 9600UL
#define	RX_BUF_Lenth		256

extern uint8_t Rx_number;
extern uint8_t xdata Rx_BUF[RX_BUF_Lenth];

void startTimer(void);
void stopTimer(void);
uint16_t millis(void);
void delay(uint16_t ms);
void idle(void);

bool isUartRunning(void);
uint8_t RX_IndexOf(uint8_t* target, uint8_t rx_offset);
void TX_Flush(void);
char Uart_Putchar (char c);

/*ESP8266 Function*/
void ESP8266_init(void);
bool ESP8266_kick(void);
bool ESP8266_restart(void);
uint8_t* ESP8266_getVersion(void);
bool ESP8266_setOprToStation(void);
bool ESP8266_setOprToSoftAP(void);
bool ESP8266_setOprToStationSoftAP(void);
void ESP8266_getAPList(line_process func);
bool ESP8266_joinAP(uint8_t* ssid, uint8_t* pwd);
bool ESP8266_leaveAP(void);
bool ESP8266_setSoftAPParam(uint8_t* ssid, uint8_t* pwd, uint8_t chl, uint8_t ecn);
uint8_t* ESP8266_getJoinedDeviceIP(void);
uint8_t* ESP8266_getIPStatus(void);
uint8_t* ESP8266_getLocalIP(void);
bool ESP8266_enableMUX(void);
bool ESP8266_disableMUX(void);
bool ESP8266_createSingleTCP(uint8_t* addr, uint8_t* port);
bool ESP8266_releaseSingleTCP(void);
bool ESP8266_registerSingleUDP(uint8_t* addr, uint8_t* port);
bool ESP8266_unregisterSingleUDP(void);
bool ESP8266_createMultipleTCP(uint8_t mux_id, uint8_t* addr, uint8_t* port);
bool ESP8266_releaseMulitpleTCP(uint8_t mux_id);
bool ESP8266_registerMultipleUDP(uint8_t mux_id, uint8_t* addr, uint8_t* port);
bool ESP8266_unregisterMulitpleUDP(uint8_t mux_id);
bool ESP8266_setTCPServerTimeout(uint16_t timeout);
bool ESP8266_startServer(uint8_t* port);
bool ESP8266_stopServer(void);
bool ESP8266_send(const uint8_t *buffer, uint16_t len);
bool ESP8266_sendM(uint8_t mux_id, const uint8_t *buffer, uint16_t len);
uint16_t ESP8266_recv(uint16_t timeout);
uint16_t ESP8266_recvM1(uint8_t mux_id, uint16_t timeout);
uint16_t ESP8266_recvM2(uint8_t *coming_mux_id, uint16_t timeout);

#endif