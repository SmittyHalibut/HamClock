#ifndef _WIFI_SERVER_H
#define _WIFI_SERVER_H

#include "WiFiClient.h"

class WiFiServer {

    public:

	WiFiServer(int newport);
	void begin();
	WiFiClient available();
        void stop();

    private:

	int port;
	int socket;
};


#endif // _WIFI_SERVER_H
