/* $Id: smtp.c 1.1 1996/05/18 21:13:41 cthuang Exp $
 *
 * Send mail reply packet using SMTP directly
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#ifdef __WIN32__
#include <winsock.h>
#else
#include <unistd.h>
#endif
#include "socket.h"
#include "souper.h"

extern char *mailGateway;

/* Close SMTP connection and socket */

void
smtpClose (int socket)
{
    SockPuts(socket, "QUIT");
    SockClose(socket);
}

/* Get a response from the SMTP server and test it
*/
static int
getSmtpReply (int socket, char *response)
{
    char buf[BUFSIZ];

    do {
	SockGets(socket, buf, BUFSIZ);
    } while (buf[3] == '-');		/* wait until not a continuation */

    if (strncmp(buf, response, 3) != 0) {
	fprintf(stderr, "Expecting SMTP %s reply, got %s\n", response, buf);
    }
    return (buf[0] == *response);	/* only first digit really matters */
}

/* Open socket and intialize connection to SMTP server. */

#ifdef DEBUG
int
smtpConnect (void)
{
    return 1;
}
#else
int
smtpConnect (void)
{
    struct servent *sp;
    int	socket;
    struct sockaddr_in local;
    int addrLen;
    struct hostent *hp;
    const char *fromHost;

    if ((sp = getservbyname("smtp", "tcp")) == NULL) {
	fprintf(stderr, "smtp/tcp: Unknown service.\n");
	return -1;
    }

    if ((socket = Socket(mailGateway, ntohs(sp->s_port))) < 0) {
	fprintf(stderr, "Cannot connect to mail server %s\n", mailGateway);
	return -1;
    }

    if (!getSmtpReply(socket, "220")) {
	printf("Disconnecting from %s\n", mailGateway);
	smtpClose(socket);
	return -1;
    }

    addrLen = sizeof(local);
    getsockname(socket, (struct sockaddr *)&local, &addrLen);
    hp = gethostbyaddr((const char *)&local.sin_addr, sizeof(local.sin_addr),
	AF_INET);
    fromHost = hp ? hp->h_name : inet_ntoa(local.sin_addr);
    SockPrintf(socket, "HELO %s\r\n", fromHost);
    if (!getSmtpReply(socket, "250")) {
	printf("Disconnecting from %s\n", mailGateway);
	smtpClose(socket);
	return -1;
    }
    return socket;
}
#endif

/* Extract mail address from the string.
 * Return a pointer to a static buffer containing the address or
 * NULL on an error.
 */
static const char *
extractAddress (const char *src)
{
    static char buf[BUFSIZ];
    char ch, *put;
    const char *get;
    char gotAddress;

    gotAddress = 0;
    put = buf;
    if ((get = strchr(src, '<')) != 0) {
	char ch = *++get;
	while (ch != '>' && ch != '\0') {
	    *put++ = ch;
	    ch = *++get;
	}
	gotAddress = 1;
    } else {
	get = src;
	ch = *get++;

	/* Skip leading whitespace. */
	while (ch != '\0' && isspace(ch))
	    ch = *get++;

	while (ch != '\0') {
	    if (isspace(ch)) {
		ch = *get++;

	    } else if (ch == '(') {
		/* Skip comment. */
		int nest = 1;
		while (nest > 0 && ch != '\0') {
		    ch = *get++;

		    if (ch == '(')
			++nest;
		    else if (ch == ')')
			--nest;
		}

		if (ch == ')') {
		    ch = *get++;
		}

	    } else if (ch == '"') {
		/* Copy quoted string. */
		do {
		    *put++ = ch;
		    ch = *get++;
		} while (ch != '"' && ch != '\0');

		if (ch == '"') {
		    *put++ = ch;
		    ch = *get++;
		}

	    } else {
		/* Copy address. */
		while (ch != '\0' && ch != '(' && !isspace(ch)) {
		    *put++ = ch;
		    ch = *get++;
		}
		gotAddress = 1;
	    }
	}
    }

    if (gotAddress) {
	*put = '\0';
	return buf;
    } else {
	return NULL;
    }
}

/* Search for ',' separating addresses.
*/
static char *
findAddressSep (char *src)
{
    char ch, matchCh;

    ch = *src; 
    while (ch != '\0' && ch != ',') {
        if (ch == '"') {
            matchCh = '"';
        } else if (ch == '(') {
            matchCh = ')';
        } else if (ch == '<') {
            matchCh = '>';
        } else {
            matchCh = '\0';
        }

        if (matchCh) {
            do {
                ch = *(++src);
            } while (ch != '\0' && ch != matchCh);

            if (ch == '\0')
                break;
        }
        ch = *(++src);
    }

    return src;
}
    
/* Send RCPT command.
*/
static void
sendSmtpRcpt (int socket, const char *buf)
{
    printf("%s: mailing to %s\n", progname, buf);
    SockPrintf(socket, "RCPT TO:<%s>\r\n", buf);
    getSmtpReply(socket, "250");
}

/* Send an RCPT command for each address in the address list.
*/
static void
putAddresses (int socket, char *addresses)
{
    char *srcEnd, *startAddr, *endAddr, saveCh;
    const char *addr;
    
    srcEnd = strchr(addresses, '\0');
    startAddr = addresses;

    while (startAddr < srcEnd) {
	endAddr = findAddressSep(startAddr);
	saveCh = *endAddr;
	*endAddr = '\0';
	addr = extractAddress(startAddr);
	if (addr) {
	    sendSmtpRcpt(socket, addr);
	}
	*endAddr = saveCh;
	startAddr = endAddr + 1;
    }
}

/* Send message to SMTP server.  Return TRUE if successful.
 */
int
smtpMail (int socket, FILE *fd, size_t bytes)
{
    const char *addr;
    char buf[BUFSIZ], *addrs, ch;
    char *from, *resentTo, *s;
    char more;
    long offset;
    size_t count;

    /* Look for From header and send MAIL command to SMTP server. */
    from = getHeader(fd, "From");
    if (from == NULL || (addr = extractAddress(from)) == NULL) {
	puts("No address in From header");
	return 0;
    }
    printf("%s: mailing from %s\n", progname, addr);
    SockPrintf(socket, "MAIL FROM:<%s>\r\n", addr);
    free(from);
    if (!getSmtpReply(socket, "250")) {
	return 0;
    }

    offset = ftell(fd);
    if ((resentTo = getHeader(fd, "Resent-To")) != NULL) {
	/* Send to address on Resent-To header. */
	putAddresses(socket, resentTo);
	free(resentTo);
    } else {
	/* Send to addresses on To, Cc and Bcc headers. */
	more = fgets(buf, sizeof(buf), fd) != 0;
	while (more) {
	    if (buf[0] == '\n')
		break;

	    if (isHeader(buf, "To", 2) || isHeader(buf, "Cc", 2)
	     || isHeader(buf, "Bcc", 3))
	    {
		addrs = buf;
		while (*addrs != '\0' && !isspace(*addrs)) {
		    ++addrs;
		}
		putAddresses(socket, addrs);

		/* Read next line and check if it is a continuation line. */
		while ((more = fgets(buf, sizeof(buf), fd) != 0) != 0) {
		    ch = buf[0];
		    if (ch == '\n' || (ch != ' ' && ch != '\t'))
			break;
		    putAddresses(socket, buf);
		}

		continue;
	    }
	
	    more = fgets(buf, sizeof(buf), fd) != 0;
	}
    }

    /* Send the DATA command and the mail message line by line. */
    SockPuts(socket, "DATA");
    if (!getSmtpReply(socket, "354")) {
	return 0;
    }

    fseek(fd, offset, SEEK_SET);
    count = bytes;
    while (fgets(buf, sizeof(buf), fd) && count > 0) {
	count -= strlen(buf);
	if ((s = strchr(buf, '\n')) != NULL)
	    *s = '\0';
	if (buf[0] == '.') {
	    SockWrite(socket, buf, 1);	/* write an extra . */
	}
	SockPrintf(socket, "%s\r\n", buf);
    }
    fseek(fd, offset+bytes, SEEK_SET);

    SockPuts(socket, ".");
    getSmtpReply(socket, "250");

    return 1;
}
