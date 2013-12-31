/* $Id: smtp.h 1.1 1996/05/18 21:14:49 cthuang Exp $
 *
 * SMTP client declarations
 */
int smtpConnect(void);
void smtpClose(int socket);
int smtpMail(int socket, FILE *inf, size_t bytes);
