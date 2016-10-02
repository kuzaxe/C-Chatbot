#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/un.h>
#include <netinet/in.h>

#include "parse.h"
#include "chatsvr.h"
#include "util.h"

#define MESSAGE 256
#define HANDLE 79

/* server struct - chatsvr.c AJR*/
struct server {
    int svrfd;
    char buf[MESSAGE+HANDLE+16];
    int buf_size;
    char *next;
};

/* chatsvr.c AJR code -- Process data from server (similar to myreadline)*/
char *reader(struct server *s) {
    int nbytes;

    if (s->buf_size && s->next)
        memmove(s->buf, s->next, s->buf_size);

    if ((s->next = extractline(s->buf, s->buf_size))) {
        s->buf_size -= (s->next - s->buf);
        return(s->buf);
    }

    nbytes = read(s->svrfd, s->buf + s->buf_size,
        sizeof s->buf - s->buf_size - 1);
    if (nbytes < 0) {
        perror("read()");
    } else if (nbytes == 0) {
    } else {
        s->buf_size += nbytes;
        if ((s->next = extractline(s->buf, s->buf_size))) {
            s->buf_size -= (s->next - s->buf);
            return(s->buf);
        }

        if (s->buf_size >= MESSAGE+HANDLE) {
            s->buf[s->buf_size] = '\0';
            s->buf_size = 0;
            s->next = NULL;
            return(s->buf);
        }
   }

   return(NULL);
}

int main(int argc, char **argv)
{
    int PORT;
    char *HOST;
    struct server s;
    struct sockaddr_in r;

    /* "Process command-line arguments" * * * * * * * * * * * * * * * * * * */

    if (argc == 1 || argc > 3) {
        fprintf(stderr, "usage: %s host [port]\n", argv[0]);
        return(1);
    } else if (argc == 2) {
        PORT = 1234;
    } else {
        PORT = atoi(argv[2]);
    }
    HOST = argv[1];

    /* lookup.c code -- AJR * * * * * * * * * * * * * * * * * * * * */

    struct hostent *hp;

    if ((hp = gethostbyname(argv[1])) == NULL) {
        fprintf(stderr, "%s: no such host\n", argv[1]);
        return(1);
    }

    if (hp->h_addr_list[0] == NULL || hp->h_addrtype != AF_INET) {
        fprintf(stderr, "%s: not an internet protocol host name\n", argv[1]);
        return(1);
    }

    if ((s.svrfd= socket(AF_INET, SOCK_STREAM, 0)) == -1)     {
       perror("socket()");
       return(1);
    }

    memset(&r, '\0', sizeof r);
    r.sin_family = AF_INET;
    r.sin_port = htons(PORT);
    r.sin_addr = *((struct in_addr *)hp->h_addr);

    if(connect(s.svrfd, (struct sockaddr *)&r, sizeof(struct sockaddr)) == -1) {
        perror("connect()");
        exit(1);
    }

    /* "send handle to server" */
    write(s.svrfd, "Marvin\r\n", 8);

    /* Check if server is the correct one, print welcome statement * * * * */

    s.buf_size = 0;
    s.next = NULL;
    fd_set readfds;

    /* "banner checking: reading function in a loop until it has entire line" */
    while (strcmp(reader(&s), "chatsvr 305975789") != 0 &&
                        s.buf_size <= sizeof "chatsvr 305975789");

    char *server_intro = reader(&s);
    write(1, server_intro, strlen(server_intro));
    write(1, "\r\n", 2);

    /* "check if connected to right server" */
    if (s.buf_size > sizeof "chatsvr 305975789") {
        perror("chat server");
    }

    /* MAIN COMM. LOOP * * * * * * * * * * * * * * * * * * * * * * * * * * **/

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(s.svrfd, &readfds);
        FD_SET(0, &readfds);
        /* "select to choose among server or stdin for next data" */
        select(s.svrfd+1, &readfds, NULL, NULL, NULL);
        if (FD_ISSET(0, &readfds)) {
            /* reading from stdin */
            char msg[MESSAGE];
            /* "stdin can simply be ready with fgets()" */
            fgets(msg, MESSAGE-2, stdin);
            write(s.svrfd, msg, strlen(msg));
        } else if (FD_ISSET(s.svrfd, &readfds)) {
            /* reading from server */
            char *msg = reader(&s);
            if (msg != NULL) {
                /* cut handle from message */
                char handle[HANDLE];
                int i = 0;
                while (msg[i] != ':' && i < HANDLE) {
                    handle[i] = msg[i];
                    i++;
                }
                handle[i] = '\0';

                msg += i+2;

                /* print out message from server onto Marvin page */
                write(1, handle, strlen(handle));
                write(1, ": ", 2);
                write(1, msg, strlen(msg));
                write(1, "\r\n", 2);
                
                if (strncmp(msg, "Hey Marvin,", 11) == 0 || 
                    strncmp(msg, "Hey marvin,", 11) == 0 ||
                    strncmp(msg, "hey marvin,", 11) == 0 || 
                    strncmp(msg, "hey Marvin,", 11) == 0 ) {
                    /* extract math statement*/
                    char *to;
                    to = strndup(msg+11, strlen(msg) - 11);

                    /* calculate math statement */
                    struct expr *pexpr = parse(to);
                    if (pexpr == NULL) {
                        /* invalid statement */
                        write(s.svrfd, "Hey ", 4);
                        write(s.svrfd, handle, strlen(handle));
                        write(s.svrfd, ", I don't like that.\r\n", 21);
                    } else {
                        /* send info to server 'Hey [username], [result]'*/
                        char result[33];
                        sprintf(result, "%d", evalexpr(pexpr));
                        write(s.svrfd, "Hey ", 4);
                        write(s.svrfd, handle, strlen(handle));
                        write(s.svrfd, ", ", 2);
                        write(s.svrfd, result, strlen(result));
                        write(s.svrfd, "\r\n", 2);
                    }
                }
            }
            /* exit if server disconnects */
            if (msg == NULL) {
                printf("Server shut down\n");
                return(0);
            }
        }
    }

    /* close server */
    close(s.svrfd);
    return(0);
}

