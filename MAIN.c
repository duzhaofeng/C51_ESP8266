#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ESP8266.h"
#include "SOFTUART.h"
#include "APPASSWD.h"

#define WIRC 37286
#define AC_POWERON_TIME 60000
#define IS_LEAPYEAR(x) (!(x % 4) && ((x % 100) || !(x % 400)))
#define TRY(do_someing, times, _ret) \
	if (_ret){uint8_t i;               \
	for (i = 0; i < times; i++)        \
	{                                  \
		if((_ret = (do_someing))) break; \
		delay(500);                      \
	}}

sbit P_ESPCHIP_EN = P5^4;
sbit P_AC_SWITCH = P3^3;
sbit P_BEEP = P3^4;

bit debug_set;
uint8_t apfound;
uint8_t* target_ssid;
uint8_t adjust_seconds;

struct Time{
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
};

union TimeUnion {
	uint32_t ntp;
	struct Time time;
};

char putchar(char c)
{
	if (c == '\n')
	{
		if (debug_set) Uart3_Putchar(0x0d);
		else Uart_Putchar(0x0d);
	}
	if (debug_set) return Uart3_Putchar(c);
	else return Uart_Putchar(c);
}

void sleep_probable_time(void)
{
	uint16_t wktc_cnt = (WIRC / 16) * 10 - 1;	//10s
	WKTCH &= ~0x80;
	WKTCL = (uint8_t)wktc_cnt;
	WKTCH = (uint8_t)(wktc_cnt >> 8);
	WKTCH |= 0x80;
	PCON |= 0x02;
	_nop_();
	_nop_();
	_nop_();
	_nop_();
}

void sleep(uint16_t minutes)
{
	uint16_t count = (minutes * 60 + adjust_seconds - 1) / adjust_seconds;
	while(count--) sleep_probable_time();
}

uint8_t find_ap(uint8_t* linestr)
{
	uint8_t* ptr;
	ptr = strstr(linestr, "+CWLAP:");
	if (ptr)
	{
		if (strstr(ptr + 7, target_ssid)) apfound = 1;
		//DebugMsg2("%s\n", ptr + 7);
	}
	else if (strstr(linestr, "OK") || strstr(linestr, "ERROR"))
	{
		return 0;
	}
	return 1;
}

bool connect_to_ap(uint8_t* ssid, uint8_t* passwd)
{
	//	uint8_t *result;
	target_ssid = ssid;
	if (!target_ssid || !strlen(target_ssid))
	{
		return false;
	}
	if (!ESP8266_setOprToStation())
	{
		DebugMsg("Failed to set station mode\n");
		return false;
	}
	//DebugMsg("Scanning...\n");
	apfound = 0;
	ESP8266_getAPList(&find_ap);
	if(!apfound)
	{
		DebugMsg("Can't find target AP\n");
		return false;
	}
	//DebugMsg("Connecting to AP...\n");
	if (!ESP8266_joinAP(target_ssid, passwd))
	{
		DebugMsg("Fail to connect to target AP\n");
		return false;
	}
	//	result = ESP8266_getLocalIP();
	//	if (result)
	//	{
	//		DebugMsg2("IP:%s\n", result);
	//	}
	return true;
}

bool create_ntp_connection(void)
{
	//	uint8_t *result;
	if (!ESP8266_registerSingleUDP("193.92.150.3", "123"))
	{
		DebugMsg("Fail to set up UDP connection\n");
		return false;
	}
	//	result = ESP8266_getIPStatus();
	//	if (result)
	//	{
	//		DebugMsg2("%s\n", result);
	//	}
	return true;
}

void delete_ntp_connection(void)
{
	ESP8266_unregisterSingleUDP();
}

bool request_ntp(uint32_t *ntptime)
{
	uint16_t recvlen;
	uint8_t code packetBuffer[] = {
		// Initialize values needed to form NTP request
		0xE3,   			// LI, Version, Mode
		0,						// Stratum, or type of clock
		6,						// Polling Interval
		0xEC,  				// Peer Clock Precision
		0, 0, 0, 0,
		0, 0, 0, 0,
		// 8 bytes of zero for Root Delay & Root Dispersion
		49,
		0x4E,
		49,
		52,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0
	};
	if (!ESP8266_send(packetBuffer, sizeof(packetBuffer)))
	{
		DebugMsg("Fail to send NTP data\n");
		return false;
	}
	recvlen = ESP8266_recv(10000);
	//DebugMsg2("Received %u bytes data\n", recvlen);
	if (recvlen != sizeof(packetBuffer))
	{
		return false;
	}
	// the timestamp starts at byte 40 of the received packet and is four bytes,
	// or two words, long. First, esxtract the two words:
	// combine the four bytes (two words) into a long integer
	// this is NTP time (seconds since Jan 1 1900):
	*ntptime = (uint32_t)Rx_BUF[40] << 24 | (uint32_t)Rx_BUF[41] << 16 | (uint32_t)Rx_BUF[42] << 8 | Rx_BUF[43];
	return true;
}


void calculate_time(union TimeUnion* timeunion, sint8_t timezone)
{
	uint8_t code months[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30 };
  uint16_t tempdays;
	uint32_t tempticks = timeunion->ntp;
	struct Time *time = &timeunion->time;

	// now convert NTP time into everyday time:
	// Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
	// subtract seventy years:
	tempticks -= 2208988800UL;
	tempticks += timezone * 3600; //adjust time zone (3600 equals secs per hour)

	tempdays  = (tempticks / 86400L) + 1;	//(86400 equals secs per day)
	tempticks = (tempticks % 86400L);

	time->year = 1970;
	while (tempdays > 365)
	{
		time->year ++;
		tempdays -= 365;
		if (IS_LEAPYEAR(time->year) && tempdays)
		{
			tempdays --; //adjust leap year
		}
	}
	time->month = 0;
	while (tempdays > months[time->month])
	{
		if (IS_LEAPYEAR(time->year) && time->month == 1)
		{
			if (tempdays == 29)
			{
				break;
			}
			tempdays --;
		}
		tempdays -= months[time->month];
		time->month ++;
	}
	time->month ++;
	time->day    =  tempdays;
	time->hour   = (tempticks) / 3600;
	time->minute = (tempticks % 3600) / 60;
	time->second = (tempticks % 60);
}

void show_time(struct Time* now)
{
	debug_set = 1;
	printf("time: [%u-%02u-%02u %02u:%02u:%02u]\n", 
			(uint16_t)now->year, (uint16_t)now->month, (uint16_t)now->day,
			(uint16_t)now->hour, (uint16_t)now->minute, (uint16_t)now->second);
	debug_set = 0;
}

uint32_t get_ntp_time(void)
{
	uint8_t ret = true;
	uint32_t ntptime;
	P_ESPCHIP_EN = 1;
	delay(1000);
	TRY(connect_to_ap(AP_SSID, AP_PSWD), 3, ret);
	TRY(create_ntp_connection(), 3, ret);
	TRY(request_ntp(&ntptime), 3, ret);
	delete_ntp_connection();
	ESP8266_leaveAP();
	P_ESPCHIP_EN = 0;
	if(ret)
	{
		//DebugMsg2("NTP time: %lu\n", ntptime);
		return ntptime;
	}
	delay(1000);
	return 0;
}
void adjust_sleep_timer(uint32_t nowntp, uint16_t sleep_minute)
{
	static uint32_t last_ntp = 0;
	static uint8_t totalcount = 1;

	if (sleep_minute > 10)
	{
		uint16_t sleepcount = (sleep_minute * 60 + adjust_seconds - 1) / adjust_seconds;
		uint16_t totalseconds = (uint16_t)adjust_seconds * totalcount + (nowntp - last_ntp) / sleepcount;
		adjust_seconds = totalseconds / ++totalcount;
		if (totalcount > 48) totalcount = 1;
		DebugMsg3("Real seconds for sleeping is about %us, adjust seconds to %us\n", (uint16_t)(nowntp - last_ntp), (uint16_t)adjust_seconds);
	}
	last_ntp = nowntp;
}

void beep_time(uint8_t hour, uint8_t minute)
{
	static uint8_t last_hour = 0;
	static uint8_t last_minute = 0;
	minute = minute < 30 ? 0 : 1;
	if (hour != last_hour)
	{
		last_hour = hour;
		last_minute = minute;
		if (last_hour > 6 && last_hour < 23)
		{
			uint8_t bee = last_hour > 12 ? last_hour - 12 : last_hour;
			while (bee--)
			{
				P_BEEP = 1;
				delay(300);
				P_BEEP = 0;
				delay(700);
			}
		}
	}
	if (minute != last_minute)
	{
		last_minute = minute;
		if (last_hour > 6 && last_hour < 23)
		{
			P_BEEP = 1;
			delay(300);
			P_BEEP = 0;
		}
	}
}

void try_water_plants(struct Time* time)
{
	static uint8_t last_day = 0;
	if (time->day != last_day && time->hour == 21)
	{
		uint16_t start;
		last_day = time->day;
		DebugMsg("Water plants...");
		startTimer();
		start = millis();
		P_AC_SWITCH = 1;
		while (millis() - start < AC_POWERON_TIME)
		{
			beep_time(time->hour, time->minute);
			idle();
		}
		P_AC_SWITCH = 0;
		stopTimer();
		DebugMsg("...OK\n");
		time->minute++;
	}
	else
	{
		beep_time(time->hour, time->minute);
	}
}

void main(void)
{
	union TimeUnion time;
	uint16_t sleeptime = 0;

	ESP8266_init();
	Uart3_Init();
	//GPIO setting
	P3M1 &= ~0x18;
	P3M0 |= 0x18;
	P5M1 = 0x00;
	P5M0 = 0x10;
	P_AC_SWITCH = 0;
	P_BEEP = 0;
	P_ESPCHIP_EN = 0;
	EA = 1;

	adjust_seconds = 20;
	DebugMsg("!!!ESP8266 Auto-Water Plants Controller!!!\n");
	while (1)
	{
		while (!(time.ntp = get_ntp_time()));
		adjust_sleep_timer(time.ntp, sleeptime);
		calculate_time(&time, 8);
		show_time(&time.time);
		try_water_plants(&time.time);
		if (time.time.hour == 23)
		{
			sleeptime = 8 * 60 - time.time.minute;
		}
		else
		{
			sleeptime = 30 - time.time.minute % 30;
		}
		DebugMsg2("sleep %u minutes\n", sleeptime);
		while (isUartRunning() || isUart3Running());
		sleep(sleeptime);
		DebugMsg("wake up\n");
	}
}