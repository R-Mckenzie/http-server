#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAXBUFLEN 1024
#define MAX_HTML_LEN 10240
#define BUFFER_SIZE 104857600

const char* file_extension(const char* filename)
{
    const char* ext = strrchr(filename, '.');
    if (!ext || ext == filename) {
        return "";
    } else {
        return ext + 1;
    }
}

const char* mime_type(const char* ext)
{
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) {
        return "text/html";
    } else if (strcasecmp(ext, "css") == 0) {
        return "text/css";
    } else if (strcasecmp(ext, "txt") == 0) {
        return "text/plain";
    } else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg")) {
        return "image/jpeg";
    } else if (strcasecmp(ext, "png") == 0) {
        return "image/png";
    } else {
        return "application/octet-stream";
    }
}

void build_http_response(const char* file_name, const char* filetype, char* response, size_t* response_len)
{
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        snprintf(response, BUFFER_SIZE,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "404 Not Found");
        *response_len = strlen(response);
        return;
    }

    char* header = (char*)malloc(BUFFER_SIZE * sizeof(char));
    snprintf(header, BUFFER_SIZE,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "\r\n",
        mime_type(filetype));

    struct stat file_stat;
    fstat(fd, &file_stat);
    off_t filesize = file_stat.st_size;

    *response_len = 0;
    memcpy(response, header, strlen(header));
    *response_len += strlen(header);

    ssize_t bytes_read;
    while ((bytes_read = read(fd, response + *response_len, BUFFER_SIZE - *response_len)) > 0) {
        *response_len += bytes_read;
    }
    free(header);
    close(fd);
}

int sendall(int sock, char* buf, size_t* len)
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

        if (strncmp(stripped_uri, "", 1) == 0) {
            strlcpy(stripped_uri, "index.html", sizeof("index.html"));
        }
        printf("URI: %s\n", stripped_uri);

        char* response = (char*)malloc(BUFFER_SIZE * 2 * sizeof(char));
        size_t response_len;
        build_http_response(stripped_uri, file_extension(stripped_uri), response, &response_len);
        sendall(newosockFD, response, &response_len);
        free(response);
        close(newosockFD);
    }

    return 0;
}
