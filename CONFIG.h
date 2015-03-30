#ifndef		__CONFIG_H
#define		__CONFIG_H

#define MAIN_Fosc		22118400L
#define SOFTUART_RX 0
#define SOFTUART_TX 1
#define DEBUG

#include	"STC15Fxxxx.H"

#ifdef DEBUG
extern bit debug_set;
#define DebugMsg(arg) do {debug_set = 1; printf(arg); debug_set = 0;} while(0)
#define DebugMsg2(arg1, arg2) do {debug_set = 1; printf(arg1, arg2); debug_set = 0;} while(0)
#define DebugMsg3(arg1, arg2, arg3) do {debug_set = 1; printf(arg1, arg2, arg3); debug_set = 0;} while(0)
#else
#define DebugMsg(arg)
#define DebugMsg2(arg1, arg2)
#define DebugMsg3(arg1, arg2, arg3)
#endif

#endif