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

#pragma ident   "@(#)tsolpolicy.c 1.26     09/04/02 SMI"

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#define		NEED_REPLIES
#define		NEED_EVENTS
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "auditwrite.h"
#include <bsm/audit_kevents.h>
#include <bsm/audit_uevents.h>
#include <X11/Xproto.h>
#include "dix.h"
#include "misc.h"
#include "scrnintstr.h"
#include "os.h"
#include "regionstr.h"
#include "validate.h"
#include "windowstr.h"
#include "propertyst.h"
#include "input.h"
#include "inputstr.h"
#include "resource.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "dixstruct.h"
#include "selection.h"
#include "gcstruct.h"
#include "servermd.h"
#include <syslog.h>
#include "extnsionst.h"
#include "registry.h"
#ifdef PANORAMIX
#include "../Xext/panoramiXsrv.h"
#endif
#include "tsol.h"
#include "tsolinfo.h"
#include "tsolpolicy.h"

static priv_set_t *pset_win_mac_read = NULL;
static priv_set_t *pset_win_mac_write = NULL;
static priv_set_t *pset_win_dac_read = NULL;
static priv_set_t *pset_win_dac_write = NULL;
static priv_set_t *pset_win_config = NULL;
static priv_set_t *pset_win_devices = NULL;
static priv_set_t *pset_win_fontpath = NULL;
static priv_set_t *pset_win_colormap = NULL;
static priv_set_t *pset_win_upgrade_sl = NULL;
static priv_set_t *pset_win_downgrade_sl = NULL;
static priv_set_t *pset_win_selection = NULL;

#define SAMECLIENT(client, xid) ((client)->index == CLIENT_ID(xid))

static int access_xid(xresource_t res, xmethod_t method, void *resource,
		      void *subject, xpolicy_t policy_flags, void *misc,
		      RESTYPE res_type, priv_set_t *which_priv);

static int check_priv(xresource_t res, xmethod_t method, void *resource,
		      void *subject, xpolicy_t policy_flags, void *misc,
		      priv_set_t *priv);

/* Unless NO_TSOL_DEBUG_MESSAGES is defined, admins will be able to enable
   debugging messages at runtime via Xorg -logverbose */
#ifndef NO_TSOL_DEBUG_MESSAGES
static char *xsltos(bslabel_t *sl);

static void XTsolErr(const char *err_type, CARD8 *protocol,
	     bslabel_t *osl, uid_t ouid, pid_t opid, const char *opname,
	     bslabel_t *ssl, uid_t suid, pid_t spid, const char *spname,
	     const char *method, int isstring, const void *xid);

#define XTSOLERR_GEN(err_type, protocol, o, s, method, xid, isstring) \
                 (void) XTsolErr(err_type, (CARD8 *) (protocol), \
				 (o)->sl, (o)->uid, (o)->pid, NULL, \
				 (s)->sl, (s)->uid, (s)->pid, (s)->pname, \
				 method, isstring, xid)
#else  /* NO_TSOL_DEBUG_MESSAGES */
#define XTSOLERR_GEN(err_type, protocol, o, s, method, xid, isstring)  /**/
#endif /* NO_TSOL_DEBUG_MESSAGES */

#define XTSOLERR(err_type, protocol, o, s, method, xid) \
	XTSOLERR_GEN(err_type, protocol, o, s, method, &(xid), 0);

#define SXTSOLERR(err_type, protocol, o, s, method, xid) \
	XTSOLERR_GEN(err_type, protocol, o, s, method, xid, 1);


int object_float(TsolInfoPtr, WindowPtr);

static void
set_audit_flags(TsolInfoPtr tsolinfo)
{
    if (tsolinfo->flags & TSOL_AUDITEVENT)
        tsolinfo->flags &= ~TSOL_AUDITEVENT;
    if (!(tsolinfo->flags & TSOL_DOXAUDIT))
        tsolinfo->flags |= TSOL_DOXAUDIT;

}

static void
unset_audit_flags(TsolInfoPtr tsolinfo)
{
    if (tsolinfo->flags & TSOL_AUDITEVENT)
        tsolinfo->flags &= ~TSOL_AUDITEVENT;
    if (tsolinfo->flags & TSOL_DOXAUDIT)
        tsolinfo->flags &= ~TSOL_DOXAUDIT;

}

/*
 * return 1 for success and 0 for failure
 * this routine contains the privd debugging for the system
 * as well as auditing the success or failure of use of priv.
 * Priv debugging will be done later TBD
 */

static int
xpriv_policy(priv_set_t *set, priv_set_t *priv, xresource_t res,
			 xmethod_t method, void *subject, Bool do_audit)
{
	static int logopened = FALSE;
	int	status = 0;
	int audit_status = 0;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (priv_issubset(priv, set))
	{
		status = 1;
		audit_status = 1;
	}
	else
	{
		audit_status = 0;
		if (!logopened)
		{
			/* LOG_USER doesn't work */
			openlog("xsun", 0, LOG_LOCAL0);
			logopened = TRUE;
		}
		/* if priv debugging is on, allow this priv to succeed */
		if (tsolinfo->priv_debug)
		{
#ifndef NO_TSOL_DEBUG_MESSAGES
		    LogMessageVerb(X_INFO, TSOL_MSG_ACCESS_TRACE,
				   TSOL_LOG_PREFIX"%s: Allowed priv %ld\n",
				   tsolinfo->pname, (long) priv);
#endif /* !NO_TSOL_DEBUG_MESSAGES */
			syslog(LOG_DEBUG|LOG_LOCAL0,
                   "DEBUG: %s pid %ld lacking privilege %d to %d %d",
                   "xclient", tsolinfo->pid, priv, method, res);
			status = 1;
            audit_status = 1;
		}
	}
	if (do_audit)
		auditwrite(AW_USEOFPRIV, audit_status, priv, AW_APPEND, AW_END);
	return (status);
}	/* xpriv_policy */


/*
 * The read/modify window policy are for window attributes & not
 * for window contents. read/modify pixel handle the contents policy
 */
/*
 * read_window
 */
static int
read_window(xresource_t res, xmethod_t method, void *resource,
			void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	ClientPtr ownerclient;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolInfoPtr tsolownerinfo;	/*client who owns the window */
	TsolResPtr tsolres = TsolWindowPriv(pWin);

	ownerclient = clients[CLIENT_ID(pWin->drawable.id)];
	tsolownerinfo = GetClientTsolInfo(ownerclient);
	/*
	 * Anyone can read RootWindow attributes
	 */
	if (WindowIsRoot(pWin))
	{
		return (PASSED);
	}
	/* optimization based on client id */
	if (SAMECLIENT(client, pWin->drawable.id))
	{
		return PASSED;
	}
	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!(blequal(tsolinfo->sl, tsolres->sl)))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_read,
							 res, method, client, do_audit) ||
				(tsolownerinfo && HasWinSelection(tsolownerinfo)))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, tsolres, tsolinfo,
					 "read window", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		/* uid == DEF_UID means public window, shared read */
		if (!(XTSOLTrusted(pWin) ||
			tsolres->uid == DEF_UID ||
			tsolinfo->uid == tsolres->uid))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_read,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, tsolres, tsolinfo,
					 "read window", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
    {
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
    }
	return (ret_stat);
}	/* read_window */

/*
 * modify_window
 */
static int
modify_window(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres = TsolWindowPriv(pWin);

	/* optimization based on client id */
	if (SAMECLIENT(client, pWin->drawable.id))
	{
		return PASSED;
	}
	/*
	 * Trusted Path Windows require Trusted Path attrib
	 */
	if (XTSOLTrusted(pWin) && !HasTrustedPath(tsolinfo))
	{
		XTSOLERR("tp",  misc, tsolres, tsolinfo,
			 "modify window", pWin->drawable.id);
		ret_stat = err_code;
	}
	/*
	 * MAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac",  misc, tsolres, tsolinfo,
					 "modify window", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac",  misc, tsolres, tsolinfo,
					 "modify window", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}

	if (do_audit)
        {
            set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
        }
	return (ret_stat);
}	/* modify_window */

/*
 * create_window
 */
static int
create_window(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;	/* parent window */
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr  tsolres = TsolWindowPriv(pWin);

	/*
	 * Anyone can create a child of root window
	 */
	if (WindowIsRoot(pWin))
	{
		return (PASSED);
	}
	/*
	 * Trusted Path Windows required Trusted Path attrib
	 */
	if (XTSOLTrusted(pWin))
	{
		if (!HasTrustedPath(tsolinfo))
		{
		/*	XTSOLERR("tp",  misc, tsolres, tsolinfo,
				"create window", pWin->drawable.id); */
			return (err_code);
		}
	}
	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
			if (!SAMECLIENT(client, pWin->parent->drawable.id) &&
                (tsolinfo->flags & TSOL_AUDITEVENT))
            {
				do_audit = TRUE;
            }
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac",  misc, tsolres, tsolinfo,
					 "create window", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid)
		{
			if (!SAMECLIENT(client, pWin->parent->drawable.id) &&
                (tsolinfo->flags & TSOL_AUDITEVENT))
            {
				do_audit = TRUE;
            }
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac",  misc, tsolres, tsolinfo,
					 "create window", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
    {
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_SLABEL, tsolres->sl,
				   AW_APPEND, AW_END);
    }

	return (ret_stat);
}	/* create_window */

/*
 * destroy_window
 */
static int
destroy_window(xresource_t res, xmethod_t method, void *resource,
			   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr  tsolres = TsolWindowPriv(pWin);

	/*
	 * Trusted Path Windows required Trusted Path attrib
	 */
	if (XTSOLTrusted(pWin))
	{
		if (!HasTrustedPath(tsolinfo))
		{
			XTSOLERR("tp",  misc, tsolres, tsolinfo,
				 "destroy window", pWin->drawable.id);
			return (err_code);
		}
	}
	/*
	 * MAC Check
	 */
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac",  misc, tsolres, tsolinfo,
					 "destroy window", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid ||
			!same_client(client, pWin->drawable.id))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac",  misc, tsolres, tsolinfo,
					 "destroy window", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
    {
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
    }
	return (ret_stat);
}	/* destroy_window */

/*
 * read_pixel: used for reading contents of drawable like GetImage
 */
static int
read_pixel(xresource_t res, xmethod_t method, void *resource,
		   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	rc;
	int	err_code = BadDrawable;
	int obj_code = 0;
	XID obj_id = (XID)NULL;
	DrawablePtr pDraw = resource;
	ClientPtr client = subject;
	PixmapPtr pMap = NullPixmap;
	WindowPtr pWin = NullWindow;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres;

	/*
	 * Public/default objects check here
	 */

	if (pDraw->type == DRAWABLE_WINDOW)
	{
		rc = dixLookupWindow(&pWin, pDraw->id, client, DixReadAccess);
		if (rc != Success)
		    return rc;

		if (pWin == NULL)
			return (PASSED); /* server will handle bad params */
		tsolres = TsolWindowPriv(pWin);
		obj_code = AW_XWINDOW;
		obj_id = pWin->drawable.id;
	}
	else if (pDraw->type == DRAWABLE_PIXMAP)
	{
		rc = dixLookupResource((pointer *)&pMap, pDraw->id, RT_PIXMAP,
				client, DixReadAccess);
		if (rc != Success)
		    return rc;

		if (pMap == NULL)
			return (PASSED); /* server will handle bad params */
		tsolres = TsolPixmapPriv(pMap);
		obj_code = AW_XPIXMAP;
		obj_id = pMap->drawable.id;
	}
	else
		return (PASSED); /* UNDRAWABLE_WINDOW */

	/* based on client id bypass MAC/DAC */
	if (!SAMECLIENT(client, pDraw->id))
	{
	    /*
	     * Client must have Trusted Path to access root window
	     * in multilevel desktop.
	     */
	    if (tsolMultiLevel && DrawableIsRoot(pDraw) &&
			!HasTrustedPath(tsolinfo))
		return (err_code);
		/*
	 	 * MAC Check
		 */
		if (policy_flags & TSOL_MAC)
		{
			if (!bldominates(tsolinfo->sl, tsolres->sl))
			{
				if (!(tsolinfo->flags & MAC_READ_AUDITED) &&
				    (tsolinfo->flags & TSOL_AUDITEVENT))
				{
					do_audit = TRUE;
					tsolinfo->flags |= MAC_READ_AUDITED;
				}
				/* PRIV override? */
				if (xpriv_policy(tsolinfo->privs, pset_win_mac_read,
								 res, method, client, do_audit))
				{
					ret_stat = PASSED;
				}
				else
				{
					XTSOLERR("mac",  misc,
						 tsolres, tsolinfo,
						 "read pixel", pDraw->id);
					ret_stat = err_code;
				}
			}
		}
		/*
	 	 * DAC Check
	 	 */
		if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
		{
			if (tsolinfo->uid != tsolres->uid)
			{
				if (!(tsolinfo->flags & DAC_READ_AUDITED) &&
				    (tsolinfo->flags & TSOL_AUDITEVENT))
				{
					do_audit = TRUE;
					tsolinfo->flags |= DAC_READ_AUDITED;
				}
				if (xpriv_policy(tsolinfo->privs, pset_win_dac_read,
						 res, method, client, do_audit))
				{
					ret_stat = PASSED;
				}
				else
				{
					XTSOLERR("mac",  misc,
						 tsolres, tsolinfo,
						 "read pixel", pDraw->id);
					ret_stat = err_code;
				}
			}
		}
	}  /* end if !SAMECLIENT */

	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(obj_code, obj_id, tsolres->uid, AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* read_pixel */

/*
 * modify_pixel
 *
 * NOTE: For Panorama, the real resource id is extracted from the
 * Panorama resource and policy check is done on the real resource.
 */
static int
modify_pixel(xresource_t res, xmethod_t method, void *resource,
			 void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	rc;
	int	err_code = BadDrawable;
	int obj_code = 0;
	XID obj_id = (XID)NULL;
	DrawablePtr pDraw = resource;
	ClientPtr client = subject;
	PixmapPtr pMap = NullPixmap;
	WindowPtr pWin = NullWindow;
	TsolInfoPtr tsolinfo;
	TsolResPtr tsolres;
#if defined(PANORAMIX)
	PanoramiXRes *panres = NULL;
#endif

	/*
	 * Trusted Path Windows required Trusted Path attrib
	 */
	tsolinfo = GetClientTsolInfo(client);

	if (pDraw->type == DRAWABLE_WINDOW)
	{
#if defined(PANORAMIX)
		if (!noPanoramiXExtension)
		{
		    panres = (PanoramiXRes *)LookupIDByType(pDraw->id, XRT_WINDOW);
		    if (panres) {
		        rc = dixLookupWindow(&pWin, panres->info[0].id, 
				client, DixWriteAccess);

		        if (rc != Success)
			    return rc;
		    }
		} else
		    rc = dixLookupWindow(&pWin, pDraw->id, 
			client, DixWriteAccess);

		    if (rc != Success)
			return rc;
#endif
		if (pWin == NULL)
			return (PASSED);
		tsolres = TsolWindowPriv(pWin);
		obj_code = AW_XWINDOW;
		obj_id = pWin->drawable.id;
	}
	else if (pDraw->type == DRAWABLE_PIXMAP)
	{
#if defined(PANORAMIX)
	    if (!noPanoramiXExtension)
	    {
		panres = (PanoramiXRes *)LookupIDByType(pDraw->id, XRT_PIXMAP);
		if (panres)
			pMap = (PixmapPtr)LookupIDByType(panres->info[0].id, RT_PIXMAP);
	    } else
#endif
		rc = dixLookupResource((pointer *)&pMap, pDraw->id, RT_PIXMAP,
				client, DixWriteAccess);
		if (rc != Success)
		    return rc;

		if (pMap == NULL)
			return (PASSED);
		tsolres = TsolPixmapPriv(pMap);
		obj_code = AW_XPIXMAP;
		obj_id = pMap->drawable.id;
	}
	else
		return (PASSED); /* UNDRAWABLE_WINDOW */

	/* optimization based on client id */
	if (!SAMECLIENT(client, pDraw->id))
	{
	    /*
	     * Trusted Path Windows require Trusted Path attrib
	     */
	    if ((pDraw->type == DRAWABLE_WINDOW) &&
		XTSOLTrusted(pWin) &&
		!HasTrustedPath(tsolinfo))
	    {
		    XTSOLERR("tp",  misc, tsolres, tsolinfo,
			     "modify pixel", pWin->drawable.id);
		    return (err_code);
	    }
	    /*
	     * You need  win_config priv to write to root window
	     */
	    if (!priv_win_config && (pWin && WindowIsRoot(pWin)))
	    {
		    if (!(tsolinfo->flags & CONFIG_AUDITED) &&
			(tsolinfo->flags & TSOL_AUDITEVENT))
		    {
			    do_audit = TRUE;
			    tsolinfo->flags |= CONFIG_AUDITED;
		    }
		    if (xpriv_policy(tsolinfo->privs, pset_win_config,
				     res, method, client, do_audit))
		    {
			    ret_stat = PASSED;
		    }
		    else
		    {
			    XTSOLERR("mac",  misc, tsolres, tsolinfo,
				     "modify pixel", pDraw->id);
			    ret_stat = err_code;
		    }
	    }
	    /*
	     * MAC Check
	     */
	    if ((ret_stat == PASSED) && policy_flags & TSOL_MAC)
	    {
		    if (!blequal(tsolinfo->sl, tsolres->sl))
		    {
			    if (!(tsolinfo->flags & MAC_WRITE_AUDITED) &&
				(tsolinfo->flags & TSOL_AUDITEVENT))
			    {
				    do_audit = TRUE;
				    tsolinfo->flags |= MAC_WRITE_AUDITED;
			    }
			    if (xpriv_policy(tsolinfo->privs,
					     pset_win_mac_write,
					     res, method, client, do_audit))
			    {
				    ret_stat = PASSED;
			    }
			    else
			    {
				    XTSOLERR("mac",  misc,
					     tsolres, tsolinfo,
					     "modify pixel", pDraw->id);
				    ret_stat = err_code;
			    }
		    }
	    }
	    /*
	     * DAC Check
	     */
	    if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	    {
		    if (tsolinfo->uid != tsolres->uid)
		    {
			    if (!(tsolinfo->flags & DAC_WRITE_AUDITED) &&
				(tsolinfo->flags & TSOL_AUDITEVENT))
			    {
				    do_audit = TRUE;
				    tsolinfo->flags |= DAC_WRITE_AUDITED;
			    }

			    if (xpriv_policy(tsolinfo->privs,
					     pset_win_dac_write,
					     res, method, client, do_audit))
			    {
				     ret_stat = PASSED;
			    }
			    else
			    {
				    XTSOLERR("dac",  misc,
					     tsolres, tsolinfo,
					     "modify pixel", pDraw->id);
				    ret_stat = err_code;
			    }
		    }
	    }
	}  /* end if SAMECLIENT */

	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(obj_code, obj_id, tsolres->uid, AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_pixel */

/*
 * read_pixmap
 */
static int
read_pixmap(xresource_t res, xmethod_t method, void *resource,
			void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadPixmap;
	PixmapPtr pMap = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr  tsolres = TsolPixmapPriv(pMap);

	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!bldominates(tsolinfo->sl, tsolres->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_read,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac",  misc, tsolres, tsolinfo,
					 "read pixmap", pMap->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_read,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac",  misc, tsolres, tsolinfo,
					 "read pixmap", pMap->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XPIXMAP, pMap->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* read_pixmap */

/*
 * modify_pixmap
 */
static int
modify_pixmap(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadPixmap;
	PixmapPtr pMap = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr  tsolres = TsolPixmapPriv(pMap);

	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac",  misc, tsolres, tsolinfo,
					 "modify pixmap", pMap->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac",  misc, tsolres, tsolinfo,
					 "modify pixmap", pMap->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XPIXMAP, pMap->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_pixmap */

/*
 * destroy_pixmap
 */
static int
destroy_pixmap(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadPixmap;
	PixmapPtr pMap = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr  tsolres = TsolPixmapPriv(pMap);

	/*
	 * MAC Check
	 */
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac",  misc, tsolres, tsolinfo,
					 "destroy pixmap", pMap->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac",  misc, tsolres, tsolinfo,
					 "destroy pixmap", pMap->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XPIXMAP, pMap->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* destroy_pixmap */

/*
 * read_client
 */
static int
read_client(xresource_t res, xmethod_t method, void *resource,
			void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	ClientPtr res_client;
	int	err_code = BadAccess;
	Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr res_tsolinfo;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	XID	targetid = * (XID *) resource;
	
	res_client = clients[CLIENT_ID(targetid)];
	if (res_client == NULL)
	{
		return (BadValue);
	}
	res_tsolinfo = GetClientTsolInfo(res_client);

	/* TrustedPath is needed to get serverClient attributes */
	if (res_client == serverClient || res_tsolinfo == NULL)
	{
		if (client == serverClient || HasTrustedPath(tsolinfo))
			return (PASSED);
		else
			return (BadValue);
	}

	/*
	 * MAC Check
	 */
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, res_tsolinfo->sl))
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_read,
					 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac",  misc, res_tsolinfo,
					 tsolinfo, "read client", targetid);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != res_tsolinfo->uid)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_read,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac",  misc, res_tsolinfo,
					 tsolinfo, "read client", targetid);
				ret_stat = ret_stat;
			}
		}
	}
	/*
	 * Trusted Path Windows required Trusted Path attrib
	 */
	if ((ret_stat == PASSED) && HasTrustedPath(res_tsolinfo))
	{
		if (!HasTrustedPath(tsolinfo))
		{
			XTSOLERR("tp", misc, res_tsolinfo,
				 tsolinfo, "read client", targetid);
			ret_stat = err_code;
		}
	}
    if (do_audit)
    {
        set_audit_flags(tsolinfo);
        auditwrite(AW_XCLIENT, res_client->index, AW_APPEND, AW_END);
    }
	return (ret_stat);
}	/* read client */

/*
 * modify_client
 * Special win_config priv used for ChangeSaveSet, SetCloseDownMode
 */
static int
modify_client(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	int	err_code = BadValue;
    Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (priv_win_config)
		return (PASSED);

    if (tsolinfo->flags & TSOL_AUDITEVENT)
        do_audit = TRUE;
	/*
	 * Needs win_config priv
	 */
	if (xpriv_policy(tsolinfo->privs, pset_win_config,
					 res, method, client, do_audit))
	{
		ret_stat = PASSED;
	}
	else
	{
		ret_stat = err_code;
	}
	if (do_audit)
	{
		set_audit_flags(tsolinfo);
		auditwrite(AW_XCLIENT, client->index, AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_client */

/*
 * destroy_client
 */
static int
destroy_client(xresource_t res, xmethod_t method, void *resource,
			   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	ClientPtr res_client;
	int	err_code = BadValue;
	Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr res_tsolinfo;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	XID	targetid = * (XID *) resource;
	
	res_client = clients[CLIENT_ID(targetid)];
	if (res_client == NULL)
	{
		return (BadValue);
	}

	res_tsolinfo = GetClientTsolInfo(res_client);

	/* Server a special client */
	if (res_client == serverClient || res_tsolinfo == NULL)
	{
		if (client != serverClient)
			return (BadValue);
		else
			return (PASSED); /* internal request */
	}


	/*
	 * MAC Check
	 */
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, res_tsolinfo->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, res_tsolinfo,
					 tsolinfo, "destroy client", targetid);
				ret_stat = ret_stat;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != res_tsolinfo->uid)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, res_tsolinfo,
					 tsolinfo, "destroy client", targetid);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * Trusted Path Windows required Trusted Path attrib
	 */
	if ((ret_stat == PASSED) && HasTrustedPath(res_tsolinfo))
	{
		if (!HasTrustedPath(tsolinfo))
		{
			XTSOLERR("tp", misc, res_tsolinfo,
				 tsolinfo, "destroy client", targetid);
			ret_stat = err_code;
		}
	}
    if (do_audit)
    {
        set_audit_flags(tsolinfo);
        auditwrite(AW_XCLIENT, res_client->index, AW_APPEND, AW_END);
    }
	return (ret_stat);
}	/* destroy_client */

/*
 * read_gc
 */
static int
read_gc(xresource_t res, xmethod_t method, void *resource,
		void *subject, xpolicy_t policy_flags, void *misc)
{
	return (access_xid(res, method, resource, subject, policy_flags,
					   misc, RT_GC, pset_win_dac_read));
}

/*
 * modify_gc
 */
static int
modify_gc(xresource_t res, xmethod_t method, void *resource,
		  void *subject, xpolicy_t policy_flags, void *misc)
{
    return (access_xid(res, method, resource, subject, policy_flags,
                       misc, RT_GC, pset_win_dac_write));
}

/*
 * read_font
 */
static int
read_font(xresource_t res, xmethod_t method, void *resource,
		  void *subject, xpolicy_t policy_flags, void *misc)
{
	return (access_xid(res, method, resource, subject, policy_flags,
			misc, RT_FONT, pset_win_dac_read));
}

/*
 * modify_font
 */
static int
modify_font(xresource_t res, xmethod_t method, void *resource,
			void *subject, xpolicy_t policy_flags, void *misc)
{
	return (access_xid(res, method, resource, subject, policy_flags,
			   misc, RT_FONT, pset_win_dac_write));
}

/*
 * modify_cursor
 */
static int
modify_cursor(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	return (access_xid(res, method, resource, subject, policy_flags,
			   misc, RT_CURSOR, pset_win_dac_write));
}

/*
 * access_ccell: access policy for color cells.
 */
static int
access_ccell(xresource_t res, xmethod_t method, void *resource, void *subject,
             xpolicy_t policy_flags, void *misc)
{
#if TBD
    int ret_stat = PASSED;
    Bool do_audit = FALSE;
    priv_t priv;
    XID cmap_id = * (XID *) misc;
    EntrySecAttrPtr  pentp = (EntrySecAttrPtr)resource;
    ClientPtr client = (ClientPtr)subject;
    TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

    /*
     * MAC check
     */
    if (policy_flags & TSOL_MAC)
    {
        if (!blequal(tsolinfo->sl, pentp->sl))
        {
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
            priv =
                (method == TSOL_READ) ? pset_win_mac_read : pset_win_mac_write;
            /*
             * any colorcell owned by root is readable by all
             */
            if ((priv == pset_win_mac_read) && (pentp->uid == 0))
                ret_stat = PASSED;
            else if (xpriv_policy(tsolinfo->privs, priv,
                                  res, method, client, do_audit))
            {
                ret_stat = PASSED;
            }
            else
            {
		XTSOLERR("clientid mac", (int)NULL, tsolinfo, tsolinfo,
			 "access ccell", cmap_id);
		ret_stat = BadAccess;
            }
        }
    }
    /*
     * DAC check
     */
    if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
    {
        if (tsolinfo->uid != pentp->uid)
        {
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
            priv = (method == TSOL_READ) ?
                pset_win_dac_read : pset_win_dac_write;
            /*
             * any colorcell owned by root is readable by all
             */
            if ((priv == pset_win_dac_read) && (pentp->uid == 0))
                ret_stat = PASSED;
            else if (xpriv_policy(tsolinfo->privs, priv,
                                  res, method, client, do_audit))
            {
                ret_stat = PASSED;
            }
            else
            {
		XTSOLERR("clientid dac", (int)NULL, tsolinfo,
                         tsolinfo, "access ccell", cmap_id);
                ret_stat = BadAccess;
            }
        }
    }
    if (do_audit)
    {
        set_audit_flags(tsolinfo);
        auditwrite(AW_XCOLORMAP, (u_long)cmap_id, tsolinfo->uid,
                   AW_APPEND, AW_END);
    }

    return (ret_stat);
#endif
   return (PASSED);
}

/*
 * read_ccell
 */
static int
read_ccell(xresource_t res, xmethod_t method, void *resource,
           void *subject, xpolicy_t policy_flags, void *misc)
{
    if (priv_win_colormap)
        return (PASSED);
    else
        return (access_ccell(res, method, resource, subject,
                             policy_flags, misc));
}

/*
 * modify_ccell
 */
static int
modify_ccell(xresource_t res, xmethod_t method, void *resource,
             void *subject, xpolicy_t policy_flags, void *misc)
{
    if (priv_win_colormap)
        return (PASSED);
    else
        return (access_ccell(res, method, resource, subject,
                             policy_flags, misc));
}

/*
 * destroy_ccell
 */
static int
destroy_ccell(xresource_t res, xmethod_t method, void *resource,
             void *subject, xpolicy_t policy_flags, void *misc)
{
#ifdef TBD
    EntrySecAttrPtr  pentp = (EntrySecAttrPtr)resource;

    if (priv_win_colormap)
        return (PASSED);
    else if ( pentp->sl == NULL) /* The cell is allocated by server */
        return (PASSED);
    else
        return (access_ccell(res, method, resource, subject,
                             policy_flags, misc));
#endif
   return (PASSED);
}

/*
 * read_cmap
 */
static int
read_cmap(xresource_t res, xmethod_t method, void *resource,
		  void *subject, xpolicy_t policy_flags, void *misc)
{
	ColormapPtr	pcmp = (ColormapPtr ) resource;

	/* handle default colormap */
	if (pcmp->flags & IsDefault)
		return (PASSED);

	return (access_xid(res, method, &(pcmp->mid),
			   subject, policy_flags, misc,
			   RT_COLORMAP, pset_win_dac_read));
}

/*
 * modify_cmap: resource passed is ColormapPtr & not an XID
 */
static int
modify_cmap(xresource_t res, xmethod_t method, void *resource,
			void *subject, xpolicy_t policy_flags, void *misc)
{
	ColormapPtr	pcmp = (ColormapPtr ) resource;

	/* modify default colormap ok */
	if (pcmp->flags & IsDefault)
		return (PASSED);

	return (access_xid(res, method, &(pcmp->mid),
			   subject, policy_flags, misc,
			   RT_COLORMAP, pset_win_dac_write));
}

/*
 * install_cmap: both install/uninstall
 */
static int
install_cmap(xresource_t res, xmethod_t method, void *resource,
			 void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	ColormapPtr	pcmp = (ColormapPtr ) resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	int	err_code = BadColor;

	/* handle default colormap */
	if (pcmp->flags & IsDefault)
		return (PASSED);

	if (priv_win_colormap)
		return (PASSED);

	if (tsolinfo->flags & TSOL_AUDITEVENT)
		do_audit = TRUE;

	/*
	 * check only win_colormap priv
	 */
	if (xpriv_policy(tsolinfo->privs, pset_win_colormap,
					 res, method, client, do_audit))
	{
		ret_stat = PASSED;
	}
	else
	{
		XTSOLERR("install_cmap", misc, tsolinfo,
			 tsolinfo, "install_cmap", pcmp->mid);
		ret_stat = err_code;
	}
    if (tsolinfo->flags & TSOL_AUDITEVENT)
    {
        set_audit_flags(tsolinfo);
        auditwrite(AW_XCOLORMAP, pcmp->mid, tsolinfo->uid, AW_APPEND, AW_END);
    }
	return (ret_stat);
}

/*
 * access_xid: access policy for XIDs
 */
static int
access_xid(xresource_t res, xmethod_t method, void *resource,
		   void *subject, xpolicy_t policy_flags, void *misc,
		   RESTYPE res_type, priv_set_t *which_priv)
{
	int ret_stat = PASSED;
	int object_code = 0;
	int	err_code; /* depends on type of XID */
	Bool do_audit = FALSE;
	XID object = * (XID *) resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = (TsolInfoPtr)NULL;

	if (res_type == RT_NONE)
		res_type = RES_TYPE(object);
	/*
	 * assign appropriate error code
	 */
	switch (res_type) {
        case RT_PIXMAP:
            err_code = BadPixmap;
            object_code = AW_XPIXMAP;
            break;
		case RT_FONT:
			err_code = BadFont;
			object_code = AW_XFONT;
			break;
		case RT_GC:
			err_code = BadGC;
			object_code = AW_XGC;
			break;
		case RT_CURSOR:
			err_code = BadCursor;
			object_code = AW_XCURSOR;
			break;
		case RT_COLORMAP:
			err_code = BadColor;
			object_code = AW_XCOLORMAP;
			break;
		default:
			err_code = BadValue;
			break;
	}
	/*
	 * DAC check is based on client isolation.
	 */
	if (policy_flags & TSOL_DAC)
	{
		if (!client_private(client, object))
		{
            tsolinfo = GetClientTsolInfo(client);
            if (!tsolinfo)
                return (err_code);
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			/* PRIV override? */
			if (xpriv_policy(tsolinfo->privs, which_priv, res,
							 method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("clientid", misc, tsolinfo,
					 tsolinfo, "access xid", object);
				ret_stat = err_code;
			}
            if (do_audit)
            {
                set_audit_flags(tsolinfo);
                auditwrite(object_code, (u_long)object, tsolinfo->uid,
                           AW_APPEND, AW_END);
            }
		}
	}
	return (ret_stat);
}	/* access_xid */

/*
 * modify_fontpath: requires win_fontpath priv
 */
static int
modify_fontpath(xresource_t res, xmethod_t method, void *resource,
		void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	int	err_code = BadFont;
	Bool do_audit = FALSE;
	XID object = * (XID *) resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (priv_win_fontpath)
		return (PASSED);

	if (tsolinfo->flags & TSOL_AUDITEVENT)
		do_audit = TRUE;

	/*
	 * No MAC & DAC. Check win_fontpath priv only
	 */
	if (xpriv_policy(tsolinfo->privs, pset_win_fontpath,
					 res, method, client, do_audit))
	{
		ret_stat = PASSED;
	}
	else
	{
		ret_stat = err_code;
	}
    if (do_audit)
    {
        set_audit_flags(tsolinfo);
        auditwrite(AW_XFONT, (u_long)object, tsolinfo->uid, AW_APPEND, AW_END);
    }
	return (ret_stat);
}

/*
 * read_devices: All kbd/ptr ctrl/mapping related access.
 * requires win_devices priv
 * BadAccess is not a valid error code for many protocols
 * and does not work especially for SetPointerModifierMapping etc
 */
static int
read_devices(xresource_t res, xmethod_t method, void *resource,
			 void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	int	err_code = BadValue;
	Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (priv_win_devices)
		return (PASSED);

	if (tsolinfo->flags & TSOL_AUDITEVENT)
		do_audit = TRUE;

	/*
	 * No MAC/DAC check. Needs win_devices priv
	 */
	if (xpriv_policy(tsolinfo->privs, pset_win_devices,
					 res, method, client, do_audit))
	{
		ret_stat = PASSED;
	}
	else
	{
		ret_stat = err_code;
	}
	if (do_audit)
	{
		set_audit_flags(tsolinfo);
		auditwrite(AW_XCLIENT, client->index, AW_APPEND, AW_END);
	}
	return (ret_stat);
}

/*
 * modify_devices: All kbd/ptr ctrl/mapping related access.
 * requires win_devices priv
 */
static int
modify_devices(xresource_t res, xmethod_t method, void *resource,
			   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	int	err_code = BadAccess;
	Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (priv_win_devices)
		return (PASSED);

	if (tsolinfo->flags & TSOL_AUDITEVENT)
		do_audit = TRUE;

	/*
	 * No MAC/DAC check. Needs win_devices priv
	 */
	if (xpriv_policy(tsolinfo->privs, pset_win_devices,
					 res, method, client, do_audit))
	{
		ret_stat = PASSED;
	}
	else
	{
		ret_stat = err_code;
	}
	if (do_audit)
	{
		set_audit_flags(tsolinfo);
		auditwrite(AW_XCLIENT, client->index, AW_APPEND, AW_END);
	}
	return (ret_stat);
}

/*
 * modify_acl
 */
static int
modify_acl(xresource_t res, xmethod_t method, void *resource,
		   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	int	err_code = BadValue;
	Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (priv_win_config)
		return (PASSED);

	if (tsolinfo->flags & TSOL_AUDITEVENT)
		do_audit = TRUE;

	/*
	 * Needs win_config priv
	 */
	if (tsolinfo->uid != OwnerUID)
	{
	    if (xpriv_policy(tsolinfo->privs, pset_win_config, res,
                         method, client, do_audit))
	    {
		    ret_stat = PASSED;
	    }
	    else
	    {
		    ret_stat = err_code;
	    }
	}
	if (do_audit)
	{
		set_audit_flags(tsolinfo);
		auditwrite(AW_XCLIENT, client->index, AW_APPEND, AW_END);
	}
	return (ret_stat);
}

/*
 * read_atom
 */
static int
read_atom(xresource_t res, xmethod_t method, void *resource,
		  void *subject, xpolicy_t policy_flags, void *misc)
{

	return PASSED;
#if 0
	int ret_stat = PASSED;
	int	err_code = BadAtom;
    Bool do_audit = FALSE;
	NodePtr node = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	int	i, status;
	int	protocol = (int) *((CARD8 *) misc));

	/*
	 * MAC Check is slightly different. We do a series of
	 * MAC checks for all SLs in the table before we
	 * apply privs
	 */
	status = FAILED;
	if (policy_flags & TSOL_MAC)
	{
		for ( i = 0; i < node->clientCount; i++)
		{
			if (bldominates(tsolinfo->sl, node->sl[i]))
			{
				status = PASSED;
				break;
			}
		}
		if (status == FAILED)
		{
#ifndef NO_TSOL_DEBUG_MESSAGES
			LogMessageVerb(X_INFO, TSOL_MSG_POLICY_DENIED,
				       TSOL_LOG_PREFIX
				       "mac failed:%s,subj(%s,%d,%d),"
				       " read atom, %s\n",
				       LookupMajorName(protocol),
				       xsltos(tsolinfo->sl),
				       tsolinfo->uid, tsolinfo->pid,
				       NameForAtom(node->a));
#endif /* NO_TSOL_DEBUG_MESSAGES */
			/* PRIV override? */
			if (tsolinfo->flags & TSOL_AUDITEVENT)
			    do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_read,
					 res, method, client, do_audit))
			{
				status = PASSED;
				ret_stat = PASSED;
			}
			else
			{
				ret_stat = err_code;
			}
		}
	}
	/*
	 * No DAC check
	 */
    if (do_audit)
    {
        set_audit_flags(tsolinfo);
        auditwrite(AW_XATOM, NameForAtom(node->a), AW_APPEND, AW_END);
    }
	return (ret_stat);
#endif

}	/* read_atom */

/*
 * The read/modify window policy are for window attributes & not
 * for window contents. read/modify pixel handle the contents policy
 */
/*
 * read_property
 */
static int
read_property(xresource_t res, xmethod_t method, void *resource,
	void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadMatch;
	PropertyPtr pProp = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolPropPtr tsolprop;

	/* Initialize property created internally by server */
	tsolprop = TsolPropertyPriv(pProp);

	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!bldominates(tsolinfo->sl, tsolprop->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_read,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				SXTSOLERR("mac", misc, tsolprop,
					  tsolinfo, "read property",
					  NameForAtom(pProp->propertyName));
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
	        /*
		 * Anyone can read properties created internally by loadable modules.
		 * roles can read property created by workstation owner at admin_low.
		 */

		if (!((tsolprop->serverOwned) ||
			(tsolprop->uid == OwnerUID && blequal(tsolprop->sl, &PublicObjSL)) ||
			tsolprop->uid == tsolinfo->uid))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;

			if (xpriv_policy(tsolinfo->privs, pset_win_dac_read,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				ret_stat = err_code;
			}
		}
	}

	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XPROPERTY, tsolinfo->uid, tsolprop->uid,
				   NameForAtom(pProp->propertyName), AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* read_property */

/*
 * modify_property
 */
static int
modify_property(xresource_t res, xmethod_t method, void *resource,
				void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadAtom;
	PropertyPtr pProp = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolPropPtr tsolprop;

	/* Initialize property created internally by server */
	tsolprop = TsolPropertyPriv(pProp);

	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolprop->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				SXTSOLERR("mac", misc, tsolprop,
					  tsolinfo, "modify property",
					  NameForAtom(pProp->propertyName));
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		/*
		 * workstation owner can write properties created internally by loadable modules.
		 */

		if (!((tsolprop->serverOwned && tsolinfo->uid == OwnerUID) || tsolprop->uid == tsolinfo->uid))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;

			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				ret_stat = err_code;
			}
		}
	}

	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XPROPERTY, tsolinfo->uid, tsolprop->uid,
				   NameForAtom(pProp->propertyName), AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_property */

/*
 * destroy_property
 */
static int
destroy_property(xresource_t res, xmethod_t method, void *resource,
				 void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadAtom;
	PropertyPtr pProp = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolPropPtr tsolprop;

	/* Initialize property created internally by server */
	tsolprop = TsolPropertyPriv(pProp);

	/*
	 * MAC Check
	 */
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolprop->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				SXTSOLERR("mac", misc, tsolprop,
					  tsolinfo, "destroy property",
					  NameForAtom(pProp->propertyName));
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolprop->uid)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				SXTSOLERR("dac", misc, tsolprop,
					  tsolinfo, "destroy property",
					  NameForAtom(pProp->propertyName));
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XPROPERTY, tsolinfo->uid, tsolprop->uid,
				   NameForAtom(pProp->propertyName), AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* destroy_property */

/*
 * modify_grabwin
 */
static int
modify_grabwin(xresource_t res, xmethod_t method, void *resource,
			   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	int	err_code = BadWindow;
	Bool do_audit = FALSE;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres;

	/*
	 * Allow pointer grab on root window, as long as
	 * pointer is currently in a window owned by
	 * requesting client.
	 */

	if (WindowIsRoot(pWin))
	{
		pWin = TsolPointerWindow();
		if (WindowIsRoot(pWin))
			return (PASSED);
	}
	tsolres = TsolWindowPriv(pWin);

	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
					 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, tsolres, tsolinfo,
					 "read grabwin", pWin->drawable.id);
				do_audit = FALSE;  /* don't audit this */
				unset_audit_flags(tsolinfo);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid /* && tsolres->uid != 0 */)
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
					 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, tsolres, tsolinfo,
					 "read grabwin", pWin->drawable.id);
				do_audit = FALSE;  /* don't audit this */
				unset_audit_flags(tsolinfo);
				ret_stat = err_code;
			}
		}
	}
	/* Grab on trusted window requires TP */
	if ((ret_stat == PASSED) && XTSOLTrusted(pWin))
	{
		if (!HasTrustedPath(tsolinfo))
			ret_stat = err_code;
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_grabwin */

/*
 * modify_confwin - ConfineTo window access
 */
static int
modify_confwin(xresource_t res, xmethod_t method, void *resource,
			   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres = TsolWindowPriv(pWin);

        /*if (priv_win_devices)
        return (PASSED);*/

	/*
	 * confine window can be None. Root window is OK
	 */

	if (pWin == NullWindow || WindowIsRoot(pWin))
	{
		return (PASSED);
	}
	/*
	 * MAC Check
	 */
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, tsolres, tsolinfo,
					 "read grabwin", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid /* && tsolres->uid != 0 */)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, tsolres, tsolinfo,
					 "read grabwin", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/* Trusted window requires TP */
	if ((ret_stat == PASSED) && XTSOLTrusted(pWin))
	{
		if (!HasTrustedPath(tsolinfo))
			ret_stat = err_code;
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_confwin */

/*
 * create_srvgrab: GrabServer requires a priv
 */
static int
create_srvgrab(xresource_t res, xmethod_t method, void *resource,
			   void *subject, xpolicy_t policy_flags, void *misc)
{
	if (priv_win_config)
	{
		return (PASSED);
	}
	else
	{
		return (check_priv(res, method, resource, subject,
				   policy_flags, misc, pset_win_config));
	}
}

/*
 * destroy_srvgrab: GrabServer requires a priv
 */
static int
destroy_srvgrab(xresource_t res, xmethod_t method, void *resource,
				void *subject, xpolicy_t policy_flags, void *misc)
{
	if (priv_win_config)
	{
		return (PASSED);
	}
	else
	{
		return (check_priv(res, method, resource, subject,
				   policy_flags, misc, pset_win_config));
	}
}

/*
 * check_priv: Use this for all policies that require
 * no MAC/DAC, but a priv
 */
static int
check_priv(xresource_t res, xmethod_t method, void *resource,
	   void *subject, xpolicy_t policy_flags, void *misc, priv_set_t *priv)
{
	int ret_stat = PASSED;
	int	err_code = BadValue;
	Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	/*
	 * No MAC/DAC check.
	 */
	if (tsolinfo->flags & CONFIG_AUDITED)
	{
		do_audit = FALSE;
	}
	else if (tsolinfo->flags & TSOL_AUDITEVENT)
	{
		do_audit = TRUE;
		tsolinfo->flags |= CONFIG_AUDITED;
	}
	if (xpriv_policy(tsolinfo->privs, priv, res, method, client, do_audit))
	{
		ret_stat = PASSED;
	}
	else
	{
		ret_stat = err_code;
	}
	if (do_audit)
	{
		set_audit_flags(tsolinfo);
		auditwrite(AW_XCLIENT, client->index, AW_APPEND, AW_END);
	}
	return (ret_stat);
}

#ifndef NO_TSOL_DEBUG_MESSAGES
/*
 * Converts SL to string
 */
static char *
xsltos(bslabel_t *sl)
{
	char *slstring = NULL;

	if (bsltos(sl, &slstring, 0,
		VIEW_INTERNAL|SHORT_CLASSIFICATION | LONG_WORDS | ALL_ENTRIES) <= 0)
		return (NULL);
	else
		return slstring;
}
#endif /* !NO_TSOL_DEBUG_MESSAGES */

/*
 * read_selection
 */
static int
read_selection(xresource_t res, xmethod_t method, void *resource,
			   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadAtom;
	Selection *selection = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolSelnPtr tsolseln = (TsolSelectionPriv(selection));

	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!bldominates(tsolinfo->sl, tsolseln->sl))
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_read,
					 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				SXTSOLERR("mac", misc, tsolseln,
					  tsolinfo, "read selection",
					  NameForAtom(selection->selection));
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		/* uid == DEF_UID means public window, shared read */
		if (!(tsolseln->uid == DEF_UID || tsolinfo->uid == tsolseln->uid))
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_read,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				SXTSOLERR("dac", misc, tsolseln,
					  tsolinfo, "read selection",
					  NameForAtom(selection->selection));
				ret_stat = err_code;
			}
		}
	}

	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XATOM, NameForAtom(selection->selection),
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* read_selection */

/*
 * modify_propwin. This is slightly different from modify_window in that
 * Anyone can create/change properties on root.
 */
static int
modify_propwin(xresource_t res, xmethod_t method, void *resource,
			   void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres = TsolWindowPriv(pWin);

	/*
	 * Anyone can modify properties on  RootWindow  subjected to
	 * property policies.
	 */
	if (WindowIsRoot(pWin))
	{
		return (PASSED);
	}
/*
	if (XTSOLTrusted(pWin))
	{
		if (!HasTrustedPath(tsolinfo))
		{
			XTSOLERR("tp", misc, tsolres, tsolinfo,
				 "modify propwin", pWin->drawable.id);
			return (err_code);
		}
	}
*/
	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, tsolres, tsolinfo,
					 "modify propwin", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid)
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, tsolres, tsolinfo,
					 "modify propwin", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_propwin */

/*
 * modify_focuswin - Focus Window policy
 * Focus window can be None is checked outside of this func
 */
static int
modify_focuswin(xresource_t res, xmethod_t method, void *resource,
				void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres = TsolWindowPriv(pWin);
#if 0
	GrabPtr grab;
#endif

	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, tsolres, tsolinfo,
					 "modify focuswin", pWin->drawable.id);
				do_audit = FALSE;  /* don't audit this */
				unset_audit_flags(tsolinfo);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid)
		{
			if (tsolinfo->flags & TSOL_AUDITEVENT)
				do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
					 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, tsolres, tsolinfo,
					 "modify focuswin", pWin->drawable.id);
				do_audit = FALSE;  /* don't audit this */
				unset_audit_flags(tsolinfo);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * Trusted Path Windows require Trusted Path attrib
	 */
	if ((ret_stat == PASSED) && XTSOLTrusted(pWin))
	{
		if (!HasTrustedPath(tsolinfo))
		{
			XTSOLERR("tp", misc, tsolres, tsolinfo,
				 "modify focuswin", pWin->drawable.id);
			ret_stat = err_code;
		}
	}
#if 0
	/*
	 * This causes problems when dragging cmdtool
	 * TBD later
	 * If ptr/kbd is grabbed, then this client must be
	 * the grabbing client
	 */
	grab = inputInfo.pointer->grab;
	if (grab == NULL)
		grab = inputInfo.keyboard->grab;
	if (grab)
	{
		if (!SameClient(grab, client))
		{
			if (xpriv_policy(tsolinfo->privs, pset_win_devices,
				res, method, client))
			{
				/* audit? */
			}
			else
			{
				XTSOLERR("tp", misc, tsolres, tsolinfo,
					 "modify focuswin", pWin->drawable.id);
				return (err_code);
			}
		}
	}
#endif
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_focuswin */

/*
 * read_focuswin
 */
static int
read_focuswin(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres = TsolWindowPriv(pWin);

	/*
	 * Anyone can read RootWindow attributes
	 */
	if (WindowIsRoot(pWin))
	{
		return (PASSED);
	}
	/*
	 * MAC Check
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!(bldominates(tsolinfo->sl, tsolres->sl)))
		{
			if (!(tsolinfo->flags & MAC_READ_AUDITED) &&
			    (tsolinfo->flags & TSOL_AUDITEVENT))
			{
				do_audit = TRUE;
				tsolinfo->flags |= MAC_READ_AUDITED;
			}
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_read,
					 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, tsolres, tsolinfo,
					 "read focuswin", pWin->drawable.id);
				do_audit = FALSE;  /* don't audit this */
				unset_audit_flags(tsolinfo);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if ((tsolinfo->uid != tsolres->uid) && (tsolres->uid != 0))
		{
			if (!(tsolinfo->flags & DAC_READ_AUDITED) &&
			    (tsolinfo->flags & TSOL_AUDITEVENT))
			{
				do_audit = TRUE;
				tsolinfo->flags |= DAC_READ_AUDITED;
			}
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_read,
					 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, tsolres, tsolinfo,
					 "read focuswin", pWin->drawable.id);
				do_audit = FALSE;  /* don't audit */
				unset_audit_flags(tsolinfo);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* read_focuswin */

#ifndef NO_TSOL_DEBUG_MESSAGES
/*
 * XTsolErr : used for debugging.
 */
static void
XTsolErr(const char *err_type, CARD8 *protocol,
	 bslabel_t *osl, uid_t ouid, pid_t opid, const char *opname,
	 bslabel_t *ssl, uid_t suid, pid_t spid, const char *spname,
	 const char *method, int isstring, const void *xid)
{
	char xidbuf[16];
	const char *xidstring;

	if (*protocol == X_QueryTree || *protocol == X_GetInputFocus)
		return;

	if (isstring)
	{
		/* for atom/prop names */
		xidstring = (const char *) xid;
	}
	else
	{
		/* for window/pixmaps */
		snprintf(xidbuf, sizeof(xidbuf), "%lX", (long) xid);
		xidstring = xidbuf;
	}

	LogMessageVerb(X_NOTICE, TSOL_MSG_POLICY_DENIED,
		       TSOL_LOG_PREFIX "%s failed: %s,"
		       " obj(%s,%d,%d,%s), subj(%s,%d,%d,%s), %s, xid=%s\n",
		       err_type, TsolRequestNameString(*protocol),
		       xsltos(osl), ouid, opid, opname ? opname : "",
		       xsltos(ssl), suid, spid, spname ? spname : "",
		       method, xidstring);
}
#endif /* !NO_TSOL_DEBUG_MESSAGES */

/*
 * read_extn
 */
static int
read_extn(xresource_t res, xmethod_t method, void *resource,
		  void *subject, xpolicy_t policy_flags, void *misc)
{
/*	int err_code = BadAccess;
	char *extn_name = (char *)resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
*/
	/*
	 * No policy for this
	 */

	/*
	if (extn_name != NULL & *extn_name != '\0')
		ErrorF("Access to %s extension allowed\n", extn_name);
	*/
	return (PASSED);
}

/*
 * modify_window
 */
static int
modify_tpwin(xresource_t res, xmethod_t method, void *resource,
			 void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres = TsolWindowPriv(pWin);

	/*
	 * MAC Check
	 */
	if (policy_flags & TSOL_MAC)
	{
		if (!blequal(tsolinfo->sl, tsolres->sl))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, tsolres, tsolinfo,
					 "modify tpwin", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		if (tsolinfo->uid != tsolres->uid)
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, tsolres, tsolinfo,
					 "modify tpwin", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	/*
	 * Trusted Path Windows require Trusted Path attrib
	 */
	if ((ret_stat == PASSED) && XTSOLTrusted(pWin))
	{
		if (!HasTrustedPath(tsolinfo))
		{
			XTSOLERR("tp", misc, tsolres, tsolinfo,
				 "modify tpwin", pWin->drawable.id);
			ret_stat = err_code;
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_tpwin */

/*
 * modify_sl
 * requires win_upgrade/downgrade_sl privs
 * misc parameter is actually sl of resource & not the protocol no.
 * misc == NULL means we are trying to set session hi/lo clearance
 */
static int
modify_sl(xresource_t res, xmethod_t method, void *resource,
		  void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadAccess;
	bslabel_t *sl = (bslabel_t *)resource;	/* sl to be set */
	bslabel_t *res_sl = (bslabel_t *)misc;	/* resource sl */
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	/*
	 * Are we trying to check for session hi/lo clearance
	 */
	if (misc == NULL)
	{
        if (priv_win_config)
            return (ret_stat);

        if (tsolinfo->flags & TSOL_AUDITEVENT)
            do_audit = TRUE;
		if (xpriv_policy(tsolinfo->privs, pset_win_config,
                         res, method, client, do_audit))
		{
			ret_stat = PASSED;
		}
		else
		{
#ifndef NO_TSOL_DEBUG_MESSAGES
			LogMessageVerb(X_INFO, TSOL_MSG_POLICY_DENIED,
				       TSOL_LOG_PREFIX
				       "modify_sl: failed for %s\n",
				       tsolinfo->pname);
#endif /* NO_TSOL_DEBUG_MESSAGES */
			ret_stat = err_code;
		}
        if (do_audit)
        {
            set_audit_flags(tsolinfo);
            auditwrite(AW_SLABEL, sl, AW_APPEND, AW_END);
        }
        return (ret_stat);
	}
	/*
	 * No MAC/DAC Check. Check only for upgrade/downgrade priv
	 */
	if (bldominates(sl, res_sl))
	{
        if (tsolinfo->flags & TSOL_AUDITEVENT)
            do_audit = TRUE;
		if (xpriv_policy(tsolinfo->privs, pset_win_upgrade_sl,
						 res, method, client, do_audit))
		{
			ret_stat = PASSED;
		}
		else
		{
#ifndef NO_TSOL_DEBUG_MESSAGES
			LogMessageVerb(X_INFO, TSOL_MSG_POLICY_DENIED,
				       TSOL_LOG_PREFIX
				       "modify_sl: failed for %s\n",
				       tsolinfo->pname);
#endif /* NO_TSOL_DEBUG_MESSAGES */
			ret_stat = err_code;
		}
	}
	else
	{
        if (tsolinfo->flags & TSOL_AUDITEVENT)
            do_audit = TRUE;
		if (xpriv_policy(tsolinfo->privs, pset_win_downgrade_sl,
						 res, method, client, do_audit))
		{
			ret_stat = PASSED;
		}
		else
		{
#ifndef NO_TSOL_DEBUG_MESSAGES
			LogMessageVerb(X_INFO, TSOL_MSG_POLICY_DENIED,
				       TSOL_LOG_PREFIX
				       "modify_sl: failed for %s\n",
				       tsolinfo->pname);
#endif /* NO_TSOL_DEBUG_MESSAGES */
			ret_stat = err_code;
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_SLABEL, sl, AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_sl */


/*
 * modify_eventwin
 */
static int
modify_eventwin(xresource_t res, xmethod_t method, void *resource,
				void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	Bool do_audit = FALSE;
	int	err_code = BadWindow;
	WindowPtr pWin = resource;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);
	TsolResPtr tsolres = TsolWindowPriv(pWin);
	TsolInfoPtr tsolownerinfo;	/*client who owns the window */
	ClientPtr	ownerclient;

	ownerclient = clients[CLIENT_ID(pWin->drawable.id)];
	tsolownerinfo = GetClientTsolInfo(ownerclient);

	/*
	 * Anyone can send event to root win
	 */
	if (WindowIsRoot(pWin) || XTSOLTrusted(pWin))
	{
		return (PASSED);
	}
	/*
	 * MAC Check
	 * NOTE: window sl must dominate the client's sl for send event
	*/
	if (policy_flags & TSOL_MAC)
	{
		if (!bldominates(tsolres->sl, tsolinfo->sl))
		{
			/*
			 * event sends to windows owned by client with priv_win_seln
			 * particularly front panel whose sl is admin_low
			 */
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
							 res, method, client, do_audit) ||
				(tsolownerinfo && HasWinSelection(tsolownerinfo)))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("mac", misc, tsolres, tsolinfo,
					 "modify eventwin", pWin->drawable.id);
				ret_stat = ret_stat;
			}
		}
	}
	/*
	 * DAC Check
	 */
	if ((ret_stat == PASSED) && policy_flags & TSOL_DAC)
	{
		/* uid == DEF_UID means public window */
		if (!(XTSOLTrusted(pWin) ||
			tsolres->uid == DEF_UID ||
			tsolinfo->uid == tsolres->uid))
		{
            if (tsolinfo->flags & TSOL_AUDITEVENT)
                do_audit = TRUE;
			if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
							 res, method, client, do_audit))
			{
				ret_stat = PASSED;
			}
			else
			{
				XTSOLERR("dac", misc, tsolres, tsolinfo,
					 "modify eventwin", pWin->drawable.id);
				ret_stat = err_code;
			}
		}
	}
	if (do_audit)
	{
        set_audit_flags(tsolinfo);
		auditwrite(AW_XWINDOW, pWin->drawable.id, tsolres->uid,
				   AW_APPEND, AW_END);
	}
	return (ret_stat);
}	/* modify_eventwin */

/*
 * modify_stripe
 * Trusted stripe requires only trusted path attrib
 */
static int
modify_stripe(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	int	err_code = BadAccess;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (!HasTrustedPath(tsolinfo))
	{
		SXTSOLERR("tp", misc, tsolinfo, tsolinfo,
			  "modify stripe", "<none>");
		return (err_code);
	}
	return (PASSED);
}

/*
 * modify_wowner
 * set workstation owner
 */
static int
modify_wowner(xresource_t res, xmethod_t method, void *resource,
			  void *subject, xpolicy_t policy_flags, void *misc)
{
	int	err_code = BadAccess;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (!HasTrustedPath(tsolinfo))
	{
		SXTSOLERR("tp", misc, tsolinfo, tsolinfo,
			  "modify tpwin", "<none>");
		return (err_code);
	}
	return (PASSED);
}

/*
 * modify_uid
 * Set UID for resource
 */
static int
modify_uid(xresource_t res, xmethod_t method, void *resource,
	void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	int	err_code = BadAccess;
	Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

	if (tsolinfo->flags & TSOL_AUDITEVENT)
		do_audit = TRUE;
	if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
			 res, method, client, do_audit))
	{
		ret_stat = PASSED;
	}
	else
	{
#ifndef NO_TSOL_DEBUG_MESSAGES
		LogMessageVerb(X_INFO, TSOL_MSG_POLICY_DENIED,
			       TSOL_LOG_PREFIX
			       "modify_uid: failed for %s\n",
			       tsolinfo->pname);
#endif /* NO_TSOL_DEBUG_MESSAGES */
		ret_stat = err_code;
	}
	if (do_audit)
		set_audit_flags(tsolinfo);
	return (ret_stat);
}

/*
 * modify_polyinfo
 * Modify polyinstantiation info(sl, uid)
 */
static int
modify_polyinfo(xresource_t res, xmethod_t method, void *resource,
				void *subject, xpolicy_t policy_flags, void *misc)
{
	int ret_stat = PASSED;
	int	err_code = BadAccess;
        Bool do_audit = FALSE;
	ClientPtr client = subject;
	TsolInfoPtr tsolinfo = GetClientTsolInfo(client);

        if (tsolinfo->flags & TSOL_AUDITEVENT)
            do_audit = TRUE;
	if (xpriv_policy(tsolinfo->privs, pset_win_mac_write,
                     res, method, client, do_audit))
	{
		ret_stat = PASSED;
		if (xpriv_policy(tsolinfo->privs, pset_win_dac_write,
						 res, method, client, do_audit))
		{
			ret_stat = PASSED;
		}
		else
			ret_stat = err_code;
	}
	else
		ret_stat = err_code;
#ifndef NO_TSOL_DEBUG_MESSAGES
	LogMessageVerb(X_INFO, TSOL_MSG_POLICY_DENIED,
		       TSOL_LOG_PREFIX
		       "modify_polyinfo: failed for %s\n",
		       tsolinfo->pname);
#endif /* NO_TSOL_DEBUG_MESSAGES */
    if (do_audit)
        set_audit_flags(tsolinfo);
	return (ret_stat);
}

/*
 * access_dbe - check whether the buffer is client-private
 */
static int
access_dbe(xresource_t res, xmethod_t method, void *resource,
	   void *subject, xpolicy_t policy_flags, void *misc)
{
    ClientPtr client = subject;
    XID object = * (XID *) resource;

    if (client_private(client, object))
       return (PASSED);
    else
       return BadAccess;
}

/*
 * swap_dbe - check if the window is created by the client
 */
static int
swap_dbe(xresource_t res, xmethod_t method, void *resource,
	 void *subject, xpolicy_t policy_flags, void *misc)
{
	WindowPtr pWin = resource;
	ClientPtr client = subject;

	if (SAMECLIENT(client, pWin->drawable.id))
            return PASSED;
        else
            return BadAccess;
}

/*
 * Return value of 0 success,	errcode for failure
 *
 * Dummy function means no policy is needed for this access.
 */
static int
no_policy(xresource_t res, xmethod_t method, void *resource,
	void *subject, xpolicy_t policy_flags, void *misc)
{
	return (PASSED);
}

/*
 * X POLICY FUNCTION TABLE. One row per resource.
 *
 * TSOL_RES_NAME            READ                MODIFY              CREATE\
 *                          DESTROY             SPECIAL
 */
typedef int (*XTSOL_policy_func)
	(xresource_t res, xmethod_t method, void *resource,
	 void *subject, xpolicy_t policy_flags, void *misc);

static const XTSOL_policy_func
XTSOL_policy_table[TSOL_MAX_XRES_TYPES - TSOL_START_XRES][TSOL_MAX_XMETHODS] =
{

/* TSOL_RES_ACL */        { no_policy,          modify_acl,         no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_ATOM */       { read_atom,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_BELL */       { no_policy,          modify_devices,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_BTNGRAB */    { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_CCELL */      { read_ccell,         modify_ccell,       no_policy,\
                            destroy_ccell,      no_policy },
/* TSOL_RES_CLIENT */     { read_client,        modify_client,      no_policy,\
                            destroy_client,     no_policy },
/* TSOL_RES_CMAP */       { read_cmap,          modify_cmap,        no_policy,\
                            modify_cmap,        install_cmap },
/* TSOL_RES_CONFWIN */    { no_policy,          modify_confwin,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_CURSOR */     { no_policy,          modify_cursor,      no_policy,\
                            modify_cursor,      no_policy },
/* TSOL_RES_EVENTWIN */   { no_policy,          modify_eventwin,    no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_EXTN */       { read_extn,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_FOCUSWIN */   { read_focuswin,      modify_focuswin,    no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_FONT */       { read_font,          modify_font,        no_policy,\
                            modify_font,        no_policy },
/* TSOL_RES_FONTLIST */   { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_FONTPATH */   { no_policy,          modify_fontpath,    no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_GC */         { read_gc,            modify_gc,          no_policy,\
                            modify_gc,          no_policy },
/* TSOL_RES_GRABWIN */    { no_policy,          modify_grabwin,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_HOSTLIST */   { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_IL */         { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_KBDCTL */     { no_policy,          modify_devices,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_KBDGRAB */    { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_KEYGRAB */    { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_KEYMAP */     { read_devices,       modify_devices,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_MODMAP */     { no_policy,          modify_devices,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PIXEL */      { read_pixel,         modify_pixel,       no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PIXMAP */     { read_pixmap,        modify_pixmap,      no_policy,\
                            destroy_pixmap,     no_policy },
/* TSOL_RES_POLYINFO */   { no_policy,          modify_polyinfo,    no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PROPERTY */   { read_property,      modify_property,    no_policy,\
                            destroy_property,   no_policy },
/* TSOL_RES_PROPWIN */    { read_window,        modify_propwin,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_IIL */        { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PROP_SL */    { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PROP_UID */   { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PTRCTL */     { no_policy,          modify_devices,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PTRGRAB */    { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PTRLOC */     { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PTRMAP */     { no_policy,          modify_devices,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_PTRMOTION */  { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_SCRSAVER */   { no_policy,          modify_devices,     no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_SELECTION */  { read_selection,     no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_SELNWIN */    { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_SENDEVENT */  { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_SL */         { no_policy,          modify_sl,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_SRVGRAB */    { no_policy,          no_policy,      create_srvgrab,\
                            destroy_srvgrab,    no_policy },
/* TSOL_RES_STRIPE */     { no_policy,          modify_stripe,      no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_TPWIN */      { no_policy,          modify_tpwin,       no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_UID */        { no_policy,          modify_uid,         no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_VISUAL */     { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_WINATTR */    { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_WINDOW */     { read_window,        modify_window,   create_window,\
                            destroy_window,     no_policy },
/* TSOL_RES_WINLOC */     { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_WINMAP */     { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_WINSIZE */    { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_WINSTACK */   { no_policy,          no_policy,          no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_WOWNER */     { no_policy,          modify_wowner,      no_policy,\
                            no_policy,          no_policy },
/* TSOL_RES_DBE */        { no_policy,          no_policy,         access_dbe,
                            access_dbe,         swap_dbe }
};

struct xpolicy_cache {
	xresource_t res;
	xmethod_t method;
	void *resource_ptr;
	XID resource_id;
	void *subject;
	xpolicy_t policy_flags;
	int	ret_value;
	int	count;
};
static struct xpolicy_cache policy_cache;

/*
 * main xtsol_policy. External interface to dix layer of X server
 */
int
xtsol_policy(xresource_t res, xmethod_t method,	void *resource_ptr,
	     XID resource_id, void *subject, xpolicy_t policy_flags,
	     void *misc)
{
	int	res_type;
	int	ret_value;
	void *	resource;

	assert(res >= TSOL_START_XRES && res < TSOL_MAX_XRES_TYPES);
	assert(method >= 0 && method < TSOL_MAX_XMETHODS);
	assert(policy_flags != 0);
	res_type = (int)(res - TSOL_START_XRES);

	if (policy_cache.subject == subject &&
		policy_cache.res == res  &&
		policy_cache.method == method &&
		policy_cache.resource_ptr == resource_ptr  &&
		policy_cache.resource_id == resource_id  &&
		policy_cache.policy_flags == policy_flags)
	{

		policy_cache.count++;
		return policy_cache.ret_value;
	} else {
		policy_cache.res = res;
		policy_cache.method = method;
		policy_cache.resource_ptr = resource_ptr;
		policy_cache.resource_id = resource_id;
		policy_cache.subject = subject;
		policy_cache.policy_flags = policy_flags;

		if (resource_ptr != NULL) {
			resource = resource_ptr;
		} else {
			resource = &resource_id;
		}

		ret_value = ((XTSOL_policy_table[res_type][method]) (res,
			    method, resource, subject, policy_flags, misc));
		policy_cache.ret_value = ret_value;

		return ret_value;
	}
}

/*
 * Allocate a single privilege set
 */
static priv_set_t *
alloc_win_priv(const char *priv)
{
	priv_set_t *pset;

	if ((pset = priv_allocset()) == NULL) {
		perror("priv_allocset");
		FatalError("Cannot allocate privilege set");
	}
	priv_emptyset(pset);
	priv_addset(pset, priv);

	return pset;
}

/*
 * Initialize all string window privileges to the binary equivalent.
 * Binary privilege testing is much faster than the string testing
 */
void
init_win_privsets(void)
{

	pset_win_mac_read = alloc_win_priv(PRIV_WIN_MAC_READ);
	pset_win_mac_write = alloc_win_priv(PRIV_WIN_MAC_WRITE);
	pset_win_dac_read = alloc_win_priv(PRIV_WIN_DAC_READ);
	pset_win_dac_write = alloc_win_priv(PRIV_WIN_DAC_WRITE);
	pset_win_config = alloc_win_priv(PRIV_WIN_CONFIG);
	pset_win_devices = alloc_win_priv(PRIV_WIN_DEVICES);
	pset_win_fontpath = alloc_win_priv(PRIV_WIN_FONTPATH);
	pset_win_colormap = alloc_win_priv(PRIV_WIN_COLORMAP);
	pset_win_upgrade_sl = alloc_win_priv(PRIV_WIN_UPGRADE_SL);
	pset_win_downgrade_sl = alloc_win_priv(PRIV_WIN_DOWNGRADE_SL);
	pset_win_selection = alloc_win_priv(PRIV_WIN_SELECTION);
}

void
free_win_privsets(void)
{
	priv_freeset(pset_win_mac_read);
	priv_freeset(pset_win_mac_write);
	priv_freeset(pset_win_dac_read);
	priv_freeset(pset_win_dac_write);
	priv_freeset(pset_win_config);
	priv_freeset(pset_win_devices);
	priv_freeset(pset_win_fontpath);
	priv_freeset(pset_win_colormap);
	priv_freeset(pset_win_upgrade_sl);
	priv_freeset(pset_win_downgrade_sl);
	priv_freeset(pset_win_selection);
}

int
HasWinSelection(TsolInfoPtr tsolinfo)
{
	return (priv_issubset(pset_win_selection, (tsolinfo->privs)));
}

