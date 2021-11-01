#ifndef _WIFICLIENT_H
#define _WIFICLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>

/* version of Arduino WiFiClient that runs on rasp pi
 */

#include "Arduino.h"
#include "IPAddress.h"

class WiFiClient {

    public:

	WiFiClient();
	WiFiClient(int fd);
	bool connect (const char *host, int port);
	bool connect (IPAddress ip, int port);
	void stop (void);
	int available();
        void setNoDelay(bool on);
	bool connected();
	int read();
	operator bool();
	int write (const uint8_t *buf, int n);
	void print (void);
	void print (String s);
	void print (float f);
	void print (float f, int s);
	void println (void);
	void println (String s);
	void println (uint32_t ui);
	void println (int i);
	void println (float f);
	void println (float f, int n);
	void flush(void){};
	String remoteIP(void);

    private:

	int socket;
  	uint8_t peek[4096];             // read-ahead buffer
  	int n_peek;                     // n useful values in peek[]
        int next_peek;                  // next peek[] index to use


        int connect_to (int sockfd, struct sockaddr *serv_addr, int addrlen, int to_ms);
        int tout (int to_ms, int fd);

};



#endif // _WIFICLIENT_H
