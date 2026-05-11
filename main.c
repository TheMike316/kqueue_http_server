#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT "6969"
#define REQ_BUF 4097 // 4kb plus 0 terminator
#define MAX_EVENTS 10000

const char *INDEX_HTML = "<html lang=\"en\"><head><title>Hello from C</title></head><body><h1>Hello, "
                         "World!</h1><p>Greetings from C</p></body></html>";

int sock_fd = -1;
int kq = -1;

// maybe I want an init function just for api symmetry reasons; we'll see
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} charray;

int charray_append(charray *arr, const char c) {
    if (!arr) {
        fprintf(stderr, "charray_append: NULL pointer\n");
        return -1;
    }
    if (arr->len >= arr->cap) {
        arr->cap = arr->cap == 0 ? 1024 : arr->cap * 2;
        char *new_buf = realloc(arr->buf, arr->cap);
        if (!new_buf) {
            fprintf(stderr, "realloc failed\n");
            return -1;
        }
        arr->buf = new_buf;
    }
    arr->buf[arr->len++] = c;

    return 0;
}

void charray_reset(charray *arr) { arr->len = 0; }

void charray_destroy(charray *arr) {
    free(arr->buf);
    arr->buf = 0;
    arr->cap = 0;
    arr->len = 0;
}

/* function definitions */

static void send_response(const int client_fd, const int status, const char *status_text, const char *content_type,
                          const char *body);

static void handle_sigint(const int sig);


int main(void) {
    signal(SIGINT | SIGTERM, handle_sigint);

    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_storage);
    int status;
    int nev;
    char method[8], path[1024], version[16];
    char request[REQ_BUF] = {0};

    struct kevent events[MAX_EVENTS] = {0};

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }

    sock_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sock_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt); // specifically for macos

    if (bind(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        perror("bind");
        return EXIT_FAILURE;
    }

    const int flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

    if (listen(sock_fd, SOMAXCONN) == -1) {
        perror("listen");
        return EXIT_FAILURE;
    }

    // create event queue
    if ((kq = kqueue()) == -1) {
        perror("kqueue");
        return EXIT_FAILURE;
    }


    printf("listening on port %s\n", PORT);

    struct kevent accept_event = {};
    EV_SET(&accept_event, sock_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
    kevent(kq, &accept_event, 1, NULL, 0, NULL);

    for (;;) {
        nev = kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);
        if (nev < 0) {
            perror("kevent");
            return EXIT_FAILURE;
        }
        if (nev == 0) continue;

        for (size_t i = 0; i < nev; ++i) {
            if (events[i].flags & EV_EOF) {
                if (events[i].ident == sock_fd) {
                    printf("server socket closed??");
                    break;
                } else {
                    close(events[i].ident);
                }
            }
            // TODO error handling
            if (events[i].flags & EV_ERROR) {
                fprintf(stderr, "EV_ERROR: %s\n", strerror(events[i].data));
                continue;
            }

            if (events[i].ident == sock_fd) {
                // TODO drain all client connections
                const int client_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &client_addr_len);

                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    // TODO client error handling
                    perror("accept");
                    return EXIT_FAILURE;
                }

                int flags = fcntl(client_fd, F_GETFL, 0);
                fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

                struct kevent client_event;
                EV_SET(&client_event, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
                kevent(kq, &client_event, 1, NULL, 0, NULL);
            } else {

                /* NOTE(mike): we completely ignore any actual client request params.
                 * This application is purely about using kqueue for async I/O in a web server.
                 */

                const int client_fd = events[i].ident;

                const ssize_t n = recv(client_fd, request, REQ_BUF, 0);
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("asdf\n");
                        continue;
                    } else {
                        perror("recv");
                        return EXIT_FAILURE;
                    }
                }
                if (n == 0) {
                    close(client_fd);
                    continue;
                }
                request[n] = '\0';

                if (sscanf(request, "%7s %1023s %15s", method, path, version) != 3) {
                    send_response(client_fd, 400, "Bad Request", "text/plain",
                                  "Bad Request\n");
                    close(client_fd);
                    continue;
                }

                // printf("Request received: Method: %s; Path: %s; version: %s\n", method, path, version);

                // handle GET and HEAD for now
                int is_head = 0;
                if (strcmp(method, "GET") == 0) {
                    is_head = 0;
                } else if (strcmp(method, "HEAD") == 0) {
                    is_head = 1;
                } else {
                    send_response(client_fd, 405, "Method Not Allowed", "text/plain",
                                  "Only GET/HEAD supported\n");
                    close(client_fd);
                    continue;
                }

                if (is_head) {
                    send_response(client_fd, 200, "HTTP/1.1 200 OK", "text/plain", "");
                } else {
                    send_response(client_fd, 200, "HTTP/1.1 200 OK", "text/html", INDEX_HTML);
                }

                shutdown(client_fd, SHUT_WR);
                close(client_fd);
            }
        }
    }

    close(sock_fd);
    close(kq);
    return EXIT_SUCCESS;
}

static void send_response(const int client_fd, const int status, const char *status_text, const char *content_type,
                          const char *body) {
    char header[512];
    const size_t body_len = body ? strlen(body) : 0;
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Server: mini-c/0.1\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, status_text, content_type, body_len);

    if (n < 0 || n >= sizeof(header)) {
        return;
    }
    send(client_fd, header, (size_t) n, 0);
    if (body_len > 0) {
        send(client_fd, body, body_len, 0);
    }
}

static void handle_sigint(const int sig) {
    (void) sig;
    if (sock_fd != -1) {
        close(sock_fd);
    }
    if (kq != -1) {
        close(kq);
    }
    write(STDOUT_FILENO, "\nShutting down.\n", 16);
    _exit(EXIT_SUCCESS);
}
