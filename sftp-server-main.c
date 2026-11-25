/* $OpenBSD: sftp-server-main.c,v 1.6 2019/06/06 05:13:13 otto Exp $ */
/*
 * Copyright (c) 2008 Markus Friedl.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "sftp.h"
#include "misc.h"
#include "xmalloc.h"

void
cleanup_exit(int i)
{
	sftp_server_cleanup_exit(i);
}

/*
 * Pterodactyl/Wings runs each container as a per-server uid (e.g. 995)
 * with no entry in the image's /etc/passwd. The only things that
 * survive exec from sshd are uid/gid, cwd, and env (sshd sets
 * USER/LOGNAME/HOME/SHELL from its pw_copy before exec).
 */
static struct passwd pw_fallback;
static char pw_fallback_name[256];
static char pw_fallback_dir[PATH_MAX];
static char pw_fallback_shell[PATH_MAX];
static char pw_fallback_gecos[256];

static struct passwd *
passwd_for_current_uid(void)
{
	struct passwd *pw;
	const char *name, *home, *shell;
	struct stat st;

	if ((pw = getpwuid(getuid())) != NULL)
		return pw;

	name = getenv("USER");
	if (name == NULL || *name == '\0')
		name = getenv("LOGNAME");
	if (name == NULL || *name == '\0')
		name = getenv("SSH_USER");

	home = getenv("HOME");
	if (home == NULL || *home == '\0') {
		if (stat("/home/container", &st) == 0 && S_ISDIR(st.st_mode))
			home = "/home/container";
		else
			home = "/";
	}

	shell = getenv("SHELL");
	if (shell == NULL || *shell == '\0')
		shell = "/bin/sh";

	memset(&pw_fallback, 0, sizeof(pw_fallback));
	if (name != NULL && *name != '\0') {
		(void)snprintf(pw_fallback_name, sizeof(pw_fallback_name),
		    "%s", name);
		(void)snprintf(pw_fallback_gecos, sizeof(pw_fallback_gecos),
		    "%s", name);
	} else {
		(void)snprintf(pw_fallback_name, sizeof(pw_fallback_name),
		    "user-%lu", (u_long)getuid());
		(void)snprintf(pw_fallback_gecos, sizeof(pw_fallback_gecos),
		    "user-%lu", (u_long)getuid());
	}
	(void)snprintf(pw_fallback_dir, sizeof(pw_fallback_dir), "%s", home);
	(void)snprintf(pw_fallback_shell, sizeof(pw_fallback_shell),
	    "%s", shell);

	pw_fallback.pw_name = pw_fallback_name;
	pw_fallback.pw_passwd = "x";
	pw_fallback.pw_uid = getuid();
	pw_fallback.pw_gid = getgid();
	pw_fallback.pw_gecos = pw_fallback_gecos;
	pw_fallback.pw_dir = pw_fallback_dir;
	pw_fallback.pw_shell = pw_fallback_shell;

	fprintf(stderr,
	    "sftp-server: no passwd entry for uid %lu, "
	    "using synthesized user \"%s\" home \"%s\"\n",
	    (u_long)getuid(), pw_fallback.pw_name, pw_fallback.pw_dir);
	return &pw_fallback;
}

int
main(int argc, char **argv)
{
	struct passwd *user_pw;

	/* Ensure that fds 0, 1 and 2 are open or directed to /dev/null */
	sanitise_stdfd();

	user_pw = passwd_for_current_uid();
	if (user_pw == NULL) {
		fprintf(stderr, "No user found for uid %lu\n",
		    (u_long)getuid());
		return 1;
	}

	return (sftp_server_main(argc, argv, user_pw));
}
