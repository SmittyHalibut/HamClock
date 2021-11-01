/* very minimal implementation of LittleFS over unix, including File and Dir supporting classes.
 * not intended to be complete, just what we need
 */

#ifndef _LITTLEFS_H
#define _LITTLEFS_H

#include <string>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "Arduino.h"

#define LFS_NAME_MAX    32              // strlen, ie, sans EOS
#define SeekSet         SEEK_SET

class File {

    public:

        File();

        operator bool();
        size_t write (char *buf, size_t count);
        size_t read (uint8_t *buf, size_t count);
        size_t size();
        void close(void);
        time_t getCreationTime();
        bool seek (size_t offset, int whence);

        // non-standard
        int fileno(void);
        std::string fpath;              // full path
        std::string errstr;             // strerror(errno)

    private:

        FILE *fp;

        friend class LittleFS;          // to set fp and fpath
};

typedef struct {

    size_t totalBytes;
    size_t usedBytes;

} FSInfo;

class Dir {

    public:

        Dir();
        ~Dir();

        bool next();
        operator bool();
        std::string fileName();
        time_t fileCreationTime();
        size_t fileSize();

    private:

        DIR *dir;
        std::string fname;              // direct->d_name
        time_t ctime;                   // really mtime
        size_t len;

        friend class LittleFS;          // to set dir
};

class LittleFS {

    public:

        LittleFS();
        ~LittleFS();

        void begin(void);
        void setTimeCallback (time_t (*f)()) { (void)f; }       // ignored
        File open (const char *filename, const char *how);
        void remove (const char *filename);
        void info (FSInfo &info);
        Dir openDir (const char *path);

    private:

};

extern LittleFS LittleFS;

# endif // _LITTLEFS_H
