C51_ESP8266

A sample code of using 51 MCU (STC15W408AS) to control ESP8266 WiFi module.
Impletment a ESP8266 library simulating WeeESP8266 (https://github.com/itead/ITEADLIB_Arduino_WeeESP8266)  

Complie this code in Keil. Set LX51 for Linker, add "REMOVEUNUSED" setting for Linker, otherwise uncalled function will occupy a lot of RAM. Please set correct overlay if you use function pointer in program to make sure Keil get a right call tree.
