/* $Id: socket.c 1.3 1996/05/18 21:06:26 cthuang Exp $
 *
 * This module has been modified for souper.
 */

/***********************************************************************
  module:       socket.c
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  UNIX sockets code.
 ***********************************************************************/
 
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#ifdef __WIN32__
#include <winsock.h>
#else
#include <unistd.h>
#endif

#include "socket.h"

/* requisite, venerable SCCS ID */
static char sccs_id [] = "@(#)socket.c  1.4\t3/29/94";

int
Socket (const char *host, short clientPort)
{
#if defined(__OS2__) && !defined(__EMX__)
    static char initialized = 0;
#endif
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad;
    struct hostent *hp;

#if defined(__OS2__) && !defined(__EMX__)
    if (!initialized) {
	sock_init();
	initialized = 1;
    }
#endif
    
    memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET;

    inaddr = inet_addr(host);
    if (inaddr != INADDR_NONE)
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    else
    {
        hp = gethostbyname(host);
        if (hp == NULL)
            return -1;
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }
    ad.sin_port = htons(clientPort);
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
#ifdef __WIN32__
    if (sock == INVALID_SOCKET) return sock;
#else
    if (sock < 0) return sock;
#endif
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
        return -1;
    return sock;
}

void
SockClose (int s)
{
    shutdown(s, 2);
#if defined(__OS2__) && !defined(__EMX__)
    soclose(s);
#elif defined(__WIN32__)
    closesocket(s);
#else
    close(s);
#endif
}

#define BUFSIZE 4096

int
SockGets (int s, char *dest, int n)
{
    static char buffer1[BUFSIZE];
    static char *buffer = buffer1;
    static int bufsize = 0;

    char *lfptr;
    int stringpos = 0, copysize, posinc, flReturnString = 0;

    for (;;)
        {
        /* get position of LF in the buffer */
        lfptr = memchr( buffer, '\n', bufsize );

        /* if there is a LF in the buffer, then ... */
        if ( lfptr )
            {
            copysize = lfptr - buffer + 1;
            flReturnString = 1;
            }
        else
            copysize = bufsize;

        /* make sure we won't write more than n-1 characters */
        if ( copysize > (n - 1) - stringpos )
            {
            copysize = (n - 1) - stringpos;
            flReturnString = 1;
            }

        /* copy copysize characters from buffer into the string */
        memcpy( dest + stringpos, buffer, copysize );
        stringpos += copysize;

        /* if we got the whole string, then set buffer to the unused data
           and return the string */
        if ( flReturnString )
            {
	    if (dest[stringpos-2] == '\r')
		dest[stringpos-2] = '\0';
	    else if (dest[stringpos-1] == '\n')
		dest[stringpos-1] = '\0';
	    else
		dest[stringpos] = '\0';
            bufsize -= copysize;
            buffer += copysize;
            return 0;
            }

        /* reset buffer and receive more data into buffer */
        buffer = buffer1;
        if ( (bufsize = recv( s, buffer1, sizeof(buffer1), 0 )) == -1 )
            {
            perror( "Error on socket recv" );
            return -1;
            }
        }
#if 0
    while (--len)
    {
        if (recv(socket, buf, 1, 0) != 1)
            return -1;
        if (*buf == '\n')
            break;
        if (*buf != '\r') /* remove all CRs */
            buf++;
    }
    *buf = 0;
    return 0;
#endif
}

int
SockPuts (int socket, char *buf)
{
    int rc;
    
    if ((rc = SockWrite(socket, buf, strlen(buf))) < 0)
        return rc;
    return SockWrite(socket, "\r\n", 2);
}

int
SockWrite (int socket, void *src, int len)
{
    int n;
    char *buf = (char *)src;
    
    while (len) {
        n = send(socket, buf, len, 0);
        if (n <= 0)
            return -1;
        len -= n;
        buf += n;
    }
    return 0;
}

int SockRead (int socket, void *dest, int len)
{
    int n;
    char *buf = (char *)dest;
    
    while (len) {
        n = recv(socket, buf, len, 0);
        if (n <= 0)
            return -1;
        len -= n;
        buf += n;
    }
    return 0;
}

int
SockPrintf (int socket, const char *format, ...)
{
    va_list ap;
    char buf[2048];
    
    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);
    return SockWrite(socket, buf, strlen(buf));
}

#if 0
int
SockStatus (int socket, int seconds)
{
    fd_set fds;
    struct timeval timeout;
    
    FD_ZERO(fds);
    FD_SET(fds, socket);
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    if (select(socket+1, &fds, (fd_set *)0, (fd_set *)0, &timeout) <= 0)
        return 0;
    return 1;
}
#endif
