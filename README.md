# Fridge-Interceptor
Haier HRQ16N3BGS fridge hacking with IOT.

![image](https://user-images.githubusercontent.com/841061/102672664-68bf5400-415f-11eb-94a4-25e50e1fb0cf.png)

The tool injects stream of bits between front door control panel and main fridge board so that it is possible to remotly schedule tempretures in certain times.
Even though patterns are distinguished in the signals, the protocol has not been reverse engineered.  
Instead, signals have been recorded for various commands from the front panel and stored in ROM on the interceptor. 
Based a schedule, a desierd commands signal is being replayed to the main board to trick it into thinking, the front panel requested the command.
On all other times, the bitstream is simply mirrored between front panel and the main board. 

Implemented on Adafruit HUZZAH ESP8266.

Functions:
- Uses wifi hotspot for extablishing connection. 
- The scheduler is hardcoded in the firmware. 
- firmware OTA within same wifi network for updating scheduler times.
- automatic time synchronization with an NTP server.
- telnet server to observe the communication and to paste any binary stream to be replayed real-time to fridge motherboard. 

Todo:
- Reverse engineer the protocol, to be able to read Fridge states (e.g. actual temperature)
- Fix real time clock drift over time. 
