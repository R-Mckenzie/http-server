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

#define MAXBUFLEN (50 * 1024) // 50kb

const char* file_extension(const char* filename);
const char* mime_type(const char* ext);
void build_http_response(const char* file_name, const char* filetype, char* response, size_t* response_len);
int sendall(int sock, char* buf, size_t* len);

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

        // READ REQUEST =====
        char request[MAXBUFLEN];
        memset(request, 0, MAXBUFLEN);
        int valRead = recv(newosockFD, request, MAXBUFLEN, 0);
        if (valRead == -1) {
            perror("read");
            return 1;
        }

        char method[10], uri[MAXBUFLEN], version[10];
        sscanf(request, "%s %s %s", method, uri, version);
        printf("\n[%s:%u] %s %s %s\n\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), method, uri, version);

        char request_string[strlen(request)];
        strcpy(request_string, request);
        int req_len = strlen(request_string);
        char *current_header, *body;
        int header_index = 0;
        char header_names[100][1024], header_values[100][MAXBUFLEN];
        int content_length = 0;
        int parse_stage = 0; // 0 = req line, 1 = headers, 2 = body

        for (int i = 0; i < req_len; i++) {
            if (parse_stage == 0 && request_string[i] == '\n') {
                current_header = &request_string[i + 1]; // Set current_header to start of headers
                parse_stage = 1; // increment parse stage
            } else if (parse_stage == 1) {
                // IF END OF HEADER SECTION (marked by \r\n\r\n)
                if (request_string[i] == '\r' && request_string[i + 1] == '\n' && request_string[i + 2] == '\r' && request_string[i + 3] == '\n') {
                    body = &request_string[i + 4]; // Set body ptr to first char of body
                    parse_stage = 2; // Increment parse stage
                    printf("=====HEADERS=====\n");
                    for (int i = 0; i < header_index; i++) {
                        printf("NAME: %s, VALUE: %s\n", header_names[i], header_values[i]);
                    }
                    printf("=======END=======\n");
                    continue;
                }
                // PARSING HEADERS
                if (request_string[i] == '\n') {
                    int header_len = &request_string[i] - current_header;
                    char header_line[1024 + MAXBUFLEN];
                    memcpy(header_line, current_header, header_len + 1);
                    header_line[header_len + 2] = '\0';

                    char *name, *value;
                    name = strtok(header_line, ":");
                    value = strtok(NULL, "\n");
                    strlcpy(header_names[header_index], name, 1023);
                    strlcpy(header_values[header_index], value, MAXBUFLEN - 1);

                    if (strcasecmp(name, "Content-Length") == 0) {
                        content_length = atoi(value);
                    }
                    header_index++;
                    current_header = &request_string[i + 1];
                } else if (parse_stage == 2) {
                    // We are in the body, to be parsed below
                    break;
                }
            }
        }

        for (int i = 0; i < content_length; i++) {
            // loop over body to parse
        }
        printf("BODY LENGTH: %d\n", content_length);
        printf("BODY: %s\n", body);

        // RESPOND TO REQUEST =====
        char* response = malloc(MAXBUFLEN);
        size_t response_len;

        if (strcasecmp(method, "GET") == 0) {
            char* stripped_uri = uri;
            stripped_uri++; // skip leading '/'

            if (strncmp(stripped_uri, "", 1) == 0) {
                strlcpy(stripped_uri, "index.html", sizeof("index.html"));
            }
            build_http_response(stripped_uri, file_extension(stripped_uri), response, &response_len);
            sendall(newosockFD, response, &response_len);

        } else if (strcasecmp(method, "POST") == 0) {
            char* postRes
                = "HTTP/1.1 201 Created\r\n"
                  "\r\n"
                  "Posted\r\n";
            response_len = strlen(postRes);
            sendall(newosockFD, postRes, &response_len);

        } else {
            char* notimplemented = "HTTP/1.1 501 Not Implemented\r\n"
                                   "\r\n"
                                   "501 Not Implemented\r\n";
            response_len = strlen(notimplemented);
            sendall(newosockFD, notimplemented, &response_len);
        }

        free(response);
        close(newosockFD);
    }

    return 0;
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
    // Try and open the file
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        /// If we can't, send a 404 response
        snprintf(response, MAXBUFLEN,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "404 Not Found");
        *response_len = strlen(response);
        return;
    }

    // Create a buffer for the header
    char* header = malloc(MAXBUFLEN);
    snprintf(header, MAXBUFLEN,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "\r\n",
        mime_type(filetype));

    struct stat file_stat;
    fstat(fd, &file_stat);
    off_t filesize = file_stat.st_size;

    // Copy the header bytes into the response buffer
    *response_len = 0;
    memcpy(response, header, strlen(header));
    *response_len += strlen(header);

    // Copy file bytes into the response header
    ssize_t bytes_read;
    while ((bytes_read = read(fd, response + *response_len, MAXBUFLEN - *response_len)) > 0) {
        *response_len += bytes_read;
    }
    free(header);
    close(fd);
}
