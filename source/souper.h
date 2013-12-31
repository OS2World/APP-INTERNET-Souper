/* $Id: souper.h 1.5 1996/05/18 21:15:01 cthuang Exp $
 *
 * souper declarations
 */

/* global data */
extern char progname[];
extern FILE *areasFile;

/* program options */
extern char readOnly;
extern char *homeDir;
extern char newsrcFile[FILENAME_MAX];
extern char killFile[FILENAME_MAX];
extern int maxLines;
extern char doNews;
extern char doXref;
extern char doSummary;
extern char doNewGroups;
extern long maxBytes;

void *xmalloc(size_t sz);
char *xstrdup(const char *s);

void openAreas(void);
void closeAreas(void);
void closeAreasOnSignal(int sig);
FILE *openMsgFile(int number, const char *name, const char *format);
void closeMsgFile(void);
FILE *openIdxFile(int number, const char *name, const char *format);
void closeIdxFile(void);
int writeNewsrc(void);

int getMail(const char *host, const char *userid, const char *password);
int getNews(void);
int sumNews(void);
int catchupNews(int numKeep);
int isHeader(const char *buf, const char *header, size_t len);
char *getHeader(FILE *inf, const char *header);
void sendReply(void);

int readKillFile(void);
int killGroup(const char *name);
int killLine(const char *line);

#if !defined(__OS2__) && !defined(__WIN32__)
#define stricmp strcasecmp
#define strnicmp strncasecmp
#endif
