#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAXBUFLEN 1024
#define MAX_HTML_LEN 10240

int sendall(int sock, char* buf, int* len)
{
    int total = 0; // bytes we've sent
    int bytesleft = *len; // bytes left to send
    int n;

    while (total < *len) {
        n = send(sock, buf + total, bytesleft, 0);
        if (n == -1) {
            break;
        }
        total += n;
        bytesleft -= n;
    }

    *len = total;
    return n == -1 ? -1 : 0; // -1 on failure, 0 on success
}

int main()
{
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket == -1) {
        perror("socket");
        return 1;
    }
    printf("socket created\n");

    struct sockaddr_in socket_addr;
    socket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socket_addr.sin_port = htons(8000);
    socket_addr.sin_family = AF_INET;

    int addr_len = sizeof(socket_addr);

    int bindErr = bind(tcp_socket, (struct sockaddr*)&socket_addr, addr_len);
    if (bindErr == -1) {
        perror("bind");
        return 1;
    }
    printf("socket bound\n");

    int listenErr = listen(tcp_socket, 10);
    if (listenErr == -1) {
        perror("listen");
        return 1;
    }
    printf("listening\n");

    for (;;) {
        int newosockFD = accept(tcp_socket, (struct sockaddr*)&socket_addr, (socklen_t*)&addr_len);
        if (newosockFD == -1) {
            perror("accept");
            return 1;
        }
        printf("accepting\n");

        struct sockaddr_in client_addr;
        int client_addrlen = sizeof(client_addr);

        int sockname = getsockname(newosockFD, (struct sockaddr*)&client_addr, (socklen_t*)&client_addrlen);
        if (sockname < 0) {
            perror("client name");
            return 1;
        }

        char buf[MAXBUFLEN];
        int valRead = recv(newosockFD, buf, MAXBUFLEN, 0);
        if (valRead == -1) {
            perror("read");
            return 1;
        }

        char method[MAXBUFLEN], uri[MAXBUFLEN], version[MAXBUFLEN];
        sscanf(buf, "%s %s %s", method, uri, version);

        printf("[%s:%u] %s %s %s\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), method, uri, version);

        char* stripped_uri = uri;
        stripped_uri++; // skip leading '/'

        if (strcmp(stripped_uri, "") == 0) {
            strcpy(stripped_uri, "index.html");
        } else {
        }
        printf("URI: %s\n", stripped_uri);

        FILE* f = fopen(stripped_uri, "r");
        if (f == NULL) {
            char notfound[] = "HTTP/1.1 404 Not Found\n";
            int writeErr = send(newosockFD, notfound, strlen(notfound), 0);
            if (writeErr == -1) {
                perror("write");
                return 1;
            }
        } else {
            char ok[] = "HTTP/1.1 200 OK\n"
                        "Content-Type: text/html\n\n";
            int len = strlen(ok);
            sendall(newosockFD, ok, &len);

            // HEADER

            // HTML
            char html[MAX_HTML_LEN + 1];
            size_t newLen = fread(html, sizeof(char), MAX_HTML_LEN, f);
            if (ferror(f) != 0) {
                fputs("Error reading file", stderr);
            } else {
                html[newLen++] = '\0'; /* Just to be safe. */
            }

            len = strlen(html);
            sendall(newosockFD, html, &len);
        }
        fclose(f);
        close(newosockFD);
    }

    return 0;
}
