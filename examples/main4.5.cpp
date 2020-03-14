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
#include "./taskruntime4.3.h"

silk::demo_runtime_4_3::independed_task process_connection(const int s) {
    char buf[1024];
    int n;
         
    while (1) {
        n = co_await silk::demo_runtime_4_3::read_async(s, buf, 1024);

        if (n <= 0) {
            printf("[%d] process_connection(%d) has been disconnected...\n", silk::current_worker_id, s);
            close(s);

            co_return;
        }

        printf("[%d] process_connection(%d) [%d] %s\n", silk::current_worker_id, s, n, buf);
    }
}

int main() {
    silk::init_pool(silk::demo_runtime_4_3::schedule, silk::makecontext);

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

    auto log_new_connection = []( int s, struct sockaddr_storage addr ) -> silk::demo_runtime_4_3::task<> {
        co_await silk::demo_runtime_4_3::yield();
        
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
        
        printf( "[%d] New connection: %s:%s, %d...\n", silk::current_worker_id, ip, port, s );

        co_return;
    };

    auto server = [&]( int listening_socket ) -> silk::demo_runtime_4_3::independed_task {
        while ( 1 ) {
            auto[ s, addr, err ] = co_await silk::demo_runtime_4_3::accept_async( listening_socket );
            
            if ( s ) {
                process_connection( s );
           
                co_await log_new_connection( s, addr );
            }
        }
    };
    
    int n = 0;
    silk::demo_runtime_4_3::kq = kqueue();
    struct kevent evSet;
    struct kevent evList[1024];
    
    server( listensockfd );

    while (1) {
        int nev = kevent(silk::demo_runtime_4_3::kq, NULL, 0, evList, 1024, NULL); //io poll...

        for (int i = 0; i < nev; i++) {  //run pending...
            if (evList[i].ident == listensockfd) {
                silk::demo_runtime_4_3::schedule( (silk::demo_runtime_4_3::frame*)evList[i].udata ); //silk::demo_runtime_4_3::spawn( (silk::demo_runtime_4_3::frame*)evList[i].udata );
            }  else if (evList[i].filter == EVFILT_READ) {
                silk::demo_runtime_4_3::io_read_awaitable* frame = (silk::demo_runtime_4_3::io_read_awaitable*) evList[i].udata;

                memset(frame->buf, 0, frame->nbytes);

                frame->n = evList[i].flags & EV_EOF ? 0 : read(evList[i].ident, frame->buf, frame->nbytes);

                silk::demo_runtime_4_3::spawn(frame->coro);
            }
        }
    }

    return 0;
}