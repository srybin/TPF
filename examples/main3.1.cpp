#define _XOPEN_SOURCE 600
#define SO_REUSEPORT    0x0200          /* allow local address & port reuse */
#include <sys/socket.h>
#include <sys/errno.h>
#include <netdb.h>
#include <assert.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/event.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include "./taskruntime3.1.h"
//#include "./taskruntime3.2.h"

void process_connection(const int socket) {
    char buf[1024];
    int n;
         
    while (1) {
        n = silk::demo_runtime_3_1::read_async(socket, buf, 1024);

        if (n <= 0) {
            close(socket);

            return;
        }
                 
        printf("[%d] r(%d) [%d] %s\n", silk::current_worker_id, socket, n, buf);
    }
}

int main() {
    silk::init_pool(silk::demo_runtime_3_1::schedule, silk::demo_runtime_3_1::makeuwcontext);

    struct addrinfo hints, *ser;

    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, "3491", & hints, &ser);

    int listensockfd = socket(ser->ai_family, ser->ai_socktype, ser->ai_protocol);

    fcntl(listensockfd, F_SETFL, fcntl(listensockfd, F_GETFL, 0) | O_NONBLOCK);

    int yes = 1;
    setsockopt(listensockfd, SOL_SOCKET, SO_REUSEPORT, & yes, sizeof(int));

    bind(listensockfd, ser-> ai_addr, ser-> ai_addrlen);

    listen(listensockfd, SOMAXCONN);

    silk::demo_runtime_3_1::kq = kqueue();
    struct kevent evSet;
    struct kevent evList[1024];

    EV_SET(&evSet, listensockfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    assert(-1 != kevent(silk::demo_runtime_3_1::kq, & evSet, 1, NULL, 0, NULL));
    
    int n = 0;

    while (1) {
        int nev = kevent(silk::demo_runtime_3_1::kq, NULL, 0, evList, 1024, NULL); //io poll...

        for (int i = 0; i < nev; i++) {  //run pending...
            if (evList[i].ident == listensockfd) {
                while (1) {
                    struct sockaddr_storage addr;
                    socklen_t socklen = sizeof(addr);
                    
                    int clientsockfd = accept(evList[i].ident, (struct sockaddr *)&addr, &socklen);
                    if (clientsockfd == -1 && (errno == EAGAIN || errno == ECONNRESET)) {
                        break;
                    }

                    if (clientsockfd == -1) {
                        break;
                    }

                    fcntl(clientsockfd, F_SETFL, fcntl(clientsockfd, F_GETFL, 0) | O_NONBLOCK);

                    silk::demo_runtime_3_1::spawn(define_coro process_connection, 32768, 1, clientsockfd);
                }
            }  else if (evList[i].filter == EVFILT_READ) {
                silk::demo_runtime_3_1::io_read_frame* frame = (silk::demo_runtime_3_1::io_read_frame*) evList[i].udata;

                memset(frame->buf, 0, frame->nbytes);

                frame->n = evList[i].flags & EV_EOF ? 0 : read(evList[i].ident, frame->buf, frame->nbytes);

                silk::demo_runtime_3_1::resume(frame->coro_frame);
            }
        }
    }

    return 0;
}