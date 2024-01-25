/*
 * client.c: A very, very primitive HTTP client.
 *
 * To run, try:
 *      ./client www.cs.technion.ac.il 80 /
 *
 * Sends one HTTP request to the specified HTTP server.
 * Prints out the HTTP response.
 *
 * HW3: For testing your server, you will want to modify this client.
 * For example:
 *
 * You may want to make this multi-threaded so that you can
 * send many requests simultaneously to the server.
 *
 * You may also want to be able to request different URIs;
 * you may want to get more URIs from the command line
 * or read the list from a file.
 *
 * When we test your server, we will be using modifications to this client.
 *
 */

#include "segel.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Send an HTTP request for the specified file
 */
void clientSend(int fd, char *filename) {
    char buf[MAXLINE];
    char hostname[MAXLINE];

    Gethostname(hostname, MAXLINE);

    /* Form and send the HTTP request */
    sprintf(buf, "GET %s HTTP/1.1\n", filename);
    sprintf(buf, "%shost: %s\n\r\n", buf, hostname);
    Rio_writen(fd, buf, strlen(buf));
}

/*
 * Read the HTTP response and print it out
 */
void clientPrint(int fd) {
    rio_t rio;
    char buf[MAXBUF];
    int length = 0;
    int n;

    Rio_readinitb(&rio, fd);

    /* Read and display the HTTP Header */
    n = Rio_readlineb(&rio, buf, MAXBUF);
    while (strcmp(buf, "\r\n") && (n > 0)) {
        printf("Header: %s", buf);
        n = Rio_readlineb(&rio, buf, MAXBUF);

        /* If you want to look for certain HTTP tags... */
        if (sscanf(buf, "Content-Length: %d ", &length) == 1) {
            printf("Length = %d\n", length);
        }
    }

    /* Read and display the HTTP Body */
    n = Rio_readlineb(&rio, buf, MAXBUF);
    while (n > 0) {
        printf("%s", buf);
        n = Rio_readlineb(&rio, buf, MAXBUF);
    }
}

void client(void *info) {
    char **argv = (char **)info;
    char *host = argv[1];
    int port = atoi(argv[2]);
    char *filename = argv[3];

    /* Open a single connection to the specified host and port */
    int clientfd = Open_clientfd(host, port);

    clientSend(clientfd, filename);
    clientPrint(clientfd);

    Close(clientfd);
    exit(0);
}

int main(int argc, char *argv[]) {
    // freopen("/dev/null", "w", stderr); // silent stderr
    printf("client waits..\n");
    sleep(5); // give some time to the server to go up

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> <filename>\n", argv[0]);
        exit(1);
    }
    int request_rate = 30; // number of request in a second
    int MAX_CLIENTS = 100;
    pid_t clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; ++i)
        clients[i] = -1;

    printf("client started\n");
    int clients_count = 0;
    for (int i = 0;; i = (i + 1) % MAX_CLIENTS) {
        int count_running = 0;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clients[i] > 0) {
                if (waitpid(clients[i], NULL, WNOHANG) == 0)
                    count_running++;
                else
                    clients[i] = -1;
            }
        }
        printf("\rCreated: %d, Running: %d                ", clients_count, count_running);
        fflush(stdout);

        if (clients[i] < 0) {
            clients[i] = fork();
            if (clients[i] == 0)
                client(argv);
            else {
                if (clients[i] > 0) {
                    clients_count++;
                }

                usleep(1000000 / request_rate); // convert rate in seconds to cycle length in micro second
            }
        }
    }

    exit(0);
}
