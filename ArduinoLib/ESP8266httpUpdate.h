#ifndef _HTTP_UPDATE_H
#define _HTTP_UPDATE_H

#include "WiFiClient.h"

typedef int t_httpUpdate_return;

class ESPhttpUpdate
{
    public:

        ESPhttpUpdate();

	t_httpUpdate_return update(WiFiClient &client, const char *url);
	int getLastError(void);
	String getLastErrorString(void);
        void onProgress (void (*progressCB)(int current, int total));

    private:
        bool runCommand (bool safe, int p0, int p1, int pn, const char *fmt, ...);
        bool findFullPath (const char *argv0, char fullpath[], size_t fplen);
        void cleanupDir (const char *tmp);
        void (*progressCB)(int current, int total);
        void prError (const char *fmt, ...);

        // rolling list of several malloced lines
        #define MAXERRLINES 10
        int err_lines_head;                     // index of next == oldest
        char *err_lines_q[MAXERRLINES];

};

enum {
	HTTP_UPDATE_OK,
	HTTP_UPDATE_FAILED,
	HTTP_UPDATE_NO_UPDATES,
};

extern ESPhttpUpdate ESPhttpUpdate;

#endif // _HTTP_UPDATE_H
