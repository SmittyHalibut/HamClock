/* simple network interface functions for unix.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>



#include "ESP8266WiFi.h"

class WiFi WiFi;

/* run shell cmd and return the first line of response.
 * return whether ok
 */
static bool getCommand (const char cmd[], char line[], size_t line_len)
{
        // Serial.printf ("getCommand: %s\n", cmd);

	line[0] = '\0';
	FILE *pp = popen (cmd, "r");
	if (!pp)
	    return (false);
	bool ok = fgets (line, line_len, pp) != NULL;
	int wstatus = pclose (pp);
        // printf ("cmd= '%s':\n  ok= %d wstatus=%d line= '%s'\n", cmd, ok, wstatus, line);
        if (ok && WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0 && strlen(line) > 1) {
            line[strlen(line)-1] = '\0';        // rm \n
            return (true);
        }
	return (false);
}

/* convert line containing a.b.c.d into IPaddress a.
 * return whether ok
 */
static bool crackIP (const char line[], IPAddress &a)
{
        int i[4] = {0, 0, 0, 0};

        if (sscanf (line, "%d.%d.%d.%d", &i[0], &i[1], &i[2], &i[3]) != 4)
            return (false);

        a[0] = i[0];
        a[1] = i[1];
        a[2] = i[2];
        a[3] = i[3];
        return (true);
}

/* convert line containing a.b.c.d/m into IPaddress mask.
 * return whether ok
 */
static bool crackCIDR (const char line[], IPAddress &m)
{
        int i[5] = {0, 0, 0, 0, 0};

        // printf ("CIDR: %s\n", line);
        if (sscanf (line, "%d.%d.%d.%d/%d", &i[0], &i[1], &i[2], &i[3], &i[4]) != 5)
            return (false);

        uint32_t mask = ~((1L << (32-i[4])) - 1);
        // printf ("mask %d 0x%8X\n", i[4], mask);

        m[0] = (mask >> 24) & 0xFF;
        m[1] = (mask >> 16) & 0xFF;
        m[2] = (mask >>  8) & 0xFF;
        m[3] = (mask >>  0) & 0xFF;
        return (true);
}

void WiFi::begin (char *ssid, char *pw)
{
#if defined(_IS_LINUX)

	// set wpa_supplicant if given ssid and pw.

	static const char wpafn[] = "/etc/wpa_supplicant/wpa_supplicant.conf";

        if (!ssid || ssid[0]=='\0' || !pw || pw[0]=='\0')
            return;

	// create/overwrite supp file
        printf ("Creating %s with %s/%s\n", wpafn, ssid, pw);
	FILE *wfp = fopen (wpafn, "w");
	if (!wfp) {
	    printf ("Can not create %s: %s\n", wpafn, strerror(errno));
	    return;
	}

	// country=US makes a good default because it has the most restricted subset of channels,
	//  see https://en.wikipedia.org/wiki/List_of_WLAN_channels
        fprintf (wfp, "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n");
        fprintf (wfp, "update_config=1\n");             // allow wpa_cli commands
        fprintf (wfp, "country=US\n");                  // most conservative set of channels
        fprintf (wfp, "network={\n");
            fprintf (wfp, "\tssid=\"%s\"\n", ssid);
            fprintf (wfp, "\tpsk=\"%s\"\n", pw);
            fprintf (wfp, "\tscan_ssid=1\n");           // allow invisible networks
        fprintf (wfp, "}\n");

	fclose (wfp);

	// restart, but don't wait here
        printf ("restarting wlan0\n");
        system ("wpa_cli -i wlan0 reconfigure");

#endif // _IS_LINUX
}


IPAddress WiFi::localIP(void)
{
	static IPAddress a;                     // cache once found

        // try cache first
        if (a[0] != 0)
            return (a);

        // create socket back to home base then get our IP from that
        const char *host = "clearskyinstitute.com";
        const int port = 80;


        // lookup host address, retry for several seconds in case network still coming up after host power-on
        // N.B. must call freeaddrinfo(aip) after successful call before returning
        struct addrinfo hints, *aip = NULL;
        char port_str[16];
        memset (&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        sprintf (port_str, "%d", port);
        int error = 1;
        for (time_t start_t = time(NULL); error && time(NULL) < start_t + 10; ) {
            error = ::getaddrinfo (host, port_str, &hints, &aip);
            if (error) {
                printf ("getaddrinfo(%s:%d): %s\n", host, port, gai_strerror(error));
                usleep (1000000);
                aip = NULL;
            }
        }
        if (!aip || error)
            return (a);

        // create socket
        int sockfd;
        sockfd = ::socket (aip->ai_family, aip->ai_socktype, aip->ai_protocol);
        if (sockfd < 0) {
            freeaddrinfo (aip);
            printf ("socket(%s:%d): %s\n", host, port, strerror(errno));
            return (a);
        }

        // connect
        if (::connect (sockfd, aip->ai_addr, aip->ai_addrlen) < 0) {
            printf ("connect(%s,%d): %s\n", host, port, strerror(errno));
            freeaddrinfo (aip);
            close (sockfd);
            return (a);
        }

        // finished with aip
        freeaddrinfo (aip);

        // get local side ip
        struct sockaddr_in sa;
        socklen_t sl = sizeof(sa);
        if (::getsockname (sockfd, (struct sockaddr *)&sa, &sl) < 0) {
            printf ("getsockname(%s,%d): %s\n", host,port,strerror(errno));
            close (sockfd);
            return (a);
        }

        // finished with socket
        close (sockfd);

        // crack addr
        char addr[32];
        strcpy (addr, inet_ntoa(sa.sin_addr));
        if (!crackIP (addr, a)) {
            printf ("bogus local IP: %s\n", addr);
            return (a);
        }

        // ok
        return (a);

}

IPAddress WiFi::subnetMask(void)
{
	static IPAddress a;                     // retain as cache
	char cmd[256], back[256];

        // try cache first
        if (a[0] != 0)
            return (a);

        strcpy (cmd, "[ -x /sbin/ip ] && /sbin/ip address show | awk '/inet / && !/127.0.0.1/{print $2}'");
	if (getCommand (cmd, back, sizeof(back)) && crackCIDR (back, a))
            return (a);

	strcpy (cmd, "[ -x /sbin/ifconfig ] && /sbin/ifconfig | awk '/ netmask / && !/127.0.0.1/{print $4}'");
	if (getCommand (cmd, back, sizeof(back)) && crackIP (back, a))
            return (a);

	// works on a line of the form inet 192.168.7.11 netmask 0xffffff00 broadcast 192.168.7.255
	strcpy (cmd, "[ -x /sbin/ifconfig ] && /sbin/ifconfig "
            "| grep -v '127.0.0.1' "
            "| awk '/netmask *0x/{printf \"%d.%d.%d.%d\\n\", $4/(2^24), ($4/(2^15))%256, ($4/2^8)%256, $4%256}'"
            "| head -1");
	if (getCommand (cmd, back, sizeof(back)) && crackIP (back, a))
            return (a);

        // default 0
	return (a);
}

IPAddress WiFi::gatewayIP(void)
{
	static IPAddress a;                     // retain as cache
	char cmd[128], back[128];

        // try cache first
        if (a[0] != 0)
            return (a);

        strcpy (cmd,  "[ -x /sbin/ip ] && /sbin/ip route show default | awk '/default via/{print $3}'");
	if (getCommand (cmd, back, sizeof(back)) && crackIP (back, a))
            return (a);

        strcpy (cmd,  "netstat -rn | awk '(/^0.0.0.0/ || /^default/) && !/::/{print $2}'");
	if (getCommand (cmd, back, sizeof(back)) && crackIP (back, a))
            return (a);

        // default 0
	return (a);
}

IPAddress WiFi::dnsIP(void)
{
	static IPAddress a;                     // retain as cache
	char cmd[128], back[128];

        // try cache first
        if (a[0] != 0)
            return (a);

	strcpy (cmd, "awk '/nameserver/{print $2}' /etc/resolv.conf | head -1");
	if (getCommand (cmd, back, sizeof(back)) && crackIP (back, a))
            return (a);

        // default 0
	return (a);
}

int WiFi::RSSI(void)
{
	// returning value > 31 signifies error
	int rssi = 100;

#ifdef _IS_LINUX

        FILE *fp = fopen ("/proc/net/wireless", "r");
        if (fp) {
            char buf[200];
            while (fgets (buf, sizeof(buf), fp)) {
                float rssif;
                if (sscanf (buf, " wlan0: %*f %*f %f %*f", &rssif) == 1) {
                    rssi = rssif;
                    break;
                }
            }
            fclose (fp);
        }

#endif // _IS_LINUX

#ifdef __APPLE__

        const char cmd[] =
            "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -I | "
            "grep CtlRSSI";
        char ret[256];
        int apple_rssi;

        if (getCommand (cmd, ret, sizeof(ret)) && sscanf (ret, " agrCtlRSSI: %d", &apple_rssi) == 1)
            rssi = apple_rssi;

#endif // __APPLE__

	return (rssi);
}

int WiFi::status(void)
{
	// get list of all interfaces
	struct ifaddrs *ifp0;
	if (getifaddrs(&ifp0) < 0) {
	    printf ("getifaddrs(): %s\n", strerror(errno));
	    return (WL_OTHER);
	}

	// scan for AF_INET and adder other than 127.0.0.1
	bool ok = false;
	for (struct ifaddrs *ifp = ifp0; ifp != NULL && !ok; ifp = ifp->ifa_next) {
	    if (ifp->ifa_addr && ifp->ifa_addr->sa_family == AF_INET) {
		void *addr_in = &((struct sockaddr_in *)ifp->ifa_addr)->sin_addr;
		char *addr_a = inet_ntoa(*(struct in_addr*)addr_in);
		if (strcmp ("127.0.0.1", addr_a)) {
		    // printf ("interface %s: %s\n", ifp->ifa_name, addr_a);
		    ok = true;
		}
	    }
	}

	// free list
	freeifaddrs (ifp0);

	// return result code
        if (!ok)
            printf ("no net connections\n");
	return (ok ? WL_CONNECTED : WL_OTHER);
}

int WiFi::mode (int m)
{
	return (WIFI_OTHER);
}

std::string WiFi::macAddress(void)
{
	char line[128];

	// try a few different variations, first two try to find the default interface
        static const char *cmds[] = {
            "[ -x /sbin/ip ] && /sbin/ip addr show dev "
                "`/sbin/ip route show default 0.0.0.0/0 | perl -n -e '/default.* dev (\\S+) / and print $1'`"
                "| perl -n -e '/ether ([a-fA-F0-9:]+)/ and print \"$1\\n\"'",
            "[ -x /sbin/ifconfig -a -x /sbin/route ] && /sbin/ifconfig "
                "`/sbin/route -n get 8.8.8.8 | awk '/interface/{print $2}'` | awk '/ether/{print $2}'",
            "[ -x /sbin/ifconfig ] && /sbin/ifconfig | awk '/ether/{print $2}' | head -1",
            "[ -x /sbin/ifconfig ] && /sbin/ifconfig | awk '/HWaddr/{print $5}' | head -1",
        };
        const int n_cmds = sizeof(cmds)/sizeof(cmds[0]);

        for (int i = 0; i < n_cmds; i++) {
            if (getCommand (cmds[i], line, sizeof(line))) {
                // insure 5 :
                unsigned int m1, m2, m3, m4, m5, m6;
                if (sscanf (line, "%x:%x:%x:%x:%x:%x", &m1, &m2, &m3, &m4, &m5, &m6) == 6)
                    break;
            }
            strcpy (line, "FF:FF:FF:FF:FF:FF");
        }

	return (std::string(line));
}

std::string WiFi::hostname(void)
{
        char hn[512];
        if (gethostname (hn, sizeof(hn))) {
            strcpy (hn, "hostname??");
        } else {
            char *dot = strchr (hn, '.');
            if (dot)
                *dot = '\0';
        }
	return (std::string(hn));
}

int WiFi::channel(void)
{
	int channel = 0;
	FILE *pf = popen ("iw wlan0 info", "r");
	if (pf) {
	    char buf[1024];
	    while (fgets (buf, sizeof(buf), pf))
		if (sscanf (buf, " channel %d", &channel) == 1)
		    break;
	    pclose (pf);
	}
	return (channel);
}

std::string WiFi::SSID(void)
{
	return (std::string(""));
}

std::string WiFi::psk(void)
{
	return (std::string(""));
}
