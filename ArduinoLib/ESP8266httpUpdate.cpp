#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>


#include "Arduino.h"
#include "ESP.h"
#include "ESP8266httpUpdate.h"

// one global instance
class ESPhttpUpdate ESPhttpUpdate;


// approximate number of lines generated when running unzip and make, used for progress meter.
#define N_UNZIP_LINES   90
#define N_MAKE_LINES    72


ESPhttpUpdate::ESPhttpUpdate()
{
        err_lines_head = 0;
        memset (err_lines_q, 0, sizeof(err_lines_q));
}


/* fork/execl sh to run cmd created with fmt, call progressCB if set, return true if program exits 0,
 *   else call prError and return false.
 * p0 and p1 are the start and ending percentage progress range with respect to pn for progressCB as we
 *   read approx pn lines; set pn to 0 if don't want the progressCB callback.
 * if use_euid then command is run with permissions of our euid, else it is run with our uid.
 */
bool ESPhttpUpdate::runCommand (bool use_euid, int p0, int p1, int pn, const char *fmt, ...)
{
        // expand full command
        char cmd[1000];
        va_list ap;
        va_start (ap, fmt);
        vsnprintf (cmd, sizeof(cmd), fmt, ap);
        va_end (ap);

        printf ("OTA: Running: %s\n", cmd);

        // create pipe for parent to read from child
        int pipe_fd[2];
        if (pipe (pipe_fd) < 0) {
	    prError ("pipe(2) failed: %s\n", strerror(errno));
            return (false);
        }

        // start new process as clone of us
        int pid = fork();
        if (pid < 0) {
	    prError ("fork(2) failed: %s\n", strerror(errno));
            return (false);
        }

        // now two processes running concurrently

        if (pid == 0) {
            // new child process continues here

            // don't need read end of pipe
            close (pipe_fd[0]);

            // arrange stdout/err to write into pipe_fd[1] to parent
            dup2 (pipe_fd[1], 1);
            dup2 (pipe_fd[1], 2);

            // set uid for sh if need euid
            if (use_euid)
                setuid(geteuid());
            else
                seteuid(getuid());
            // printf ("OTA: uid %d euid %d\n", getuid(), geteuid());

            // go
            execl ("/bin/sh", "sh", "-c", cmd, NULL);

            printf ("OTA: Can not exec %s: %s\n", cmd, strerror(errno));
            exit(1);
        }

        // parent process continues here

        // don't need write end of pipe
        close (pipe_fd[1]);

        // decide whether we want to invoke progress callback
        bool want_cb = progressCB && pn > 0;

        // start with progress p0
        if (want_cb)
            (*progressCB) (p0, 100);

        // parent arranges to read from pipe_fd[0] from child until EOF
        FILE *rsp_fp = fdopen (pipe_fd[0], "r");

        // read and log output, report progress if desired
        for (int nlines = 0; ; nlines++) {

            if (want_cb) {
                int percent = p0 + nlines*(p1 - p0)/pn;
                if (percent > p1)         // N.B. maxlines is just an estimate
                    percent = p1;
                (*progressCB) (percent, 100);
            }

            char rsp[1000];
            if (!fgets (rsp, sizeof(rsp), rsp_fp))
                break;
            prError ("%s", rsp);                // already includes nl
        }

        // always end with final progress p1
        if (want_cb)
            (*progressCB) (p1, 100);

        // finished with pipe
        fclose (rsp_fp);        // also closes(pipe_fd[0])

        // parent waits for child
        int wstatus;
        if (waitpid (pid, &wstatus, 0) < 0) {
	    prError ("waitpid(2) failed: %s\n", strerror(errno));
            return (false);
        }

        // finished, report any error status
	if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
	    return (false);

        printf ("OTA: cmd ok\n");
	return (true);
}

/* given argv[0] find our full real path and whether it can be removed (containing dir is writable).
 * if ok return true else false and call prError to report reason.
 * N.B. beware symlinks, we want the read deal.
 */
bool ESPhttpUpdate::findFullPath (const char *argv0, char full_path[], size_t fplen)
{
        // get current dir
        char cwd[1000];
        if (!getcwd (cwd, sizeof(cwd))) {
            prError ("Could not get CWD: %s\n", strerror(errno));
            return (false);
        }

        // look in several places to find full path of argv0, confirm with successful fopen
        FILE *fp = NULL;
        if (argv0[0] == '/') {
            // full path already!
            snprintf (full_path, fplen, "%s", argv0);
            fp = fopen (full_path, "r");
        }
        if (!fp) {
            // try relative to cwd
            snprintf (full_path, fplen, "%s/%s", cwd, argv0);
            fp = fopen (full_path, "r");
        }
        if (!fp) {
            // look in each PATH entry, beware "."
            char *onepath, *path = strdup (getenv ("PATH")), *tofree = path;   // don't clobber the real PATH
            while ((onepath = strsep(&path, ":")) != NULL) {
                if (strcmp (onepath, ".") == 0)
                    snprintf (full_path, fplen, "%s/%s", cwd, argv0);
                else
                    snprintf (full_path, fplen, "%s/%s", onepath, argv0);
                fp = fopen (full_path, "r");
                if (fp)
                    break;
            }
            free(tofree);
        }

        // trouble if still can't open or still not a full path
        if (!fp) {
            prError ("Can not open\n%s\n%s\n", full_path, strerror(errno));
            return (false);
        }
        fclose (fp);
        if (full_path[0] != '/') {
            prError ("Not a full path\n%s\n", full_path);
            return (false);
        }

        // follow through any symlinks in place
        char link[1000];
        int ln;
        while ((ln = readlink (full_path, link, sizeof(link))) > 0) {
            if (link[0] == '/') {
                // replace entire path
                snprintf (full_path, fplen, "%.*s", ln, link);
            } else {
                // replace last component
                char *right_slash = strrchr (full_path, '/');
                snprintf (right_slash+1, fplen-(right_slash-full_path+1), "%.*s", ln, link);
            }
        }
        if (ln < 0 && errno != EINVAL) {
            // EINVAL just means path was not a symlink
            prError ("%s\n%s\n", full_path, strerror(errno));
            return (false);
        }

        // now confirm the file can be removed by checking whether we can write the containing dir as euid
        char *right_slash = strrchr (full_path, '/');
        if (full_path[0] != '/' || !right_slash) {
            prError ("%s\nnot a full path\n", full_path);
            return (false);
        }
        *right_slash = '\0';                    // temporarily remove file to get its containing directory
        if (faccessat (0, full_path, W_OK, AT_EACCESS) < 0) {
            prError ("Can not edit\n%s\n%s\n", full_path, strerror(errno));
            return (false);
        }
        *right_slash = '/';                     // restore

        // ok!
        return (true);
}

/* rm tmp and everything it contains.
 * too late to worry about errors.
 */
void ESPhttpUpdate::cleanupDir (const char *tmp)
{
        (void) runCommand (false, 0, 0, 0, "rm -fr %s", tmp);
}

/* url is curl path to new zip file.
 * call *progressCB as make progress.
 */
t_httpUpdate_return ESPhttpUpdate::update(WiFiClient &client, const char *url)
{
        (void) client;

        printf ("OTA: Update with url: %s\n", url);

	// find full path to current program and insure we have permission to remove it
	char our_path[1000];
        if (!findFullPath (our_argv[0], our_path, sizeof(our_path)))
	    return (HTTP_UPDATE_FAILED);                // already set err msg
        printf ("OTA: our full real path: %s\n", our_path);

	// find zip file name portion of url
	const char *zip_file = strrchr (url, '/');
	if (!zip_file || !strstr (url, ".zip")) {
	    prError ("BUG! url\n%s\nhas no zip file??\n", url);
	    return (HTTP_UPDATE_FAILED);
	}
	zip_file += 1;		// skip /
        printf ("OTA: zip name: %s\n", zip_file);

        // homebrew a temp working dir, seems there are issues with all the usual methods
        // N.B. after this always call cleanupDir before returning
        char tmp_dir[50];
        srand (time(NULL));
        snprintf (tmp_dir, sizeof(tmp_dir), "/tmp/HamClock-tmp-%010d.d", rand());
        printf ("OTA: creating %s\n", tmp_dir);
	if (!runCommand (false, 1, 5, 1, "mkdir %s", tmp_dir))
	    return (HTTP_UPDATE_FAILED);

	// download url into tmp_dir naming it zip_file
	if (!runCommand (false, 5, 10, 1, "curl --retry 3 --silent --show-error --output '%s/%s' '%s'",
                                                                tmp_dir, zip_file, url)) {
            cleanupDir (tmp_dir);
	    return (HTTP_UPDATE_FAILED);
        }

	// find new dir unzip will make by assuming it matches base name of zip file.
        // N.B. beware of rc files which have "-V[\d]+\.[\d]+rc[\d]+" after base name.
	const char *zip_ext = strchr (zip_file, '-');          // ESPHamClock-Vxxx.zip
        if (!zip_ext)
            zip_ext = strchr (zip_file, '.');                  // ESPHamClock.zip
	if (!zip_ext) {
	    prError ("BUG! zip file\n%s\nhas no extension?\n", zip_file);
            cleanupDir (tmp_dir);
	    return (HTTP_UPDATE_FAILED);
	}
        int base_len = zip_ext - zip_file;
	char make_dir[64];
	snprintf (make_dir, sizeof(make_dir), "%.*s", base_len, zip_file);
        printf ("OTA: zip will create dir %s\n", make_dir);

	// explode
	if (!runCommand (false, 10, 15, N_UNZIP_LINES, "cd %s && unzip %s", tmp_dir, zip_file)) {
            cleanupDir (tmp_dir);
	    return (HTTP_UPDATE_FAILED);
        }

	// within the new source tree, make the same target we were made with
        printf ("OTA: making %s\n", our_make);
    #ifdef _IS_FREEBSD
	if (!runCommand (false, 15, 95, N_MAKE_LINES, "cd %s/%s && gmake -j 4 %s",tmp_dir,make_dir,our_make)) {
    #else
	if (!runCommand (false, 15, 95, N_MAKE_LINES, "cd %s/%s && make -j 4 %s",tmp_dir,make_dir,our_make)) {
    #endif
            cleanupDir (tmp_dir);
	    return (HTTP_UPDATE_FAILED);
        }

        // get the mode of the currently running file before we remove it
        struct stat sbuf;
        if (stat (our_path, &sbuf) < 0) {
	    prError ("Can not stat\n%s\n%s\n", our_path, strerror(errno));
	    return (HTTP_UPDATE_FAILED);
	}

        // replace current program file with new one, we already think we can remove it if we are euid
	if (!runCommand (true, 95, 98, 1, "rm -f %s && mv %s/%s/%s %s", our_path,
                                                tmp_dir, make_dir, our_make, our_path)) {
            cleanupDir (tmp_dir);
	    return (HTTP_UPDATE_FAILED);
        }

        // set new version of our file to same ownership and mode
        if (chown (our_path, sbuf.st_uid, sbuf.st_gid) < 0) {
	    prError ("Can not change ownership\n%s\n%s\n", our_path,
                                                                                strerror(errno));
	    return (HTTP_UPDATE_FAILED);
	}
        if (chmod (our_path, sbuf.st_mode) < 0) {
	    prError ("Can not change mode of\n%s\n%s\n", our_path, strerror(errno));
	    return (HTTP_UPDATE_FAILED);
	}

	// clean up
        cleanupDir (tmp_dir);

	// close all connections and execute over ourselves -- never returns if works
        printf ("OTA: restarting new version\n");
        ESP.restart();

	// darn! will never get here if successful
        prError ("OTA: restart failed\n");
	return (HTTP_UPDATE_FAILED);
}

void ESPhttpUpdate::onProgress (void (*my_progressCB)(int current, int total))
{
        progressCB = my_progressCB;
}


int ESPhttpUpdate::getLastError(void)
{
	return (1);
}

String ESPhttpUpdate::getLastErrorString(void)
{
        // collect lines
        // N.B. don't bother to reclaim memory, we know this is fatal
        char *err_msg = strdup("");
        int err_msg_len = 0;
        for (int i = 0; i < MAXERRLINES; i++) {
            char *el = err_lines_q[(err_lines_head+i)%MAXERRLINES];
            if (el) {
                int ll = strlen (el);
                err_msg = (char *) realloc (err_msg, err_msg_len + ll + 1);
                strcpy (err_msg+err_msg_len, el);
                err_msg_len += ll;
            }
        }

        // return as String
	return (String(err_msg));
}

void ESPhttpUpdate::prError (const char *fmt, ...)
{
        // format
        char msg[1000];
        va_list ap;
        va_start (ap, fmt);
        vsnprintf (msg, sizeof(msg), fmt, ap);
        va_end (ap);

        // push onto err_lines_q
        if (err_lines_q[err_lines_head])
            free (err_lines_q[err_lines_head]);
        err_lines_q[err_lines_head] = strdup (msg);
        err_lines_head = (err_lines_head + 1) % MAXERRLINES;

        // and log
        printf ("%s", msg);
}
