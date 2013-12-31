/* $Id: pop3.c 1.5 1996/05/18 21:06:26 cthuang Exp $
 *
 * This module has been modified for souper.
 */

/* Copyright 1993,1994 by Carl Harris, Jr.
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 *
 * Send bug reports, bug fixes, enhancements, requests, flames, etc., and
 * I'll try to keep a version up to date.  I can be reached as follows:
 * Carl Harris <ceharris@vt.edu>
 */


/***********************************************************************
  module:       pop3.c
  program:      popclient
  SCCS ID:      @(#)pop3.c      2.4  3/31/94
  programmer:   Carl Harris, ceharris@vt.edu
  date:         29 December 1993
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  POP2 client code.
 ***********************************************************************/
 
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "socket.h"
#include "souper.h"

/* requisite, venerable SCCS ID string */
static char sccs_id [] = "@(#)pop3.c    2.4\t3/31/94";

/* TCP port number for POP2 as defined by RFC 937 */
#define   POP3_PORT     110

/* exit code values */
#define PS_SUCCESS      0       /* successful receipt of messages */
#define PS_NOMAIL       1       /* no mail available */
#define PS_SOCKET       2       /* socket I/O woes */
#define PS_AUTHFAIL     3       /* user authorization failed */
#define PS_PROTOCOL     4       /* protocol violation */
#define PS_SYNTAX       5       /* command-line syntax error */
#define PS_FOLDER       6       /* local folder I/O woes */
#define PS_ERROR        7       /* some kind of POP3 error condition */
#define PS_UNDEFINED    9       /* something I hadn't thought of */

/*********************************************************************
  function:      POP3_OK
  description:   get the server's response to a command, and return
                 the extra arguments sent with the response.
  arguments:     
    argbuf       buffer to receive the argument string.
    socket       socket to which the server is connected.

  return value:  zero if okay, else return code.
  calls:         SockGets
 *********************************************************************/

static int
POP3_OK (char *argbuf, int socket)
{
  int ok;
  char buf[BUFSIZ];
  char *bufp;

  if (SockGets(socket, buf, sizeof(buf)) == 0) {
    bufp = buf;
    if (*bufp == '+' || *bufp == '-')
      bufp++;
    else
      return(PS_PROTOCOL);

    while (isalpha(*bufp))
      bufp++;
    *(bufp++) = '\0';

    if (strcmp(buf,"+OK") == 0)
      ok = 0;
    else if (strcmp(buf,"-ERR") == 0)
      ok = PS_ERROR;
    else
      ok = PS_PROTOCOL;

    if (argbuf != NULL)
      strcpy(argbuf,bufp);
  }
  else 
    ok = PS_SOCKET;

  return(ok);
}



/*********************************************************************
  function:      POP3_auth
  description:   send the USER and PASS commands to the server, and
                 get the server's response.
  arguments:     
    userid       user's mailserver id.
    password     user's mailserver password.
    socket       socket to which the server is connected.

  return value:  non-zero if success, else zero.
  calls:         SockPrintf, POP3_OK.
 *********************************************************************/

static int
POP3_auth (const char *userid, const char *password, int socket) 
{
  int ok;
  char buf[BUFSIZ];

  SockPrintf(socket,"USER %s\r\n",userid);
  if ((ok = POP3_OK(buf,socket)) == 0) {
    SockPrintf(socket,"PASS %s\r\n",password);
    if ((ok = POP3_OK(buf,socket)) == 0) 
      ;  /* okay, we're approved.. */
    else
      fprintf(stderr,"%s\n",buf);
  }
  else
    fprintf(stderr,"%s\n",buf);
  
  return(ok);
}




/*********************************************************************
  function:      POP3_sendQUIT
  description:   send the QUIT command to the server and close 
                 the socket.

  arguments:     
    socket       socket to which the server is connected.

  return value:  none.
  calls:         SockPuts, POP3_OK.
 *********************************************************************/

static int
POP3_sendQUIT (int socket)
{
  int ok;
  char buf[BUFSIZ];

  SockPuts(socket,"QUIT");
  ok = POP3_OK(buf,socket);
  if (ok != 0)
    fprintf(stderr,"%s\n",buf);

  return(ok);
}



/*********************************************************************
  function:      POP3_sendSTAT
  description:   send the STAT command to the POP3 server to find
                 out how many messages are waiting.
  arguments:     
    count        pointer to an integer to receive the message count.
    socket       socket to which the POP3 server is connected.

  return value:  return code from POP3_OK.
  calls:         POP3_OK, SockPrintf
 *********************************************************************/

static int
POP3_sendSTAT (int *msgcount, int socket)
{
  int ok;
  char buf[BUFSIZ];
  int totalsize;

  SockPrintf(socket,"STAT\r\n");
  ok = POP3_OK(buf,socket);
  if (ok == 0)
    sscanf(buf,"%d %d",msgcount,&totalsize);
  else
    fprintf(stderr,"%s\n",buf);

  return(ok);
}




/*********************************************************************
  function:      POP3_sendRETR
  description:   send the RETR command to the POP3 server.
  arguments:     
    msgnum       message ID number
    socket       socket to which the POP3 server is connected.

  return value:  return code from POP3_OK.
  calls:         POP3_OK, SockPrintf
 *********************************************************************/

static int
POP3_sendRETR (int msgnum, int socket)
{
  int ok;
  char buf[BUFSIZ];

  SockPrintf(socket,"RETR %d\r\n",msgnum);
  ok = POP3_OK(buf,socket);
  if (ok != 0)
    fprintf(stderr,"%s\n",buf);

  return(ok);
}



/*********************************************************************
  function:      POP3_sendDELE
  description:   send the DELE command to the POP3 server.
  arguments:     
    msgnum       message ID number
    socket       socket to which the POP3 server is connected.

  return value:  return code from POP3_OK.
  calls:         POP3_OK, SockPrintF.
 *********************************************************************/

static int
POP3_sendDELE (int msgnum, int socket)
{
  int ok;
  char buf[BUFSIZ];

  SockPrintf(socket,"DELE %d\r\n",msgnum);
  ok = POP3_OK(buf,socket);
  if (ok != 0)
    fprintf(stderr,"%s\n",buf);

  return(ok);
}



/*********************************************************************
  function:      POP3_readmsg
  description:   Read the message content as described in RFC 1225.
  arguments:     
    socket       ... to which the server is connected.
    mboxfd       open file descriptor to which the retrieved message will
                 be written.  
    topipe       true if we're writing to the system mailbox pipe.

  return value:  zero if success else PS_* return code.
  calls:         SockGets.
 *********************************************************************/

static int
POP3_readmsg (int socket, FILE *outf)
{
  char buf[BUFSIZ];
  char *bufp;
  time_t now;
  /* This keeps the retrieved message count for display purposes */
  static int msgnum = 0;  

  /* Unix mail folder format requires the Unix-syntax 'From' header.
     POP3 doesn't send it, so we fake it here. */
  now = time(NULL);
  fprintf(outf, "From POPmail %s", ctime(&now));

  /* read the message content from the server */
  while (1) {
    if (SockGets(socket,buf,sizeof(buf)) < 0)
      return(PS_SOCKET);
    bufp = buf;
    if (*bufp == '.') {
      bufp++;
      if (*bufp == 0)
        break;  /* end of message */
    }

    if (strncmp(bufp, "From ", 5) == 0)
      fputc('>', outf);
    fputs(bufp, outf);
    fputc('\n', outf);
  }

  return(0);
}

/*********************************************************************
  function:      getMail
  description:   retrieve messages from the specified mail server
                 using Post Office Protocol 3.

  arguments:     
    options      fully-specified options (i.e. parsed, defaults invoked,
                 etc).

  return value:  exit code from the set of PS_.* constants defined in 
                 popclient.h
  calls:
 *********************************************************************/

int
getMail (const char *host, const char *userid, const char *password)
{
  struct servent *sp;
  int ok;
  char buf[BUFSIZ];
  int socket;
  int number,count;
  int percent, lastPercent;

  if ((sp = getservbyname("pop3", "tcp")) == NULL) {
    if ((sp = getservbyname("pop", "tcp")) == NULL) {
      fprintf(stderr, "pop/tcp: Unknown service.\n");
      return PS_SOCKET;
    }
  }

  /* open the socket and get the greeting */
  if ((socket = Socket(host, ntohs(sp->s_port))) < 0) {
    fprintf(stderr, "Cannot connect to mail server %s\n", host);
    return PS_SOCKET;
  }

  ok = POP3_OK(buf, socket);
  if (ok != 0) {
    if (ok != PS_SOCKET)
      POP3_sendQUIT(socket);
    SockClose(socket);
    return(ok);
  }

  /* try to get authorized */
  ok = POP3_auth(userid, password, socket);
  if (ok == PS_ERROR)
    ok = PS_AUTHFAIL;
  if (ok != 0)
    goto cleanUp;

  /* find out how many messages are waiting */
  ok = POP3_sendSTAT(&count, socket);
  if (ok != 0) {
    goto cleanUp;
  }

  /* show them how many messages we'll be downloading */
  printf("%s: you have %d mail message%s\n", progname, count,
      (count == 1) ? "" : "s");

  if (count > 0) { 
    /* Open the mailbox file. */
    FILE *outf;
    if ((outf = openMsgFile(0, "Email", "mn")) == NULL)
      goto cleanUp;

    lastPercent = 0;
    for (number = 1;  number <= count;  number++) {
      percent = (number * 100) / count;
      if (percent != lastPercent) {
	printf("%d%%\r", percent);
	fflush(stdout);
	lastPercent = percent;
      }

      ok = POP3_sendRETR(number,socket);
      if (ok != 0)
        goto cleanUp;
        
      ok = POP3_readmsg(socket, outf);
      if (ok != 0)
        goto cleanUp;

      if (!readOnly) {
        ok = POP3_sendDELE(number,socket);
        if (ok != 0)
          goto cleanUp;
      }
    }
    closeMsgFile();

    ok = POP3_sendQUIT(socket);
    if (ok == 0)
      ok = PS_SUCCESS;
    SockClose(socket);
    return(ok);
  }
  else {
    ok = POP3_sendQUIT(socket);
    if (ok == 0)
      ok = PS_NOMAIL;
    SockClose(socket);
    return(ok);
  }

cleanUp:
  if (ok != 0 && ok != PS_SOCKET)
    POP3_sendQUIT(socket);
    
  if (ok == PS_FOLDER || ok == PS_SOCKET) 
    perror("doPOP3: cleanUp");

  return(ok);
}

