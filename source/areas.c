/* $Id: areas.c 1.4 1996/05/18 21:06:26 cthuang Exp $
 *
 * SOUP AREAS file management
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "souper.h"

static FILE *areasF, *aFile;
static char msgFile[FILENAME_MAX];
static int aNumber;
static const char *aName, *aFormat;

/* state of AREAS file
 * 0	AREAS file closed
 * 1	AREAS file open
 * 2	.MSG/.IDX file open
 */
static sig_atomic_t state = 0;

/* Create AREAS file. */

void
openAreas (void)
{
    static char areasFile[] = "AREAS";

    if ((areasF = fopen(areasFile, "wb")) == NULL) {
	perror(areasFile);
	exit(EXIT_FAILURE);
    }
    ++state;
}

/* Create message file for the group. */

FILE *
openMsgFile (int number, const char *name, const char *format)
{
    aNumber = number;
    aName = name;
    aFormat = format;

    sprintf(msgFile, "%07d.MSG", number);
    aFile = fopen(msgFile, (number == 0) ? "ab" : "wb");
    ++state;

    return aFile;
}

/* Close msg file.  If empty, remove it, otherwise add entry to AREAS file. */

void
closeMsgFile (void)
{
    long offset;

    offset = ftell(aFile);
    fclose(aFile);
    --state;
    if (offset == 0) {
	remove(msgFile);
    } else {
	fprintf(areasF, "%07d\t%s\t%s\n", aNumber, aName, aFormat);
    }
}

/* Create index file for the group. */

FILE *
openIdxFile (int number, const char *name, const char *format)
{
    aNumber = number;
    aName = name;
    aFormat = format;

    sprintf(msgFile, "%07d.IDX", number);
    aFile = fopen(msgFile, "wb");
    ++state;

    return aFile;
}

/* Close index file.  If empty, remove it, otherwise add entry to AREAS file. */

void
closeIdxFile (void)
{
    closeMsgFile();
}

/* Close AREAS file. */

void
closeAreas (void)
{
    fclose(areasF);
    --state;
}

/* Close areas file on signal. */

void
closeAreasOnSignal (int sig)
{
    switch (state) {
    case 2:
	closeMsgFile();
	/* FALLTHRU */
    case 1:
	closeAreas();
	writeNewsrc();
	break;
    }
    exit(EXIT_FAILURE);
}
