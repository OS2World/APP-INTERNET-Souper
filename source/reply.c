/* $Id: reply.c 1.3 1996/05/18 21:13:33 cthuang Exp $
 *
 * Send reply packet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "souper.h"
#include "smtp.h"
#include "nntpcl.h"

#ifdef __WIN32__
#define popen _popen
#define pclose _pclose
#endif

static int smtpSock;
static int nntpSock;

static char *mailer;
static char *poster;

/* Return TRUE if the line begins with this header. */

int
isHeader (const char *buf, const char *header, size_t len)
{
    return strnicmp(buf, header, len) == 0 && buf[len] == ':' &&
	   (buf[len+1] == ' ' || buf[len+1] == '\t');
}

/* Get the value for the message header.
 * Return pointer to an allocated buffer containing the header value.
 */
char *
getHeader (FILE *fd, const char *header)
{
    char buf[BUFSIZ], *result;
    long offset;
    int headerLen, n;

    /* Remember file position */
    offset = ftell(fd);

    headerLen = strlen(header);

    /* Look through header */
    while (fgets(buf, sizeof(buf), fd)) {
	if (buf[0] == '\n')
	    break;

	if (isHeader(buf, header, headerLen)) {
	    fseek(fd, offset, SEEK_SET);
	    n = strlen(buf + headerLen + 2);
	    result = (char *)xmalloc(n + 1);
	    strcpy(result, buf + headerLen + 2);
	    if (result[n-1] == '\n')
		result[n-1] = '\0';
	    return result;
	}
    }       

    /* Reposition file */
    fseek(fd, offset, SEEK_SET);
    return NULL;
}

#ifndef __WATCOMC__
/* Pipe a message to the specified delivery agent. */

static void
sendPipe (FILE *fd, size_t bytes, const char *agent)
{
    FILE *pfd;
    unsigned char c;

    /* Open pipe to agent */
    if ((pfd = popen(agent, "w")) == NULL) {
	fprintf(stderr, "%s: can't open reply pipe\n", progname);
	while (bytes--)
	    fgetc(fd);
	return;
    }

    /* Send message to pipe */
    while (bytes--) {
	c = fgetc(fd);
	fputc(c, pfd);
    }

    pclose(pfd);
}
#endif

static void
sendMail (FILE *inf, size_t bytes)
{
#ifndef __WATCOMC__
    if (mailer) {
	char *to = getHeader(inf, "To");
	if (to) {
	    printf("%s: mailing to %s\n", progname, to);
	    free(to);
	}

	/* Pipe message to delivery agent */
	sendPipe(inf, bytes, mailer);

    } else
#endif
    if (!smtpMail(smtpSock, inf, bytes)) {
	exit(EXIT_FAILURE);
    }
}

static void
sendNews (FILE *inf, size_t bytes)
{
#ifndef __WATCOMC__
    if (poster) {
	/* Pipe message to delivery agent */
	sendPipe(inf, bytes, poster);

    } else
#endif
    if (!nntpPost(nntpSock, inf, bytes)) {
	exit(EXIT_FAILURE);
    }
}

/* Process a mail reply file, usenet type */

static void
sendMailu (const char *fn)
{
    char buf[BUFSIZ];
    FILE *fd;
    int bytes;
    char *to, *addr;

    /* Open the reply file */
    if ((fd = fopen (fn, "rb")) == NULL) {
	fprintf(stderr, "%s: can't open %s\n", progname, fn);
	return;
    }

    /* Read through it */
    while (fgets(buf, sizeof(buf), fd)) {
	if (strncmp (buf, "#! rnews ", 9)) {
	    fprintf(stderr, "%s: malformed reply file\n", progname);
	    fclose(fd);
	    return;
	}

	/* Get byte count */
	sscanf(buf+9, "%d", &bytes);

	sendMail(fd, bytes);
    }

    fclose(fd);
}

/* Process a news reply file, usenet type */

static void
sendNewsu (const char *fn)
{
    char buf[BUFSIZ];
    FILE *fd;
    int bytes;
    char *grp;

    /* Open the reply file */
    if ((fd = fopen (fn, "rb")) == NULL) {
	fprintf (stderr, "%s: can't open %s\n", progname, fn);
	return;
    }

    /* Read through it */
    while (fgets(buf, sizeof(buf), fd)) {
	if (strncmp (buf, "#! rnews ", 9)) {
	    fprintf(stderr, "%s: malformed reply file\n", progname);
	    break;
	}

	grp = getHeader(fd, "Newsgroups");
	printf("%s: Posting article to %s\n", progname, grp);
	free(grp);

	sscanf(buf+9, "%d", &bytes);
	sendNews(fd, bytes);
    }
    fclose(fd);
}

/* Process a mail reply file, binary type */

static void
sendMailb (const char *fn)
{
    char buf[BUFSIZ];
    unsigned char count[4];
    FILE *fd;
    int bytes;
    char *to, *addr;

    /* Open the reply file */
    if ((fd = fopen(fn, "rb")) == NULL) {
	fprintf(stderr, "%s: can't open %s\n", progname, fn);
	return;
    }

    /* Read through it */
    while (fread(count, sizeof(char), 4, fd) == 4) {
	/* Get byte count */
	bytes = ((count[0]*256 + count[1])*256 + count[2])*256 + count[3];
	sendMail(fd, bytes);
    }

    fclose(fd);
}

/* Process a news reply file, binary type */

static void
sendNewsb (const char *fn)
{
    char buf[BUFSIZ];
    unsigned char count[4];
    FILE *fd;
    int bytes;
    char *grp;

    /* Open the reply file */
    if ((fd = fopen (fn, "rb")) == NULL) {
	fprintf(stderr, "%s: can't open %s\n", progname, fn);
	return;
    }

    /* Read through it */
    while (fread(count, sizeof(char), 4, fd) == 4) {
	grp = getHeader(fd, "Newsgroups");
	printf("%s: Posting article to %s\n", progname, grp);
	free(grp);

	bytes = ((count[0]*256 + count[1])*256 + count[2])*256 + count[3];
	sendNews(fd, bytes);
    }
    fclose(fd);
}

/* Process a reply packet. */

void
sendReply (void)
{
    FILE *rep_fd;
    char buf[BUFSIZ], repFile[FILENAME_MAX];
    char fname[FILENAME_MAX], kind[FILENAME_MAX], type[FILENAME_MAX];

    /* Open the packet */
    strcpy(repFile, "REPLIES");
    if ((rep_fd = fopen(repFile, "rb")) == NULL) {
	fprintf(stderr, "%s: can't open %s\n", progname, repFile);
	return;
    }

    /* Look through lines in REPLIES file */
    smtpSock = nntpSock = -1;
    while (fgets(buf, sizeof(buf), rep_fd)) {
	if (sscanf(buf, "%s %s %s", fname, kind, type) != 3) {
	    fprintf(stderr, "%s: malformed REPLIES line\n", progname);
	    break;
	}

	/* Check reply type */
	if (type[0] != 'u' && type[0] != 'b' && type[0] != 'B') {
	    fprintf(stderr, "%s: reply type %c not supported\n", progname,
		type[0]);
	    continue;
	}

	/* Look for mail or news */
	if (strcmp(kind, "mail") == 0) {
	    mailer = getenv("MAILER");
	    if (!mailer && smtpSock == -1)
		if ((smtpSock = smtpConnect()) < 0)
		    exit(EXIT_FAILURE);
	} else if (strcmp(kind, "news") == 0) {
	    poster = getenv("POSTER");
	    if (!poster && nntpSock == -1)
		if ((nntpSock = nntpConnect()) < 0)
		    exit(EXIT_FAILURE);
	} else {
	    fprintf (stderr, "%s: bad reply kind: %s\n", progname, kind);
	    continue;
	}

	/* Make file name */
	strcat(fname, ".MSG");

	/* Process it */
	switch (type[0]) {
	case 'u':
	    if (strcmp(kind, "mail") == 0) sendMailu(fname);
	    if (strcmp(kind, "news") == 0) sendNewsu(fname);
	    break;
	case 'b':
	case 'B':
	    if (strcmp(kind, "mail") == 0) sendMailb(fname);
	    if (strcmp(kind, "news") == 0) sendNewsb(fname);
	    break;
	}

	/* Delete it */
	if (!readOnly)
	    remove(fname);
    }

    if (smtpSock >= 0)
	smtpClose(smtpSock);
    if (nntpSock >= 0)
	nntpClose(nntpSock);

    fclose(rep_fd);

    if (!readOnly)
	remove(repFile);
}

