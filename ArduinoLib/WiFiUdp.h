#ifndef _WIFI_UDP_H
#define _WIFI_UDP_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>


#include "ESP8266WiFi.h"

class WiFiUDP {

    public:

	WiFiUDP();
	~WiFiUDP();
        operator bool() const { return (sockfd >= 0); }
	bool begin(int port);
        bool beginMulticast (IPAddress ifIP, IPAddress mcIP, int port);
	void beginPacket (const char *host, int port);
	void write (uint8_t *buf, int n);
	bool endPacket(void);
	int parsePacket();
        IPAddress remoteIP(void);
	int read(uint8_t *buf, int n);
	void stop();

    private:

	struct sockaddr_in remoteip;

	int sockfd;
	uint8_t r_buf[1024];
	int r_n, w_n;
	int sendto_n;
};

#endif // _WIFI_UDP_H
