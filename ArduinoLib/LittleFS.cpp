/* very minimal implementation of LittleFS over unix, including File and Dir supporting classes.
 * not intended to be complete, just what we need
 */

#include "LittleFS.h"


class LittleFS LittleFS;

static bool _trace_littlefs = false;


/******************************************************************
 *
 * File
 *
 ******************************************************************/


File::File()
{
        fp = NULL;
}

File::operator bool()
{
        if (fp) {
            if (_trace_littlefs)
                printf ("%s: file is open\n", fpath.c_str());
            return (true);
        } else {
            if (_trace_littlefs)
                printf ("file is closed\n");
            return (false);
        }
}

size_t File::write (char *buf, size_t count)
{
        size_t nw = fwrite (buf, 1, count, fp);
        if (nw != count)
            printf ("%s: write ask %u wrote %u\n", fpath.c_str(), (unsigned) count, (unsigned) nw);
        return (nw);
}

size_t File::read (uint8_t *buf, size_t count)
{
        size_t nr = fread (buf, 1, count, fp);
        if (nr != count)
            printf ("%s: read ask %u read %u: %s\n", fpath.c_str(), (unsigned) count, (unsigned) nr, feof(fp) ? "eof" : "error");
        return (nr);
}

size_t File::size()
{
        struct stat sbuf;
        if (fstat (fileno(), &sbuf) < 0) {
            printf ("%s: size fstat(%d): %s\n", fpath.c_str(), fileno(), strerror(errno));
            return (0);
        }
        return (sbuf.st_size);
}

void File::close(void)
{
        if (fp) {
            if (_trace_littlefs)
                printf ("%s: closing file\n", fpath.c_str());
            fclose(fp);
            fp = NULL;
        } else {
            printf ("No file to close\n");
        }
}

time_t File::getCreationTime()
{
        struct stat sbuf;
        if (fstat (fileno(), &sbuf) < 0) {
            printf ("%s: time fstat(%d): %s\n", fpath.c_str(), fileno(), strerror(errno));
            return (0);
        }
        return (sbuf.st_mtime);
}

bool File::seek (size_t offset, int whence)
{
        return (fseek (fp, offset, whence) == 0);
}

// non-standard extension
int File::fileno()
{
        return (::fileno(fp));
}





/******************************************************************
 *
 * Dir
 *
 ******************************************************************/

Dir::Dir()
{
        dir = NULL;
}

Dir::~Dir()
{
        if (_trace_littlefs)
            printf ("dir %p: Dir destructor\n", dir);
        if (dir)
            closedir(dir);
}


bool Dir::next()
{
        struct dirent *ent;

        while ((ent = readdir (dir)) != NULL && ent->d_name[0] == '.')
            continue;

        if (ent) {
            fname = (const char *) ent->d_name;
            std::string fpath = our_dir + fname;
            struct stat mystat;
            stat (fpath.c_str(), &mystat);
            ctime = mystat.st_mtime;
            len = mystat.st_size;
        }

        return (ent != NULL);
}

Dir::operator bool()
{
        return (dir != NULL);
}

std::string Dir::fileName()
{
        return (fname);
}

time_t Dir::fileCreationTime()
{
        return (ctime);
}

size_t Dir::fileSize()
{
        return (len);
}



/******************************************************************
 *
 * LittleFS
 *
 ******************************************************************/



LittleFS::LittleFS()
{
}

LittleFS::~LittleFS()
{
}


void LittleFS::begin(void)
{
}

File LittleFS::open (const char *fname, const char *how)
{
        File f;
        f.fpath = our_dir + fname;
        const char *path = f.fpath.c_str();
        f.fp = fopen (path, how);
        if (f.fp) {
            if (_trace_littlefs)
                printf ("fopen(%s, %s): ok\n", path, how);
            if (strchr (how, 'w'))
                fchown (fileno(f.fp), getuid(), getgid());
        } else {
            f.errstr = strerror(errno);
            printf ("fopen(%s, %s): %s\n", path, how, f.errstr.c_str());
        }

        return (f);
}

void LittleFS::remove (const char *fname)
{
        std::string fpath = our_dir + fname;
        if (unlink (fpath.c_str()) < 0)
            printf ("unlink(%s): %s\n", fpath.c_str(), strerror(errno));
        else if (_trace_littlefs)
            printf ("unlink(%s): ok\n", fpath.c_str());
}

void LittleFS::info (FSInfo &info)
{
        struct statvfs svs;
        if (statvfs (our_dir.c_str(), &svs) < 0) {
            printf ("statvfs(%s): %s\n", our_dir.c_str(), strerror(errno));
            info.totalBytes = 0;
            info.usedBytes = 0;
        } else {
            info.totalBytes = svs.f_blocks * svs.f_frsize;
            info.usedBytes = (svs.f_blocks - svs.f_bavail) * svs.f_frsize;
        }
}

Dir LittleFS::openDir (const char *dname)
{
        // we always use our_dir
        (void) dname;

        static Dir d;   // lazy way to avoid getting destructed
        d.dir = opendir (our_dir.c_str());
        if (!d.dir)
            printf ("opendir(%s): %s\n", our_dir.c_str(), strerror(errno));
        else if (_trace_littlefs)
            printf ("opendir %s\n", our_dir.c_str());
        return (d);
}
