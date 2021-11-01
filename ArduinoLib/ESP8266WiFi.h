#ifndef _ESP8266WIFI_H
#define _ESP8266WIFI_H

#include "IPAddress.h"
#include "Arduino.h"

enum {
    WL_CONNECTED,
    WL_OTHER
};

enum {
    WIFI_STA,
    WIFI_OTHER
};

class WiFi {

    public:

	void begin (char *ssid, char *pw);
	IPAddress localIP(void);
	IPAddress subnetMask(void);
	IPAddress gatewayIP(void);
	IPAddress dnsIP(void);
	int RSSI(void);
	int status(void);
	int mode (int m);
	std::string macAddress(void);
	std::string hostname(void);
	std::string SSID(void);
	std::string psk(void);
	int channel(void);
};

extern WiFi WiFi; 

#endif // _ESP8266WIFI_H
