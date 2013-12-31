/* $Id: souper.c 1.6 1996/05/18 21:13:54 cthuang Exp $
 *
 * Fetch mail and news using POP3 and NNTP into a SOUP packet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "souper.h"

#ifdef __WIN32__
#include <fcntl.h>
#include <winsock.h>
WSADATA WSAData;
#endif

/* getopt declarations */
#if HAVE_GETOPT_H
#include <getopt.h>
#else
extern int getopt(int argc, char *const *argv, const char *shortopts);
extern char *optarg;
extern int optind;
#endif

/* global data */
char progname[] = "souper";

#ifdef __OS2__
char doIni = 1;		/* if TRUE, read TCPOS2.INI file */
#endif
char *nntpServer;
char *mailGateway;

/* program options */
enum Mode { RECEIVE, SEND, CATCHUP } mode = RECEIVE;
char doMail = 1;
char doNews = 1;
char doXref = 1;
char doSummary = 0;
char doNewGroups = 0;
char readOnly = 0;
long maxBytes = 2048*1024L;
char *homeDir = NULL;
char newsrcFile[FILENAME_MAX];
char killFile[FILENAME_MAX];
int maxLines = 0;
int catchupCount;
char *popServer, *popUser, *popPassword;

/* Try to allocate some memory.  Exit if unsuccessful.
 */
void *
xmalloc (size_t sz)
{
    void *p;

    if ((p = malloc(sz)) == NULL) {
	fputs("out of memory\n", stderr);
	exit(EXIT_FAILURE);
    }
    return p;
}

/* Copy string to allocated memory.  Exit if unsuccessful.
 */
char *
xstrdup (const char *s)
{
    return strcpy((char *)xmalloc(strlen(s)+1), s);
}

static void
setDefaults (void)
{
    char *s;

    if (!homeDir)
	if ((homeDir = getenv("HOME")) == NULL)
	    homeDir = ".";

    if ((s = getenv("NNTPSERVER")) != NULL)
	nntpServer = s;

#ifdef __OS2__
    sprintf(newsrcFile, "%s/newsrc", homeDir);
    sprintf(killFile, "%s/kill", homeDir);
#else
    sprintf(newsrcFile, "%s/.newsrc", homeDir);
    sprintf(killFile, "%s/.kill", homeDir);
#endif
}

static void
usage (void)
{
    fputs("Souper v1.5 - transfer POP3 mail and NNTP news to SOUP\n", stderr);
    fprintf(stderr, "usage: %s [options] [mailhost userid password]\n", progname);
    fputs("  -a       Add new newsgroups to newsrc file\n", stderr);
    fputs("  -c n     Mark every article as read except for the last n in each newsgroup\n", stderr);
    fputs("  -h dir   Set home directory\n", stderr);
#ifdef __OS2__
    fputs("  -i       Do not read Internet Connection for OS/2 settings\n", stderr);
#endif
    fputs("  -k n     Set maximum news packet size in Kbytes\n", stderr);
    fputs("  -l n     Kill articles longer than n lines\n", stderr);
    fputs("  -m       Do not get mail\n", stderr);
    fputs("  -n       Do not get news\n", stderr);
    fputs("  -N file  Set newsrc file\n", stderr);
    fputs("  -K file  Set kill file\n", stderr);
    fputs("  -r       Read only mode.  Do not delete mail or update newsrc\n", stderr);
    fputs("  -s       Send replies\n", stderr);
    fputs("  -u       Create news summary\n", stderr);
    fputs("  -x       Do not process news Xref headers\n", stderr);
    exit(EXIT_FAILURE);
}

static void
parseOptions (int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "ac:h:iK:k:l:mN:nrsux")) != EOF) {
	switch (c) {
	case 'a':
	    doNewGroups = 1;
	    break;
	case 'c':
	    mode = CATCHUP;
	    catchupCount = atoi(optarg);
	    break;
	case 'h':
	    homeDir = optarg;
	    setDefaults();
	    break;
#ifdef __OS2__
	case 'i':
	    doIni = 0;
	    break;
#endif
	case 'K':
	    strcpy(killFile, optarg);
	    break;
	case 'k':
	    maxBytes = atol(optarg) * 1024L;
	    break;
	case 'l':
	    maxLines = atoi(optarg);
	    break;
	case 'm':
	    doMail = 0;
	    break;
	case 'N':
	    strcpy(newsrcFile, optarg);
	    break;
	case 'n':
	    doNews = 0;
	    break;
	case 'r':
	    readOnly = 1;
	    break;
	case 's':
	    mode = SEND;
	    break;
	case 'u':
	    doNews = 1;
	    doSummary = 1;
	    break;
	case 'x':
	    doXref = 0;
	    break;
	default:
	    usage();
	}
    }

}

#ifdef __OS2__
#define INCL_WINSHELLDATA
#include <os2.h>

static void
readTcpIni (void)
{
    HAB hab;
    HINI hini;
    char *etc;
    char buf[BUFSIZ], curConnect[20], host[40], domain[40];

    etc = getenv("ETC");
    if (etc == NULL) {
	fputs("Must set ETC\n", stderr);
	exit(EXIT_FAILURE);
    }
    sprintf(buf, "%s\\TCPOS2.INI", etc);

    hab = WinInitialize(0);
    hini = PrfOpenProfile(hab, buf);
    if (hini == NULLHANDLE) {
	fprintf(stderr, "Cannot open profile %s\n", buf);
	exit(EXIT_FAILURE);
    }

    PrfQueryProfileString(hini, "CONNECTION", "CURRENT_CONNECTION", NULL,
                          curConnect, sizeof(curConnect));

#if 0
    PrfQueryProfileString(hini, curConnect, "HOSTNAME", NULL, host,
	sizeof(host));
    PrfQueryProfileString(hini, curConnect, "DOMAIN_NAME", NULL, domain,
	sizeof(domain));
    sprintf(buf, "%s.%s", host, domain);
    hostName = xstrdup(buf);
#endif

    PrfQueryProfileString(hini, curConnect, "POPSRVR", NULL, buf, sizeof(buf));
    popServer = xstrdup(buf);
    PrfQueryProfileString(hini, curConnect, "POP_ID", NULL, buf, sizeof(buf));
    popUser = xstrdup(buf);
    PrfQueryProfileString(hini, curConnect, "POP_PWD", NULL, buf, sizeof(buf));
    popPassword = xstrdup(buf);

    if (!nntpServer) {
	PrfQueryProfileString(hini, curConnect, "DEFAULT_NEWS", NULL, buf, sizeof(buf));
	nntpServer = xstrdup(buf);
    }

    PrfQueryProfileString(hini, curConnect, "MAIL_GW", NULL, buf, sizeof(buf));
    mailGateway = xstrdup(buf);
#if 0
    PrfQueryProfileString(hini, curConnect, "REPLY_DOMAIN", NULL, buf, sizeof(buf));
    replyDomain = xstrdup(buf);
#endif

    PrfCloseProfile(hini);
    WinTerminate(hab);
}
#endif

int
main (int argc, char **argv)
{
#ifdef __WIN32__ /* Set up to save the old signal */
   typedef void (*Sig_t)(int);
   Sig_t functionCall;
#endif
#if 0
#ifdef __OS2__
    progname = strrchr(argv[0], '\\');
#else
    progname = strrchr(argv[0], '/');
#endif
    if (progname == NULL)
	progname = argv[0];
    else
	++progname;
#endif

#ifdef __WIN32__
{
    int err;
    WORD wVerReq;

    wVerReq = MAKEWORD(1,1);
   
    err = WSAStartup(wVerReq, &WSAData);
    if (err != 0) {
        printf("Help!!!");
        return -1;
    }
}
#endif

    setDefaults();
    parseOptions(argc, argv);
#ifdef __OS2__
    if (doIni)
	readTcpIni();
#endif

    switch (mode) {
    case RECEIVE:
	if (doMail) {
	    if (argc - optind == 3) {
		popServer = argv[optind];
		popUser = argv[optind+1];
		popPassword = argv[optind+2];
	    } else {
#ifndef __OS2__
		usage();
#endif
	    }
	}

	openAreas();
#ifdef __WIN32__
	/* Save the old signal handler. */
        functionCall = signal(SIGINT, closeAreasOnSignal);
#else
	signal(SIGINT, closeAreasOnSignal);
#endif  
	if (doMail) getMail(popServer, popUser, popPassword);
	if (doNews) {
	    if (doSummary) {
		sumNews();
	    } else {
		if (!getNews())
		    exit(EXIT_FAILURE);
	    }
	}
	closeAreas();
	break;

    case SEND:
	if (argc - optind >= 1) {
	    mailGateway = argv[optind];
	}
	sendReply();
	break;

    case CATCHUP:
	catchupNews(catchupCount);
	break;
    }

#ifdef __WIN32__
    signal(SIGINT, functionCall);	/* Restore the old signal handler. */
    WSACleanup();
#endif
    return EXIT_SUCCESS;
}
