/* $Id: nntpcl.c 1.4 1996/05/18 21:12:57 cthuang Exp $
 *
 * NNTP client routines
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "socket.h"
#include "nntp.h"
#include "nntpcl.h"

/* Open connection to NNTP server.
 * Return socket handle or -1 on error.
 */
int
nntpConnect (void)
{
    struct servent *sp;
    char buf[BUFSIZ];
    int response;
    int socket;

    if (nntpServer == NULL) {
	fprintf(stderr,
	    "Set the NNTPSERVER environment variable to the news host.\n");
	exit(EXIT_FAILURE);
    }

    if ((sp = getservbyname("nntp", "tcp")) == NULL) {
	fprintf(stderr, "nntp/tcp: Unknown service.\n");
	return -1;
    }

    socket = Socket(nntpServer, ntohs(sp->s_port));
    if (socket < 0) {
	fprintf(stderr, "Cannot connect to news server %s\n", nntpServer);
	return -1;
    }

    if (SockGets(socket, buf, sizeof(buf)) == 0) {
	response = atoi(buf);
	switch (response) {
	case OK_NOPOST:
	    printf("You cannot post articles to the news server %s\n",
		nntpServer);
	    break;

	case OK_CANPOST:
	    break;

	case ERR_ACCESS:
	    printf("You do not have permission to use the news server %s\n",
		nntpServer);
	    return -1;

	default:
	    printf("Unexpected response from news server %s\n", nntpServer);
	    puts(buf);
	    return -1;
	}
    }

    /* This is for INN */
    SockPuts(socket, "mode reader");
    SockGets(socket, buf, sizeof(buf));

    return socket;
}

/* Close NNTP connection. */

void
nntpClose (int socket)
{
    SockPuts(socket, "QUIT");
    SockClose(socket);
}

/* Select newsgroup to read from
 * Return TRUE if successful.
 */
int
nntpGroup (int socket, const char *ngname, ArticleNumber *pLo,
    ArticleNumber *pHi)
{
    char buf[BUFSIZ];
    int count;

    SockPrintf(socket, "GROUP %s\r\n", ngname);
    if (SockGets(socket, buf, sizeof(buf)) != 0) {
	return 0;
    }

    if (buf[0] == CHAR_OK) {
	sscanf(buf+4, "%d %d %d", &count, pLo, pHi);
    } else {
	fprintf(stderr, "%s: %s\n", buf, ngname);
    }
    return buf[0] == CHAR_OK;
}

/* Request overview for the current newsgroup.
 * Return TRUE if successful.
 */
int
nntpXover (int socket, ArticleNumber lo, ArticleNumber hi)
{
    char buf[BUFSIZ];

    if (lo < hi)
	SockPrintf(socket, "XOVER %ld-%ld\r\n", lo, hi);
    else
	SockPrintf(socket, "XOVER %ld\r\n", lo);

    if (SockGets(socket, buf, sizeof(buf)) != 0)
	return 0;
    if (buf[0] != CHAR_OK)
	fprintf(stderr, "%s\n", buf);

    return buf[0] == CHAR_OK;
}

/* Get next article in group.
 * Return TRUE if successful.
 */
int
nntpNext (int socket, ArticleNumber *pArtNum)
{
    char buf[BUFSIZ];

    SockPrintf(socket, "NEXT\r\n");
    if (SockGets(socket, buf, sizeof(buf)) != 0) {
	return 0;
    }

    if (buf[0] == CHAR_OK) {
	sscanf(buf+4, "%ld", pArtNum);
    }
    return buf[0] == CHAR_OK;
}

/* Get article from server.
 * Return TRUE if successful.
 */
int
nntpArticle (int socket, const char *cmd, ArticleNumber artnum, FILE *outf)
{
    char buf[BUFSIZ];
    char *bufp;

    SockPrintf(socket, "%s %ld\r\n", cmd, artnum);
    if (SockGets(socket, buf, sizeof(buf)) < 0) {
	return 0;
    }

    if (buf[0] == CHAR_FATAL) {	/* Fatal error */
	fprintf(stderr, "%s\n", buf);
	exit(EXIT_FAILURE);
    }

    if (buf[0] != CHAR_OK) {		/* and get it's reaction */
	return 0;
    }

    while (SockGets(socket, buf, sizeof(buf)) == 0) {
	bufp = buf;
	if (buf[0] == '.') {
	    ++bufp;
	    if (buf[1] == '\0')
		break;
	}

	fputs(bufp, outf);
	fputc('\n', outf);
    }

    return 1;
}

/* Get date from server.
 * Return TRUE if successful.
 */
int
nntpDate (int socket, char *dest)
{
    char buf[BUFSIZ];

    SockPuts(socket, "DATE");
    if (SockGets(socket, buf, sizeof(buf)) != 0) {
	return 0;
    }

    if (buf[0] == CHAR_INF) {
	sscanf(buf+4, "%s", dest);
    }
    return buf[0] == CHAR_INF;
}

/* Post article to NNTP server.
 * Return TRUE if successful.
*/
int
nntpPost (int socket, FILE *inf, size_t bytes)
{
    char buf[BUFSIZ], *s;
    size_t len, count;
    long offset;

    SockPuts(socket, "POST");
    if (SockGets(socket, buf, sizeof(buf)) < 0) {
	return 0;
    }
    if (buf[0] != CHAR_CONT) {
	if (atoi(buf) == ERR_NOPOST) {
	    fprintf(stderr, "You cannot post to this server.\n");
	}
	fprintf(stderr, "%s\n", buf);
	return 0;
    }

    offset = ftell(inf);
    count = bytes;
    while (fgets(buf, sizeof(buf), inf) && count > 0) {
	count -= strlen(buf);
	if ((s = strchr(buf, '\n')) != NULL)
	    *s = '\0';
	if (buf[0] == '.')
	    SockWrite(socket, buf, 1);
	SockPrintf(socket, "%s\r\n", buf);
    }
    fseek(inf, offset+bytes, SEEK_SET);

    SockPrintf(socket, ".\r\n");

    if (SockGets(socket, buf, sizeof(buf)) < 0) {
	return 0;
    }
    if (buf[0] != CHAR_OK) {
	if (atoi(buf) == ERR_POSTFAIL) {
	    fprintf(stderr, "Article not accepted by server; not posted.\n");
	}
	fprintf(stderr, "%s\n", buf);
	return 0;
    }

    return 1;
}
