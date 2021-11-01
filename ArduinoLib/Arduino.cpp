#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include "Arduino.h"

char **our_argv;                // our argv for restarting
std::string our_dir;            // our storage directory, including trailing /


// how we were made
#if defined(_USE_FB0)
  #if defined(_CLOCK_1600x960)
      char our_make[] = "hamclock-fb0-1600x960";
  #elif defined(_CLOCK_2400x1440)
      char our_make[] = "hamclock-fb0-2400x1440";
  #elif defined(_CLOCK_3200x1920)
      char our_make[] = "hamclock-fb0-3200x1920";
  #else
      char our_make[] = "hamclock-fb0-800x480";
  #endif
#elif defined(_USE_X11)
  #if defined(_CLOCK_1600x960)
      char our_make[] = "hamclock-1600x960";
  #elif defined(_CLOCK_2400x1440)
      char our_make[] = "hamclock-2400x1440";
  #elif defined(_CLOCK_3200x1920)
      char our_make[] = "hamclock-3200x1920";
  #else
      char our_make[] = "hamclock-800x480";
  #endif
#else
  #error Unknown build configuration
#endif
 

/* return milliseconds since first call
 */
uint32_t millis(void)
{
	static struct timeval t0;

	struct timeval t;
	gettimeofday (&t, NULL);

	if (t0.tv_sec == 0 && t0.tv_usec == 0)
	    t0 = t;

	int32_t dt_ms = (t.tv_sec - t0.tv_sec)*1000 + (t.tv_usec - t0.tv_usec)/1000;
	// printf ("millis %u: %ld.%06ld - %ld.%06ld\n", dt_ms, t.tv_sec, t.tv_usec, t0.tv_sec, t0.tv_usec);
	return (dt_ms);
}

void delay (uint32_t ms)
{
	usleep (ms*1000);
}

long random(int max)
{
	return ((long)((max-1.0F)*::random()/RAND_MAX));
}

uint16_t analogRead(int pin)
{
	return (0);		// not supported on Pi, consider https://www.adafruit.com/product/1083
}

static void mvLog (const char *from, const char *to)
{
        std::string from_path = our_dir + from;
        std::string to_path = our_dir + to;
        const char *from_fn = from_path.c_str();
        const char *to_fn = to_path.c_str();
        if (rename (from_fn, to_fn) < 0 && errno != ENOENT) {
            // fails for a reason other than from does not exist
            fprintf (stderr, "rename(%s,%s): %s\n", from_fn, to_fn, strerror(errno));
            exit(1);
        }
}


/* roll log files and change stdout to fresh file in our_dir
 */
static void stdout2File()
{
        // save previous few
        mvLog ("diagnostic-log-1.txt", "diagnostic-log-2.txt"); 
        mvLog ("diagnostic-log-0.txt", "diagnostic-log-1.txt"); 
        mvLog ("diagnostic-log.txt",   "diagnostic-log-0.txt"); 

        // reopen stdout as new log
        std::string new_log = our_dir + "diagnostic-log.txt";
        const char *new_log_fn = new_log.c_str();
        int logfd = open (new_log_fn, O_WRONLY|O_CREAT, 0664);
        close (1);      // insure disconnect from previous log file
        if (logfd < 0 || ::dup2(logfd, 1) < 0) {
            fprintf (stderr, "%s: %s\n", new_log_fn, strerror(errno));
            exit(1);
        }
        fchown (logfd, getuid(), getgid());

        // just need fd 1
        close (logfd);

        // note
        printf ("log file is %s\n", new_log_fn);
}

/* return default working directory
 */
static std::string defaultAppDir()
{
        std::string home = getenv ("HOME");
        return (home + "/.hamclock/");
}

/* insure our application work directory exists and named in our_dir.
 * use default unless user_dir.
 * exit if trouble.
 */
static void mkAppDir(const char *user_dir)
{
        // use user_dir else default
        if (user_dir) {
            our_dir = user_dir;
            // insure ends with /
            if (our_dir.compare (our_dir.length()-1, 1, "/")) {
                std::string slash = "/";
                our_dir = our_dir + slash;
            }
        } else {
            // use default
            our_dir = defaultAppDir();
        }

        // insure exists, fine if already created
        const char *path = our_dir.c_str();
        mode_t old_um = umask(0);
        if (mkdir (path, 0775) < 0 && errno != EEXIST) {
            // EEXIST just means it already exists
            fprintf (stderr, "%s: %s\n", path, strerror(errno));
            exit(1);
        }
        chown (path, getuid(), getgid());
        umask(old_um);
}

/* show usage and exit(1)
 */
static void usage (const char *errfmt, ...)
{
        char *slash = strrchr (our_argv[0], '/');
        char *me = slash ? slash+1 : our_argv[0];

        if (errfmt) {
            va_list ap;
            va_start (ap, errfmt);
            fprintf (stderr, "Usage error: ");
            vfprintf (stderr, errfmt, ap);
            va_end (ap);
            if (!strchr(errfmt, '\n'))
                fprintf (stderr, "\n");
        }

        fprintf (stderr, "Purpose: display time and other information useful to amateur radio operators\n");
        fprintf (stderr, "Usage: %s [options]\n", me);
        fprintf (stderr, "Options:\n");
        fprintf (stderr, " -b h : set backend host to h instead of %s\n", svr_host);
        fprintf (stderr, " -d d : set working dir d instead of %s\n", defaultAppDir().c_str());
        fprintf (stderr, " -f o : display full screen initially \"on\" or \"off\"\n");
        fprintf (stderr, " -g   : init DE using geolocation with our IP; requires -k\n");
        fprintf (stderr, " -i i : init DE using geolocation with IP i; requires -k\n");
        fprintf (stderr, " -k   : don't offer Setup or wait for Skips\n");
        fprintf (stderr, " -l l : set mercator center lng to l degs; requires -k\n");
        fprintf (stderr, " -m   : enable demo mode\n");
        fprintf (stderr, " -o   : write diagnostic log to stdout instead of in working dir\n");
        fprintf (stderr, " -w p : set web server port p instead of %d\n", svr_port);

        exit(1);
}

/* process main's argc/argv -- never returns if any issues
 */
static void crackArgs (int ac, char *av[])
{
        bool diag_to_file = true;
        bool full_screen = false;
        bool fs_set = false;
        const char *new_appdir = NULL;
        bool cl_set = false;

         while (--ac && **++av == '-') {
            char *s = *av;
            while (*++s) {
                switch (*s) {
                case 'b':
                    if (ac < 2)
                        usage ("missing host name for -b");
                    svr_host = *++av;
                    ac--;
                    break;
                case 'd':
                    if (ac < 2)
                        usage ("missing directory path for -d");
                    new_appdir = *++av;
                    ac--;
                    break;
                case 'f':
                    if (ac < 2) {
                        usage ("missing arg for -f");
                    } else {
                        char *oo = *++av;
                        if (strcmp (oo, "on") == 0)
                            full_screen = true;
                        else if (strcmp (oo, "off") == 0)
                            full_screen = false;
                        else
                            usage ("-f requires on or off");
                        ac--;
                        fs_set = true;
                    }
                    break;
                case 'g':
                    init_iploc = true;
                    break;
                case 'i':
                    if (ac < 2)
                        usage ("missing IP for -i");
                    init_locip = *++av;
                    ac--;
                    break;
                case 'k':
                    skip_skip = true;
                    break;
                case 'l':
                    if (ac < 2)
                        usage ("missing longitude for -l");
                    setCenterLng(atoi(*++av));
                    cl_set = true;
                    ac--;
                    break;
                case 'm':
                    setDemoMode(true);
                    break;
                case 'o':
                    diag_to_file = false;
                    break;
                    break;
                case 'w':
                    if (ac < 2)
                        usage ("missing port number for -w");
                    svr_port = atoi(*++av);
                    ac--;
                    break;
                default:
                    usage ("unknown option: %c", *s);
                }
            }
        }
        if (ac > 0)
            usage ("extra args");
        if (init_iploc && init_locip)
            usage ("can not use both -g and -i");
        if (init_iploc && !skip_skip)
            usage ("-g requires -k");
        if (init_locip && !skip_skip)
            usage ("-i requires -k");
        if (cl_set && !skip_skip)
            usage ("-l requires -k");

        // prepare our working directory in our_dir
        mkAppDir (new_appdir);

        // redirect stdout to diag file unless requested not to
        if (diag_to_file)
            stdout2File();

        // set desired screen option if set
        if (fs_set)
            setX11FullScreen (full_screen);
}

/* Every normal C program requires a main().
 * This is provided as magic in the Arduino IDE so here we must do it ourselves.
 */
int main (int ac, char *av[])
{
	// save our args for identical restart or remote update
	our_argv = av;

        // always want stdout synchronous 
        setbuf (stdout, NULL);

        // check args
        crackArgs (ac, av);

        // log args after cracking so they go to proper diag file
        printf ("\nNew program args:\n");
        for (int i = 0; i < ac; i++)
            printf ("  argv[%d] = %s\n", i, av[i]);

        // log our working dir
        printf ("working directory is %s\n", our_dir.c_str());

	// call Arduino setup one time
        printf ("Calling Arduino setup()\n");
	setup();

        // usage stats
        struct rusage ru0;
        struct timeval tv0;
        memset (&ru0, 0, sizeof(ru0));
        memset (&tv0, 0, sizeof(tv0));
        bool usage_init = false;

	// call Arduino loop forever
        printf ("Starting Arduino loop()\n");
	for (;;) {
	    loop();

            // this loop by itself would run 100% CPU so try to be a better citizen and throttle back

            // measure elapsed time during previous loop
            struct timeval tv1;
            gettimeofday (&tv1, NULL);
            int et_us = (tv1.tv_sec - tv0.tv_sec)*1000000 + (tv1.tv_usec - tv0.tv_usec);
            tv0 = tv1;

            // measure cpu time used during previous loop
            struct rusage ru1;
            getrusage (RUSAGE_SELF, &ru1);
            struct timeval *ut0 = &ru0.ru_utime;
            struct timeval *ut1 = &ru1.ru_utime;
            struct timeval *st0 = &ru0.ru_stime;
            struct timeval *st1 = &ru1.ru_stime;
            int ut_us = (ut1->tv_sec - ut0->tv_sec)*1000000 + (ut1->tv_usec - ut0->tv_usec);
            int st_us = (st1->tv_sec - st0->tv_sec)*1000000 + (st1->tv_usec - st0->tv_usec);
            int cpu_us = ut_us + st_us;
            ru0 = ru1;
            // printf ("ut %d st %d et %d\n", ut_us, st_us, et_us);

            // cap cpu usage a little below max
            #define MAX_CPU_USAGE 0.9F
            int s_us = cpu_us/MAX_CPU_USAGE - et_us;
            if (usage_init && s_us > 0)
                usleep (s_us);
            usage_init = true;

	}
}
