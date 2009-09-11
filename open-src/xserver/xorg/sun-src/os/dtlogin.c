/* Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons
 * to whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
 * OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL
 * INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder
 * shall not be used in advertising or otherwise to promote the sale, use
 * or other dealings in this Software without prior written authorization
 * of the copyright holder.
 */

#pragma ident "@(#)dtlogin.c	1.20	09/09/11   SMI"

/* Implementation of Display Manager (dtlogin/gdm/xdm/etc.) to X server
 * communication pipe. The Display Manager process will start
 * the X window server at system boot time before any user
 * has logged into the system.  The X server is by default
 * started as the root UID "0".
 *
 * At login time the Xserver local communication pipe is provided
 * by the Xserver for user specific configuration data supplied
 * by the display manager.  It notifies the Xserver it needs to change
 * over to the user's credentials (UID, GID, GID_LIST) and
 * also switch CWD (current working directory) of to match
 * the user's CWD home.
 *
 * When shutting down, the Xserver restores it's original uid/gid as
 * needed by the cleanup/teardown actions in several drivers.
 *
 * For the original definition, see Sun ASARC case 1995/390
 */

#include <X11/Xos.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <X11/X.h>
#include <X11/Xmd.h>
#include "misc.h"
#include <X11/Xpoll.h>
#include "osdep.h"
#include "input.h"
#include "dixstruct.h"
#include "dixfont.h"
#include "opaque.h"
#include <pwd.h>
#include <project.h>
#include <sys/task.h>
#include <ctype.h>
#include "scrnintstr.h"
#include <sys/vt.h>

#define DTLOGIN_PATH "/var/dt/sdtlogin"

#define BUFLEN 1024

int xf86ConsoleFd = -1;

/* in Xserver/os/auth.c */
extern const char *GetAuthFilename(void);

/* Data about the user we need to switch to */
struct dmuser {
    uid_t	uid;
    gid_t	gid;			/* Primary group */
    gid_t	groupids[NGROUPS_UMAX];	/* Supplementary groups */
    int		groupid_cnt;
    char *	homedir;
    projid_t	projid;
};

/* Data passed to block/wakeup handlers */
struct dmdata {
    int		pipeFD;
    char *	pipename;	/* path to pipe */
    char *	buf;		/* contains characters to be processed */
    int		bufsize;	/* size allocated for buf */
    struct dmuser user;		/* target user, to switch to on login */
};


/* Data stored in screen privates */
struct dmScreenPriv {
    CloseScreenProcPtr	CloseScreen;
};
static int dmScreenKeyIndex;
static DevPrivateKey dmScreenKey = &dmScreenKeyIndex;
static struct dmdata *dmHandlerData;
static struct dmuser originalUser; /* user to switch back to in CloseDown */

static Bool DtloginCloseScreen(int i, ScreenPtr pScreen);
static void DtloginBlockHandler(pointer, struct timeval **, pointer);
static void DtloginWakeupHandler(pointer, int, pointer);
static int  dtlogin_create_pipe(int, struct dmdata *);
static void dtlogin_receive_packet(struct dmdata *);
static int  dtlogin_parse_packet(struct dmdata *, char *);
static void dtlogin_process(struct dmuser *user, int user_logged_in);
static void dtlogin_close_pipe(struct dmdata *);

#define DtloginError(fmt, arg)	LogMessageVerb(X_ERROR, 1, fmt ": %s\n", \
						arg, strerror(errno))
#define DtloginInfo(fmt, arg)	LogMessageVerb(X_INFO, 5, fmt, arg)

/*
 * initialize DTLOGIN: create pipe; set handlers.
 * Called from main loop in os/main.c
 */

_X_HIDDEN void
DtloginInit(void)
{
    int displayNumber = 0;
    struct dmdata *dmd;

    if (serverGeneration != 1) return;

    originalUser.uid = geteuid();
    originalUser.gid = getegid();
    getgroups(NGROUPS_UMAX, originalUser.groupids);
    originalUser.homedir = getcwd(NULL, 0);
    originalUser.projid = getprojid();

    if (getuid() != 0)  return;

    dmd = Xcalloc(sizeof(struct dmdata));
    if (dmd == NULL) {
	DtloginError("Failed to allocate %d bytes for display manager pipe",
		     sizeof(struct dmdata));
	return;
    }

    dmd->user.uid = (uid_t) -1;
    dmd->user.gid = (gid_t) -1;
    dmd->user.projid = (projid_t) -1;

    displayNumber = atoi(display); /* Assigned in dix/main.c */

    dmd->pipeFD = dtlogin_create_pipe(displayNumber, dmd);

    if (dmd->pipeFD == -1) {
	xfree(dmd);
	return;
    }

    dmHandlerData = dmd;

    RegisterBlockAndWakeupHandlers (DtloginBlockHandler,
				    DtloginWakeupHandler,
				    (pointer) dmd);
}

/*
 * cleanup dtlogin pipe at exit if still running, reset privs back to
 * root as needed for various cleanup tasks.
 * Called from main loop in os/main.c & CloseScreen wrappers
 */
_X_HIDDEN void
DtloginCloseDown(void)
{
    if (geteuid() != 0) {		/* reset privs back to root */
	if (seteuid(0) < 0) {
	    DtloginError("Error in resetting euid to %d", 0);
	}
	dtlogin_process(&originalUser, 0);
    }

    if (dmHandlerData != NULL) {
	dtlogin_close_pipe(dmHandlerData);
    }
}

static Bool
DtloginCloseScreen (int i, ScreenPtr pScreen)
{
    struct dmScreenPriv *pScreenPriv;

    DtloginCloseDown();

    /* Unwrap CloseScreen and call down to further levels */
    pScreenPriv = (struct dmScreenPriv *)
	dixLookupPrivate(&pScreen->devPrivates, dmScreenKey);

    pScreen->CloseScreen = pScreenPriv->CloseScreen;
    xfree ((pointer) pScreenPriv);

    return (*pScreen->CloseScreen) (i, pScreen);
}

static void
DtloginBlockHandler(
    pointer         data,
    struct timeval  **wt, /* unused */
    pointer         pReadmask)
{
    struct dmdata *dmd = (struct dmdata *) data;
    fd_set *LastSelectMask = (fd_set*)pReadmask;

    FD_SET(dmd->pipeFD, LastSelectMask);
}

static void
DtloginWakeupHandler(
    pointer data,   /* unused */
    int     i,
    pointer pReadmask)
{
    struct dmdata *dmd = (struct dmdata *) data;
    fd_set* LastSelectMask = (fd_set*)pReadmask;

    if (i > 0)
    {
	if (FD_ISSET(dmd->pipeFD, LastSelectMask))
	{
	    FD_CLR(dmd->pipeFD, LastSelectMask);
	    dtlogin_receive_packet(dmd);
	    /* dmd may have been freed in dtlogin_receive_packet, do
	       not use after this point */
	}
    }
}

static int
dtlogin_create_pipe(int port, struct dmdata *dmd)
{
    struct stat statbuf;
    char pipename[128];
    int pipeFD = -1;

    if (stat(DTLOGIN_PATH, &statbuf) == -1) {
	if (mkdir(DTLOGIN_PATH, 0700) == -1) {
	    DtloginError("Cannot create %s directory for display "
			 "manager pipe", DTLOGIN_PATH);
	    return -1;
	}
    } else if (!S_ISDIR(statbuf.st_mode)) {
	DtloginError("Cannot create display manager pipe: "
		     "%s is not a directory", DTLOGIN_PATH);
	return -1;
    }

    snprintf(pipename, sizeof(pipename), "%s/%d", DTLOGIN_PATH, port);

    if (mkfifo(pipename, S_IRUSR | S_IWUSR) < 0)
	return -1;

    if ((pipeFD = open(pipename, O_RDONLY | O_NONBLOCK)) < 0) {
	remove(pipename);
	return -1;
    }

    dmd->pipename = xstrdup(pipename);

    /* To make sure root has rw permissions. */
    (void) fchmod(pipeFD, 0600);

    return pipeFD;
}

static void dtlogin_close_pipe(struct dmdata *dmd)
{
    RemoveBlockAndWakeupHandlers (DtloginBlockHandler,
				  DtloginWakeupHandler, dmd);

    close(dmd->pipeFD);
    remove(dmd->pipename);
    xfree(dmd->pipename);
    xfree(dmd->buf);
    xfree(dmd->user.homedir);
    xfree(dmd);

    if (dmHandlerData == dmd) {
	dmHandlerData = NULL;
    }
}

static void
dtlogin_receive_packet(struct dmdata *dmd)
{
    int bufLen, nbRead;
    char *p, *n;
    int done = 0;

    if (dmd->buf == NULL) {
	dmd->bufsize = BUFLEN;
	dmd->buf = xalloc(dmd->bufsize);
	dmd->buf[0] = '\0';
    }

    /* Read data from pipe and split into tokens, buffering the rest */
    while (!done) {
	bufLen = strlen(dmd->buf);

	/*
	 * Realloc only if buf has filled up and we don't have a record
	 * delimiter yet. Keep track of alloced size.
	 */
	if (bufLen > (dmd->bufsize/2)) {
	    dmd->bufsize += BUFLEN;
	    dmd->buf = xrealloc(dmd->buf, dmd->bufsize);
	}

	nbRead = read(dmd->pipeFD, dmd->buf + bufLen,
		      dmd->bufsize - bufLen - 1);

	if (nbRead == -1) {
	    if (errno == EINTR) {
		continue;
	    } else if (errno == EAGAIN) {
		return; 	/* return to WaitFor, wait for select() */
	    } else {
		dtlogin_close_pipe(dmd);
		return;
	    }
	}

	if (nbRead == 0) { /* End of file */
	    dtlogin_close_pipe(dmd);
	    return;
	}

	bufLen += nbRead;
	dmd->buf[bufLen] = '\0';

	DtloginInfo("Data buffer: %s\n", dmd->buf);

	p = dmd->buf;
	while ((n = strchr(p, ';')) != NULL) { /* Next complete packet */
	    *n = '\0';
	    DtloginInfo("Next packet: %s\n", p);
	    done = dtlogin_parse_packet(dmd, p);
	    if (done) {
		dtlogin_close_pipe(dmd);	/* free's dmd */
		return;
	    }
	    p = n+1;
	}

	/* save the rest for the next iteration */
	if (p < (dmd->buf + bufLen)) {
	    DtloginInfo("Left over: %s\n", p);
	    strcpy(dmd->buf, p);
	}
    }
}

/* Parse data from packet
 *
 * Example Record:
 *      UID="xxx" GID="yyy";
 *      G_LIST_ID="aaa" G_LIST_ID="bbb" G_LIST_ID="ccc";
 *      HOME="/nn/mmm/ooo" EOF="";
 */
static int
dtlogin_parse_packet(struct dmdata *dmd, char *s)
{
    char *k, *v, *p;

    while (s < (dmd->buf + dmd->bufsize)) {
	/* format is key="value" - split into key & value pair */

	for (k = s ; (*k != '\0') && isspace(*k); k++) {
	    /* Skip over whitespace */
	}

	if (*k == '\0') {
	    break;
	}

	p = strchr(k, '=');

	if (p == NULL) {
	    DtloginInfo("Malformed key \'%s\'\n", k);
	    return 0;
	}

	*p = '\0'; /* end of key string */
	p++;

	if (*p != '\"') {
	    DtloginInfo("Bad delimiter at \'%s\'\n", p);
	    return 0;
	}

	v = p + 1; /* start of value string */

	p = strchr(v, '\"');

	if (p == NULL) {
	    DtloginInfo("Missing quote after value \'%s\'\n", v);
	    break;
	}

	*p = '\0'; /* end of value string */

	s = p + 1; /* start of next pair */

	DtloginInfo("Received key \"%s\" =>", k);
	DtloginInfo(" value \"%s\"\n", v);

	/* Found key & value, now process & put into dmd */
	if (strcmp(k, "EOF") == 0) {
	    /* End of transmission, process & close */
	    dtlogin_process(&(dmd->user), 1);
	    return 1;
	}
	else if (strcmp(k, "HOME") == 0) {
	    dmd->user.homedir = xstrdup(v);
	}
	else if ( (strcmp(k, "UID") == 0) || (strcmp(k, "GID") == 0)
		  || (strcmp(k, "G_LIST_ID") == 0) ) {
	    /* Value is numeric, convert to int */
	    int val;

	    errno = 0;
	    val = strtol(v, NULL, 10);

	    if ((val == 0) && (strcmp(v, "0") != 0)) {
		/* strtol couldn't parse the results */
		DtloginInfo("Invalid number \"%s\"\n", v);
		continue;
	    }

	    if ( ((val == LONG_MAX) || (val == LONG_MIN))
		 && (errno == ERANGE) ) {
		/* Value out of range */
		DtloginInfo("Out of range number \"%s\"\n", v);
		continue;
	    }

	    if (strcmp(k, "UID") == 0) {
		dmd->user.uid = val;
	    }
	    else if (strcmp(k, "GID") == 0) {
		dmd->user.gid = val;
	    }
	    else if (strcmp(k, "G_LIST_ID") == 0) {
		if (dmd->user.groupid_cnt < NGROUPS_UMAX) {
		    dmd->user.groupids[dmd->user.groupid_cnt++] = val;
		}
	    }
	}
	else {
	    DtloginInfo("Unrecognized key \"%s\"\n", k);
	}
    }

    return 0;
}


static void
dtlogin_process(struct dmuser *user, int user_logged_in)
{
    struct project proj;
    char proj_buf[PROJECT_BUFSZ];
    struct passwd *ppasswd;
    const char *auth_file = NULL;

    auth_file = GetAuthFilename();

    if (auth_file) {
	if (chown(auth_file, user->uid, user->gid) < 0)
	    DtloginError("Error in changing owner to %d", user->uid);
    }

    /* This gid dance is necessary in order to make sure
       our "saved-set-gid" is 0 so that we can regain gid
       0 when necessary for priocntl & power management.
       The first step sets rgid to the user's gid and
       makes the egid & saved-gid be 0.  The second then
       sets the egid to the users gid, but leaves the
       saved-gid as 0.  */

    if (user->gid != (gid_t) -1) {
	DtloginInfo("Setting gid to %d\n", user->gid);

	if (setregid(user->gid, 0) < 0)
	    DtloginError("Error in setting regid to %d\n", user->gid);

	if (setegid(user->gid) < 0)
	    DtloginError("Error in setting egid to %d\n", user->gid);
    }

    if (user->groupid_cnt >= 0) {
	if (setgroups(user->groupid_cnt, user->groupids) < 0)
	    DtloginError("Error in setting supplemental (%d) groups",
			 user->groupid_cnt);
    }


    /*
     * BUG: 4462531: Set project ID for Xserver
     *	             Get user name and default project.
     *		     Set before the uid value is set.
     */
    if (user->projid != (uid_t) -1) {
	if (settaskid(user->projid, TASK_NORMAL) == (taskid_t) -1) {
	    DtloginError("Error in setting project id to %d", user->projid);
	}
    } else if (user->uid != (uid_t) -1) {
	ppasswd = getpwuid(user->uid);

	if (ppasswd == NULL) {
	    DtloginError("Error in getting user name for %d", user->uid);
	} else {
	    if (getdefaultproj(ppasswd->pw_name, &proj,
			       (void *)&proj_buf, PROJECT_BUFSZ) == NULL) {
		DtloginError("Error in getting project id for %s",
			     ppasswd->pw_name);
	    } else {
		DtloginInfo("Setting project to %s\n", proj.pj_name);

		if (setproject(proj.pj_name, ppasswd->pw_name,
			       TASK_NORMAL) == -1) {
		    DtloginError("Error in setting project to %s",
				 proj.pj_name);
		}
	    }
	}
    }

    if (user->uid != (uid_t) -1) {
	DtloginInfo("Setting uid to %d\n", user->uid);

	if (setreuid(user->uid, -1) < 0)
	    DtloginError("Error in setting ruid to %d", user->uid);

	if (setreuid(-1, user->uid) < 0)
	    DtloginError("Error in setting euid to %d", user->uid);

	/* Wrap closeScreen to allow resetting uid on closedown */
	if ((user->uid != 0) && (user != &originalUser)) {
	    int i;

	    for (i = 0; i < screenInfo.numScreens; i++)
	    {
		ScreenPtr pScreen = screenInfo.screens[i];
		struct dmScreenPriv *pScreenPriv;

		pScreenPriv = (struct dmScreenPriv *)
		    Xcalloc(sizeof(struct dmScreenPriv));
		dixSetPrivate(&pScreen->devPrivates, dmScreenKey, pScreenPriv);

		if (pScreenPriv != NULL) {
		    pScreenPriv->CloseScreen = pScreen->CloseScreen;
		    pScreen->CloseScreen = DtloginCloseScreen;
		}
	    }
	}
    }

    if (user->homedir != NULL) {
	char *env_str = Xprintf("HOME=%s", user->homedir);

	if (env_str == NULL) {
	    DtloginError("Not enough memory to setenv HOME=%s", user->homedir);
	} else {
	    DtloginInfo("Setting %s\n",env_str);

	    if (putenv(env_str) < 0)
		DtloginError("Failed to setenv %s", env_str);
	}

	if (chdir(user->homedir) < 0)
	    DtloginError("Error in changing working directory to %s",
			 user->homedir);
    }

    /* Inform the kernel whether a user has logged in on this VT device */
    if (xf86ConsoleFd != -1)
	ioctl(xf86ConsoleFd, VT_SETDISPLOGIN, user_logged_in);
}
