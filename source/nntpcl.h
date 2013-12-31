/* $Id: nntpcl.h 1.4 1996/05/18 21:14:25 cthuang Exp $
 *
 * NNTP client declarations
 */

typedef long ArticleNumber;

extern char *nntpServer;

int nntpConnect(void);
int nntpGroup(int socket, const char *name,
	ArticleNumber *pLo, ArticleNumber *pHi);
int nntpXover(int socket, ArticleNumber lo, ArticleNumber hi);
int nntpNext(int socket, ArticleNumber *pArtNum);
int nntpArticle(int socket, const char *cmd, ArticleNumber artNum, FILE *outf);
int nntpDate(int socket, char *dest);
void nntpClose(int socket);
int nntpPost(int socket, FILE *inf, size_t bytes);
