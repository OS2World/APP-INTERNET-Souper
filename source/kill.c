/* $Id: kill.c 1.3 1996/05/18 21:11:10 cthuang Exp $
 *
 * Kill file processing
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "regexp.h"
#include "souper.h"

/* kill regular expression */
typedef struct aKillExp {
    struct aKillExp *next;	/* next in list */
    regexp *re;			/* compiled regular expression */
} KillExp;

/* kill file entry for a newsgroup */
typedef struct aGroupKill {
    struct aGroupKill *next;	/* next in list */
    char *name;			/* newsgroup name */
    KillExp *expList;		/* list of kill expressions */
} GroupKill;

static GroupKill *globalKill;		/* global kill */
static GroupKill *groupKillList;	/* list of group specific kills */
static GroupKill *curGroupKill;		/* current group kill */

/* Function called for an error when compiling regular expression. */

void
regerror (const char *msg)
{ }

/* Copy string.  Return pointer to '\0' at end of destination string. */

char *
stpcpy (char *dest, const char *src)
{
    while ((*dest++ = *src++) != '\0')
	;
    return dest - 1;
}

#ifndef HAVE_STRLWR
char *
strlwr (char *str)
{
    char *p = str;
    while (*p != '\0') {
	if (isascii(*p))
	    *p = tolower(*p);
	++p;
    }
    return str;
}
#endif

/* Read kill file expression.
 */
static KillExp *
readKillExp (FILE *inf, const char *searchIn)
{
    char searchFor[BUFSIZ], exp[BUFSIZ], *p;
    KillExp *result;

    if (!fgets(searchFor, sizeof(searchFor), inf) || searchFor[0] == '\n')
	return NULL;
    if ((p = strchr(searchFor, '\n')) != NULL)
	*p = '\0';

    if (stricmp(searchIn, "header") == 0) {
	strcpy(exp, searchFor);
    } else {
	p = exp;
	*p++ = '^';
	p = stpcpy(p, searchIn);
	p = stpcpy(p, ":.*");
	p = stpcpy(p, searchFor);
	*p = '\0';
    }

    result = (KillExp *)xmalloc(sizeof(KillExp));
    result->next = NULL;
    strlwr(exp);
    result->re = regcomp(exp);
    return result;
}

/* Read kill file and compile regular expressions.
 * Return TRUE if successful.
 */
int
readKillFile (void)
{
    char buf[BUFSIZ], name[FILENAME_MAX], searchIn[FILENAME_MAX];
    FILE *inf;
    GroupKill *pGroup, *pLastGroup;
    KillExp *pExp, *pLastExp;
    char ok;

    globalKill = groupKillList = NULL;

    if ((inf = fopen(killFile, "r")) == NULL) {
	return 0;
    }

    pLastGroup = NULL;
    ok = 1;

    /* Read newsgroup name. */
    while (ok && fscanf(inf, "%s", name) == 1) {
	/* Read opening brace. */
	fscanf(inf, "%s", buf);
	if (buf[0] != '{' || buf[1] != '\0') {
	    ok = 0;
	    break;
	}

	if (stricmp(name, "all") == 0) {
	    /* Allocate global kill entry. */
	    if (globalKill == NULL)
		globalKill = (GroupKill *)xmalloc(sizeof(GroupKill));
	    pGroup = globalKill;
	} else {
	    /* Allocate group kill entry. */
	    pGroup = (GroupKill *)xmalloc(sizeof(GroupKill));
	    pGroup->name = xstrdup(name);
	    pGroup->next = NULL;

	    if (pLastGroup == NULL)
		groupKillList = pGroup;
	    else
		pLastGroup->next = pGroup;
	    pLastGroup = pGroup;
	}
	pGroup->expList = NULL;

	/* Read kill expressions until closing brace. */
	pLastExp = NULL;
	while (fscanf(inf, "%s ", searchIn) == 1) {
	    if (searchIn[0] == '}' && searchIn[1] == '\0')
		break;
	    if ((pExp = readKillExp(inf, searchIn)) == NULL) {
		ok = 0;
		break;
	    }

	    if (pLastExp == NULL)
		pGroup->expList = pExp;
	    else
		pLastExp->next = pExp;
	    pLastExp = pExp;
	}
    }
    fclose(inf);

    if (!ok)
	fprintf(stderr, "%s: error in kill file %s\n", progname, killFile);
    return ok;
}

/* Set current group kill.
 * Return TRUE if global kills exist or specific group kill exists.
 */
int
killGroup (const char *name)
{
    GroupKill *p;

    for (p = groupKillList; p != NULL; p = p->next) {
	if (stricmp(p->name, name) == 0) {
	    curGroupKill = p;
	    return 1;
	}
    }
    curGroupKill = NULL;
    return globalKill != NULL || maxLines > 0;
}

/* Check if line matches any expression in the group kill entry.
 * Return TRUE if article should be killed.
 */
static int
matchLine (GroupKill *pGroup, const char *buf)
{
    KillExp *pExp;

    if (pGroup == NULL)
	return 0;

    for (pExp = pGroup->expList; pExp != NULL; pExp = pExp->next) {
	if (regexec(pExp->re, buf))
	    return 1;
    }
    return 0;
}

/* Check if line matches kill criteria.
 * Return TRUE if article should be killed.
 */
int
killLine (const char *line)
{
    char buf[BUFSIZ];

    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    strlwr(buf);

    if (maxLines > 0 && strncmp(buf, "lines: ", 7) == 0) {
	if (atoi(buf+7) > maxLines)
	    return 1;
    }

    if (matchLine(globalKill, buf))
	return 1;
    return matchLine(curGroupKill, buf);
}
