/* $Id: news.c 1.6 1996/05/18 21:06:26 cthuang Exp $
 *
 * Get news from NNTP server.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "socket.h"
#include "nntp.h"
#include "nntpcl.h"
#include "souper.h"

/* article number range in the .newsrc file */
typedef struct aRange {
    struct aRange *next;	/* pointer to next */
    ArticleNumber lo, hi;	/* article number range */
} Range;

/* newsgroup entry in the .newsrc file */
typedef struct aNewsrcGroup {
    struct aNewsrcGroup *next;	/* pointer to next */
    char *name;			/* newsgroup name */
    Range *readList;		/* list of read article ranges */
    char subscribed;		/* subscribed flag */
} NewsrcGroup;

static NewsrcGroup *nrcList;	/* list of .newsrc entries. */
static long byteCount;		/* current size of fetched news */
static char killEnabled;	/* kill processing enabled for this group */
static FILE *tmpF;		/* temporary file for article */
static int groupCnt;		/* current group number */

#ifdef __WIN32__
#include <conio.h>
#endif

/* Read the article numbers from a .newsrc line. */

static Range *
getReadList (FILE *nrcFile)
{
    static const char digits[] = "%[0123456789]";
    Range *pLast, *rp, *head;
    ArticleNumber lo, hi;
    int c;
    char *range;
    char buf[20];

    /* Initialize subscription list */
    pLast = NULL;
    head = NULL;

    /* Expect [ \n] */
    c = fgetc(nrcFile);

    while (c != '\n' && c != EOF) {
	/* Expect number */
	if (fscanf(nrcFile, digits, buf) != 1)
	    break;
	lo = atol(buf);

	/* Get space for new list entry */
	rp = (Range *)xmalloc(sizeof(Range));

	/* Expect [-,\n] */
	c = fgetc(nrcFile);
	if (c == '-') {
	    /* Is a range */
	    /* Expect number */
	    if (fscanf(nrcFile, digits, buf) != 1)
		break;
	    hi = atol(buf);

	    rp->lo = lo;
	    rp->hi = hi;

	    if (lo != 1 || hi != 0) {
		/* Reverse them in case they're backwards */
		if (hi < lo) {
		    rp->lo = hi;
		    rp->hi = lo;
		}
	    }

	    /* Expect [,\n] */
	    c = fgetc(nrcFile);
	} else {
	    /* Not a range */
	    rp->lo = rp->hi = lo;
	}

	/* Check if range overlaps last one */
	if (pLast != NULL && rp->lo <= pLast->hi + 1) {
	    /* Combine ranges */
	    if (rp->lo < pLast->lo) pLast->lo = rp->lo;
	    if (rp->hi > pLast->hi) pLast->hi = rp->hi;

	    /* Free old one */
	    free(rp);
	} else {
	    /* No overlap, update pointers */
	    if (pLast == NULL) {
		head = rp;
	    } else {
		pLast->next = rp;
	    }
	    rp->next = NULL;
	    pLast = rp;
	}
    }

    while (c != '\n' && c != EOF)
	c = fgetc(nrcFile);

    return head;
}

/* Read the .newsrc file and point nrcList to list of newsgroups entries.
 * Return TRUE if successful.
 */
static int
readNewsrc (void)
{
    FILE *nrcFile;
    char group_name[BUFSIZ], ch;
    NewsrcGroup *head, *np, *lnp;

    /* lnp points to last entry */
    lnp = NULL;
    head = NULL;

    /* Open it */
    if ((nrcFile = fopen(newsrcFile, "r")) == NULL) {
	fprintf(stderr, "%s: can't open %s\n", progname, newsrcFile);
	return 0;
    }

    /* Read newsgroup entry */
    while (fscanf(nrcFile, "%[^:! \t\n]", group_name) == 1) {
	if (group_name[0] == '\0')
	    break;

	/* Allocate a new entry */
	np = (NewsrcGroup *)xmalloc(sizeof(NewsrcGroup));
	ch = fgetc(nrcFile);
	if (ch == '\n') {
	    /* The user didn't end the line with a colon. */
	    np->subscribed = 1;
	    np->readList = NULL;
	} else {
	    /* Parse subscription list */
	    np->subscribed = (ch == ':');
	    np->readList = getReadList(nrcFile);
	}
	np->name = xstrdup(group_name);
	np->next = NULL;

	/* Add to list */
	if (lnp == NULL) {
	    head = np;
	} else {
	    lnp->next = np;
	}
	lnp = np;
    }

    fclose(nrcFile);
    nrcList = head;
    return 1;
}

/* Write the article numbers for a .newsrc entry. */

static void
putReadList (FILE *fd, Range *head)
{
    while (head != NULL) {
	if (head->lo == head->hi)
	    fprintf(fd, "%ld", head->lo);
	else
	    fprintf(fd, "%ld-%ld", head->lo, head->hi);
	head = head->next;
	if (head != NULL) fputc(',', fd);
    }
    fputc('\n', fd);
}

/* Rewrite the updated .newsrc file. */

int
writeNewsrc (void)
{
    char oldFile[FILENAME_MAX];
    FILE *nrcFile;
    NewsrcGroup *np;

    if (readOnly || !doNews || !nrcList) return 0;

    /* Back up old .newsrc file. */
    sprintf(oldFile, "%s.old", newsrcFile);
    remove(oldFile);
    rename(newsrcFile, oldFile);

    if ((nrcFile = fopen(newsrcFile, "w")) == NULL) {
	fprintf(stderr, "%s: can't write %s\n", progname, newsrcFile);
	return 0;
    }

    for (np = nrcList; np != NULL; np = np->next) {
	fputs(np->name, nrcFile);
	fputc(np->subscribed ? ':' : '!', nrcFile);
	fputc(' ', nrcFile);
	putReadList(nrcFile, np->readList);
    }

    fclose(nrcFile);
    return 1;
}

/* Get first unread article number. */

static ArticleNumber
firstUnread (Range *head, ArticleNumber lo)
{
    if (head == NULL)
	return lo;
    return head->hi + 1;
}

/* Determine if the article number has been read */

static int
isRead (ArticleNumber num, Range *head)
{
    /* Look through the list */
    while (head != NULL) {
	if (num < head->lo) return 0;
	if (num >= head->lo && num <= head->hi) return 1;
	head = head->next;
    }
    return 0;
}

/* Mark article as read. */

static Range *
markRead (ArticleNumber num, Range *head)
{
    Range *rp, *trp, *lrp;

    rp = head;

    /* If num is much lower than lowest range, or the list is
       empty, we need new entry */
    if (rp == NULL || num < rp->lo - 1) {
	trp = (Range *)xmalloc(sizeof(Range));
	trp->lo = trp->hi = num;
	trp->next = rp;
	return trp;
    }

    /* lrp remembers last entry in case we need to add a new entry */
    lrp = NULL;

    /* Find appropriate entry for this number */
    while (rp != NULL) {
	/* Have to squeeze one in before this one? */
	if (num < rp->lo - 1) {
	    trp = (Range *)xmalloc(sizeof(Range));
	    trp->lo = trp->hi = num;
	    trp->next = rp;
	    lrp->next = trp;
	    return head;
	}

	/* One less than entry's lo? */
	if (num == rp->lo - 1) {
	    rp->lo = num;
	    return head;
	}

	/* In middle of range, do nothing */
	if (num >= rp->lo && num <= rp->hi) return head;

	/* One too high, must check if we merge with next entry */
	if (num == rp->hi + 1) {
	    if (rp->next != NULL && num == rp->next->lo - 1) {
		trp = rp->next;
		rp->hi = trp->hi;
		rp->next = trp->next;
		free(trp);
		return head;
	    } else {
		/* No merge */
		rp->hi = num;
		return head;
	    }
	}

	lrp = rp;
	rp = rp->next;
    }

    /* We flew off the end and need a new entry */
    trp = (Range *)xmalloc(sizeof(Range));
    trp->lo = trp->hi = num;
    trp->next = NULL;
    lrp->next = trp;

    return head;
}

/* Sanity fixes to the read article number list */

static Range *
fixReadList (NewsrcGroup *np, ArticleNumber lo, ArticleNumber hi)
{
    Range *head, *rp1, *rp2, *rp3;

    head = np->readList;
    if (head != NULL) {
	/* Go to last entry in list. */
	for (rp1 = head; rp1->next != NULL; rp1 = rp1->next)
	    ;
	if (rp1->hi > hi) {
	    /* The highest read article number is greater than the highest
	     * available article number.
	     */
#if 0
	    printf("%s: Somebody reset %s -- assuming nothing read.\n",
		progname, np->name);
#endif

	    /* Mark everything as read. */
	    head->lo = 1;
	    head->hi = hi;

	    /* Free the rest */
	    rp2 = head->next;
	    while (rp2 != NULL) {
		rp3 = rp2->next;
		free(rp2);
		rp2 = rp3;
	    }

#if 0
	    /* If lowest available article is 1, then leave read list empty,
	     * otherwise when group is reset, the first article would be
	     * skipped.
	     */
	    if (lo <= 1) {
		free(head);
		return NULL;
	    }
#endif
	    head->next = NULL;
	    return head;
	}

	/* Walk through the list and eliminate ranges lower than the lowest
	 * available article
	 */
	rp1 = head;
	while (rp1 != NULL) {
	    /* If the lowest available article falls within or is less than
	     * this range, all the rest of the ranges are unnecessary.
	     */
	    if (rp1->lo > lo || rp1->hi >= lo) {
		rp1->lo = 1;
		if (rp1->hi < lo) rp1->hi = lo - 1;

		/* Free the rest. */
		rp2 = head;
		while (rp2 != rp1) {
		    rp3 = rp2->next;
		    free(rp2);
		    rp2 = rp3;
		}
		return rp1;
	    }
	    rp1 = rp1->next;
	}

	/* All the ranges are less than the lowest available article.
	 * Remove them.
	 */
	while (head != NULL) {
	    rp3 = head->next;
	    free(head);
	    head = rp3;
	}
    }

    /* If lowest available article is 1, then leave read list empty,
     * otherwise when a new group is started, the first article would
     * be skipped.
     */
    if (lo <= 1)
	return NULL;

    /* Make one new entry marking everything up to the lowest available
     * article as read.
     */
    head = (Range *)xmalloc(sizeof(Range));
    head->lo = 1;
    head->hi = (lo > 0) ? (lo - 1) : 0;
    head->next = NULL;
    return head;
}

/* Process an Xref line. */

static void
processXref (char *s)
{
    char *c, *p, name[FILENAME_MAX];
    ArticleNumber num;
    NewsrcGroup *np;

    /* Skip the host field */
    c = strtok(s, " \t");
    if (c == NULL) return;

    /* Look through the rest of the fields */
    while ((c = strtok(NULL, " \t")) != NULL) {
	/* Replace : with space. */
	if ((p = strchr(c, ':')) != NULL)
	    *p = ' ';

	if (sscanf(c, "%s %ld", name, &num) == 2) {
	    /* Find .newsrc entry for this group */
	    for (np = nrcList; np != NULL; np = np->next) {
		if (stricmp(np->name, name) == 0) {
		    /* Mark as read */
		    np->readList = markRead(num, np->readList);
		    break;
		}
	    }
	}
    }
}

/* Fetch article from news server to temporary file.
 * Return TRUE if the article was available.
 */
static int
requestArticle (int socket, const char *artNum)
{
    char buf[BUFSIZ], *bufp;
    char gotXref;

    /* Request article. */
    SockPrintf(socket, "ARTICLE %s\r\n", artNum);
    if (SockGets(socket, buf, sizeof(buf)) < 0)
	return 0;

    if (buf[0] == CHAR_FATAL) {	/* Fatal error */
	fprintf(stderr, "%s\n", buf);
	exit(EXIT_FAILURE);
    }

    if (buf[0] != CHAR_OK) {
#ifdef DEBUG
	printf("%s: article number %s is unavailable\n", progname, artNum);
	printf("%s: %s\n", progname, buf);
#endif
	return 0;
    }

    gotXref = 0;

    /* Get lines of article head. */
    while (SockGets(socket, buf, sizeof(buf)) == 0) {
	bufp = buf;
	if (buf[0] == '.') {
	    if (*(++bufp) == '\0')
		break;
	}

	fputs(bufp, tmpF);
	fputc('\n', tmpF);
	if (*bufp == '\0')
	    break;

	if (doXref && !gotXref && strnicmp(bufp, "Xref: ", 6) == 0) {
	    processXref(bufp+6);
	    gotXref = 1;
	}
    }

    /* Retrieve article body. */
    while (SockGets(socket, buf, sizeof(buf)) == 0) {
	bufp = buf;
	if (buf[0] == '.') {
	    if (*(++bufp) == '\0')
		break;
	}

	fputs(bufp, tmpF);
	fputc('\n', tmpF);
    }

    return 1;
}

/* Copy article from temporary file to .MSG file.
 * Return TRUE if article was copied.
 */
static int
writeArticle (FILE *msgF)
{
    long artSize;
    unsigned toRead, wasRead;
    char buf[BUFSIZ];

    /* Get article size. */
    artSize = ftell(tmpF);
    if (artSize == 0)
	return 0;	/* Skip empty articles */

    /* Update packet size.  Include size of rnews line. */
    byteCount += artSize + 14;

    /* Write "rnews" line */
    fprintf(msgF, "#! rnews %ld\n", artSize);

    /* Copy article body. */
    fseek(tmpF, 0L, SEEK_SET);
    while (artSize > 0) {
	toRead = (artSize < sizeof(buf)) ? (unsigned)artSize : sizeof(buf);
	wasRead = fread(buf, sizeof(char), toRead, tmpF);
	if (wasRead == 0) {
	    perror("read article");
	    return 0;
	}
	if (fwrite(buf, sizeof(char), wasRead, msgF) != wasRead) {
	    perror("write article");
	    return 0;
	}
	artSize -= wasRead;
    }

    return 1;
}

/* Get the article and write it to the file stream.
 * Return TRUE if the article was available.
 */
static int
getArticle (int socket, ArticleNumber artNum, FILE *msgF)
{
    char buf[BUFSIZ], *bufp;
    char gotXref, killed;

    /* Get article to temporary file. */
    fseek(tmpF, 0L, SEEK_SET);

    if (killEnabled) {
	/* Request article head. */
	SockPrintf(socket, "HEAD %ld\r\n", artNum);
	if (SockGets(socket, buf, sizeof(buf)) < 0) {
	    return 0;
	}

	if (buf[0] == CHAR_FATAL) {	/* Fatal error */
	    fprintf(stderr, "%s\n", buf);
	    exit(EXIT_FAILURE);
	}

	if (buf[0] != CHAR_OK) {
#ifdef DEBUG
	    printf("%s: article number %ld is unavailable\n", progname, artNum);
	    printf("%s: %s\n", progname, buf);
#endif
	    return 0;
	}

	killed = 0;
	gotXref = 0;

	/* Get lines of article head. */
	while (SockGets(socket, buf, sizeof(buf)) == 0) {
	    bufp = buf;
	    if (buf[0] == '.') {
		if (*(++bufp) == '\0')
		    break;
	    }

	    fputs(bufp, tmpF);
	    fputc('\n', tmpF);

	    if (killEnabled && !killed)
		if (killLine(bufp)) {
		    printf("%s: killed %s\n", progname, bufp);
		    killed = 1;
		}

	    if (doXref && !gotXref && strnicmp(bufp, "Xref: ", 6) == 0) {
		processXref(bufp+6);
		gotXref = 1;
	    }
	}

	/* Don't process anymore if article was killed. */
	if (killed) {
	    return 1;
	}

	/* Put empty line separating head from body. */
	fputc('\n', tmpF);

	/* Retrieve article body. */
	if (!nntpArticle(socket, "BODY", artNum, tmpF))
	    return 0;

    } else {
	sprintf(buf, "%ld", artNum);
        if (!requestArticle(socket, buf))
	    return 0;
    }

    writeArticle(msgF);
    return 1;
}

/* Get articles from the newsgroup.
 * Return TRUE if successful.
 */
static int
getGroup (int socket, NewsrcGroup *np, int areaNum)
{
    ArticleNumber lo, hi, first, artNum, nextNum, j, n;
    int percent, lastPercent;
    char gotArt;
    FILE *msgf;

    /* Select group name from news server. */
    if (!nntpGroup(socket, np->name, &lo, &hi)) {
	np->subscribed = 0;	/* Unsubscribe from invalid group. */
	return 0;
    }

    killEnabled = killGroup(np->name);
    msgf = openMsgFile(areaNum, np->name, "un");

    /* Fix the read article number list. */
    np->readList = fixReadList(np, lo, hi);

    first = firstUnread(np->readList, lo);
    n = hi - first + 1;
    if (n < 0) n = 0;
    printf("%s: %4d unread article%c in %s\n", progname, n,
	(n == 1) ? ' ' : 's', np->name);
    lastPercent = 0;

    /* Look through unread articles */
    gotArt = 0;
    artNum = first;
    while (artNum <= hi) {
#ifdef DEBUG
	printf("%d\r", artNum);
#else
	percent = ((artNum - first + 1) * 100) / n;
	if (percent != lastPercent) {
	    printf("%d%%\r", percent);
	    fflush(stdout);
	    lastPercent = percent;
	}
#endif

	if (isRead(artNum, np->readList)) {
	    ++artNum;
	} else {
	    /* Fetch the article */
	    if (getArticle(socket, artNum, msgf)) {
		/* Mark as read */
		np->readList = markRead(artNum++, np->readList);
		gotArt = 1;
	    } else if (gotArt) {
		/* Article not available.  Look for next available article. */
		nextNum = hi + 1;
    		while (nntpNext(socket, &j)) {
		    if (j > artNum) {
			nextNum = j;
			break;
		    }
#ifdef DEBUG
		    printf("%d\r", j);
#endif
		}

		/* Mark all article numbers to next article as read. */
		while (artNum < nextNum) {
		    np->readList = markRead(artNum++, np->readList);
		}
	    } else {
		np->readList = markRead(artNum++, np->readList);
	    }

	    /* Check if too many blocks already */
	    if (maxBytes > 0 && byteCount >= maxBytes) {
		printf("%s: maximum packet size exceeded\n", progname);
		break;
	    }

#ifdef __WIN32__
            {
                int skip = 0;
                /* Check if they hit ^S (Skip) */
                while (kbhit()) {
                    if (getch() == 0x13) skip = 1;
                }
                if (skip == 1) break;
            }
#endif      
	}
    }

    closeMsgFile();
    return 1;
}

/* Send mail listing new newsgroups. */

static void
getNewGroups (int socket)
{
    static char mailMsgFile[] = "0000000.MSG";
    char date[80], path[FILENAME_MAX], buf[BUFSIZ], *p;
    char openedMail, gotMail;
    FILE *dateF, *msgF;
    time_t now;
    NewsrcGroup *np, *last;

    /* Get current date/time from news server. */
    if (!nntpDate(socket, buf)) {
	/* News server doesn't support the DATE extension.
	 * Get time from the local system.
	 */
	now = time(NULL);
	strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", gmtime(&now));
    }

    /* Get last date/time we checked for new newsgroups. */
#ifdef __OS2__
    sprintf(path, "%s/newstime", homeDir);
#else
    sprintf(path, "%s/.newstime", homeDir);
#endif
    if ((dateF = fopen(path, "r+")) == NULL) {
	/* This is probably the first we checked for new newsgroups.
	 * Just save the current date/time and return.
	 */
	if ((dateF = fopen(path, "w")) != NULL) {
	    fputs(buf, dateF);
	    fputc('\n', dateF);
	    fclose(dateF);
	}
	return;
    }
    fgets(date, sizeof(date), dateF);

    /* Save current date/time. */
    fseek(dateF, 0L, SEEK_SET);
    fputs(buf, dateF);
    fputc('\n', dateF);
    fclose(dateF);

    /* Request new newsgroups. */
    SockPrintf(socket, "NEWGROUPS %-6.6s %-6.6s GMT\r\n", date+2, date+8);
    if (SockGets(socket, buf, sizeof(buf)) < 0) {
	return;
    }

    if (buf[0] == CHAR_FATAL) {	/* Fatal error */
	fprintf(stderr, "%s\n", buf);
	exit(EXIT_FAILURE);
    }

    if (buf[0] != CHAR_OK) {
	return;
    }

    openedMail = 0;
    while (SockGets(socket, buf, sizeof(buf)) == 0) {
	if (buf[0] == '.')
	    break;
	if ((p = strchr(buf, ' ')) != NULL)
	    *p = '\0';

	/* Scan to see if we know about this one. */
	for (last = NULL, np = nrcList; np != NULL; last = np, np = np->next)
	    if (strcmp(buf, np->name) == 0)
		break;
	if (np != NULL)
	    continue;

	/* Allocate a new entry */
	np = (NewsrcGroup *)xmalloc(sizeof(NewsrcGroup));
	np->subscribed = 0;
	np->readList = NULL;
	np->name = xstrdup(buf);
	np->next = NULL;
	last->next = np;

	if (!openedMail) {
	    char msgDate[80];

	    /* Open message file. */
	    if ((msgF = fopen(mailMsgFile, "r+b")) == NULL) {
		if ((msgF = openMsgFile(0, "Email", "mn")) == NULL)
		    return;
		gotMail = 0;
	    } else {
		fseek(msgF, 0L, SEEK_END);
		gotMail = 1;
	    }

	    /* Write message header. */
	    now = time(NULL);
	    fprintf(msgF, "From POPmail %s", ctime(&now));

	    strftime(msgDate, sizeof(msgDate), "Date: %a, %d %b %Y %T %Z\n",
		     localtime(&now));
	    fputs(msgDate, msgF);

	    fputs("To: SouperUser\n", msgF);
	    fputs("From: Souper\n", msgF);
	    fprintf(msgF,
		"Subject: New newsgroups since %-4.4s-%-2.2s-%-2.2s\n\n",
		(date), (date+4), (date+6));

	    openedMail = 1;
	}

	fputs(buf, msgF);
	fputc('\n', msgF);
    }

    if (openedMail) {
	if (gotMail)
	    fclose(msgF);
	else
	    closeMsgFile();
    }
}

static NewsrcGroup *
findGroupByName (const char *name)
{
    NewsrcGroup *np;

    for (np = nrcList; np != NULL; np = np->next) {
	if (stricmp(np->name, name) == 0)
	    return np;
    }
    return NULL;
}

static void
processSendme (int socket, FILE *cmdF)
{
    NewsrcGroup *np;
    FILE *msgF;
    ArticleNumber lo, hi;
    int c;
    char buf[BUFSIZ];
	    
    /* Read newsgroup name. */
    if (fscanf(cmdF, "%s", buf) != 1) {
	fgets(buf, sizeof(buf), cmdF);
	return;
    }

    np = findGroupByName(buf);
    if (np == NULL) {
	fprintf(stderr, "%s: %s not in newsrc\n", progname, buf);
	fgets(buf, sizeof(buf), cmdF);
	return;
    }

    /* Select group name from news server. */
    if (!nntpGroup(socket, np->name, &lo, &hi)) {
	fgets(buf, sizeof(buf), cmdF);
	return;
    }

    msgF = openMsgFile(++groupCnt, np->name, "un");

    c = fgetc(cmdF);
    while (c != EOF && c != '\r' && c != '\n') {
	if (fscanf(cmdF, "%[^ \t\r\n]", buf) != 1) {
	    closeMsgFile();
	    fgets(buf, sizeof(buf), cmdF);
	    return;
	}

	/* Get article to temporary file. */
	fseek(tmpF, 0L, SEEK_SET);
	if (requestArticle(socket, buf)) {
	    writeArticle(msgF);
	} else {
	    printf("%s: article not available: %s: %s\n", progname, np->name,
		   buf);
	}

	c = fgetc(cmdF);
    }

    closeMsgFile();
}

/* Process command file containing sendme commands.
*/
static void
processCommands (int socket, FILE *cmdF)
{
    char buf[BUFSIZ];

    while (fscanf(cmdF, "%s", buf) == 1) {
	if (stricmp(buf, "sendme") == 0)
	    processSendme(socket, cmdF);
	else
	    fgets(buf, sizeof(buf), cmdF);
    }
}

/* If a COMMANDS file exists in the current directory, fetch the articles
 * specified by the sendme commands in the file, otherwise fetch unread
 * articles from newsgroups listed in the newsrc file.
 */
int
getNews (void)
{
    static const char cmdFile[] = "COMMANDS";
    FILE *cmdF;
    NewsrcGroup *np;
    int socket;

    /* Read .newsrc file */
    if (!readNewsrc()) return 0;

    /* Read kill file. */
    readKillFile();

    /* Open NNTP connection. */
    if ((socket = nntpConnect()) < 0)
	return 0;

    /* Check for new newsgroups. */
    if (doNewGroups)
	getNewGroups(socket);

    tmpF = tmpfile();
    byteCount = 0;
    groupCnt = 0;

    if ((cmdF = fopen(cmdFile, "rb")) != NULL) {
	processCommands(socket, cmdF);
	fclose(cmdF);
	remove(cmdFile);
    } else {
	/* For each newsgroup in .newsrc file */
	for (np = nrcList; np != NULL; np = np->next) {
	    if (np->subscribed) {
		getGroup(socket, np, ++groupCnt);
		if (maxBytes > 0 && byteCount >= maxBytes)
		    break;
	    }
	}
    }

    fclose(tmpF);
    nntpClose(socket);
    writeNewsrc();
    return 1;
}

/* Return next field in record. */

static char *
nextField (char **ppCur)
{
    char *pEnd;
    char *pStart = *ppCur;

    if ((pEnd = strchr(pStart, '\t')) != NULL) {
	*pEnd++ = '\0';
	*ppCur = pEnd;
    }
    return pStart;
}

/* Create summary of articles in the newsgroup.
 * Return TRUE if successful.
 */
static int
sumGroup (int socket, NewsrcGroup *np, int areaNum)
{
    ArticleNumber lo, hi, first, n, artNum;
    FILE *idxF;
    char buf[2048], *cur, *s;

    /* Select group name from news server. */
    if (!nntpGroup(socket, np->name, &lo, &hi))
	return 0;

    /* Fix up the read article number list */
    np->readList = fixReadList(np, lo, hi);
    first = firstUnread(np->readList, lo);

    n = hi - first + 1;
    if (n < 0) n = 0;
    printf("%s: %4d unread article%c in %s\n", progname, n,
	(n == 1) ? ' ' : 's', np->name);
    if (first > hi)
	return 0;

    /* Request overview. */
    if (!nntpXover(socket, first, hi))
	return 0;
    idxF = openIdxFile(areaNum, np->name, "ic");

    while (SockGets(socket, buf, sizeof(buf)) == 0) {
	if (buf[0] == '.')
	    break;
	cur = buf;

	artNum = atol(nextField(&cur));
	np->readList = markRead(artNum, np->readList);

	fputc('\t', idxF);			/* offset */
	s = nextField(&cur);
	fputs(s, idxF); fputc('\t', idxF);	/* Subject */
	s = nextField(&cur);
	fputs(s, idxF); fputc('\t', idxF);	/* From */
	s = nextField(&cur);
	fputs(s, idxF); fputc('\t', idxF);	/* Date */
	s = nextField(&cur);
	fputs(s, idxF); fputc('\t', idxF);	/* Message-ID */
	s = nextField(&cur);
	fputs(s, idxF); fputc('\t', idxF);	/* References */
	s = nextField(&cur);
	fputc('0', idxF); fputc('\t', idxF);	/* bytes */
	s = nextField(&cur);
	fputs(s, idxF); fputc('\t', idxF);	/* lines */
	fprintf(idxF, "%ld\n", artNum);		/* selector */
    }

    closeIdxFile();
    return 1;
}

/* Create news summary. */

int
sumNews (void)
{
    NewsrcGroup *np;
    int socket;

    /* Read .newsrc file */
    if (!readNewsrc()) return 0;

    /* Open NNTP connection. */
    if ((socket = nntpConnect()) < 0) return 0;

    groupCnt = 0;

    /* For each newsgroup in .newsrc file */
    for (np = nrcList; np != NULL; np = np->next) {
	if (np->subscribed) {
	    sumGroup(socket, np, ++groupCnt);
	}
    }

    nntpClose(socket);
    writeNewsrc();
    return 1;
}

/* Catch up in subscribed newsgroups. */

int
catchupNews (int numKeep)
{
    NewsrcGroup *np;
    Range *head, *rp, *pNext;
    int socket;
    ArticleNumber lo, hi;

    /* Read .newsrc file */
    if (!readNewsrc()) return 0;

    /* Open NNTP connection. */
    if ((socket = nntpConnect()) < 0) return 0;

    /* For each newsgroup in .newsrc file */
    for (np = nrcList; np != NULL; np = np->next) {
	if (np->subscribed) {
	    /* select group name from news server */
	    if (nntpGroup(socket, np->name, &lo, &hi)) {
		hi -= numKeep;
		lo = firstUnread(np->readList, lo);
		if (hi < lo)
		    hi = (lo > 0) ? (lo - 1) : 0;

		/* Mark article numbers 1 to hi as read. */
		head = np->readList;
		if (head == NULL) {
		    head = (Range *)xmalloc(sizeof(Range));
		    head->next = NULL;
		    np->readList = head;
		}
		head->lo = 1;
		head->hi = hi;
		rp = head->next;
		head->next = NULL;

		/* Free rest of list */
		while (rp != NULL) {
		    pNext = rp->next;
		    free(rp);
		    rp = pNext;
		}
	    }
	}
    }

    nntpClose(socket);
    writeNewsrc();
    return 1;
}
