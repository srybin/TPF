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
#include <arpa/inet.h>
#include "./taskruntime4.3.h"

silk__independed_coro process_connection(const int s) {
    char buf[1024];
    int n;
         
    while (1) {
        n = co_await silk__read_async(s, buf, 1024);

        if (n <= 0) {
            printf("[%d] process_connection(%d) has been disconnected...\n", silk__current_worker_id, s);
            close(s);

            co_return;
        }

        printf("[%d] process_connection(%d) [%d] %s\n", silk__current_worker_id, s, n, buf);
    }
}

int main() {
    silk__init_pool(silk__schedule, silk__makecontext, 1);

    kq = kqueue();

    struct addrinfo hints, *ser;

    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, "3491", & hints, &ser);

    int listensockfd = socket(ser->ai_family, ser->ai_socktype, ser->ai_protocol);

    fcntl(listensockfd, F_SETFL, fcntl(listensockfd, F_GETFL, 0) | O_NONBLOCK);

    int yes = 1;
    setsockopt(listensockfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int));

    bind(listensockfd, ser-> ai_addr, ser-> ai_addrlen);

    listen(listensockfd, SOMAXCONN);

    auto log_new_connection = []( int s, struct sockaddr_storage addr ) -> silk__coro<> {
        co_await silk__yield();
        
        char ip[NI_MAXHOST];
        char port[NI_MAXSERV];
         
        getnameinfo(
            (struct sockaddr *)&addr,
            sizeof(addr),
            ip,
            sizeof(ip),
            port,
            sizeof(port),
            NI_NUMERICHOST | NI_NUMERICSERV
            );
        
        printf( "[%d] New connection: %s:%s, %d...\n", silk__current_worker_id, ip, port, s );

        co_return;
    };

    auto server = [&]( int listening_socket ) -> silk__independed_coro {
        while ( 1 ) {
            auto[ s, addr, err ] = co_await silk__accept_async( listening_socket );
            
            if ( s ) {
                process_connection( s );
           
                co_await log_new_connection( s, addr );
            }
        }
    };

    auto client = [&] () -> silk__independed_coro {
        auto[ s, result, err ] = co_await silk__connect_async( "127.0.0.1", 3491 );

        char* m = "C3333333333333333333333333333333333333333333333";

        for ( int i = 0; i < 100000; i++ ) {
            co_await silk__write_async( s, m, 48 );
        }

        close( s );
    };
    
    server( listensockfd );

    client();

    client();

    int n = 0;

    struct kevent evSet;
    struct kevent evList[1024];

    while ( 1 ) {
        silk__join_main_thread_2_pool( silk__schedule );

        int nev = kevent(kq, NULL, 0, evList, 1024, NULL); //io poll...

        for (int i = 0; i < nev; i++) {  //run pending...
            if (evList[i].ident == listensockfd || evList[i].filter == EVFILT_WRITE) {
                silk__spawn( silk__current_worker_id, (silk__frame*) evList[i].udata );
            }  else if (evList[i].filter == EVFILT_READ) {
                silk__io_read_awaitable* frame = (silk__io_read_awaitable*) evList[i].udata;

                memset(frame->buf, 0, frame->nbytes);

                frame->n = evList[i].flags & EV_EOF ? 0 : read(evList[i].ident, frame->buf, frame->nbytes);

                silk__spawn(frame->coro);
            }
        }
    }

    return 0;
}