#include <stdlib.h> /* EXIT_X */
#include <stdio.h> /* fprintf */
#include <unistd.h> /* getopt */
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <netinet/in.h>

#include "Headers/real_address.h"
#include "Headers/create_socket.h"
#include "Headers/read_write_loop.h"
#include "Headers/wait_for_client.h"

//Ce code a été repris du code du projet de l'année précédente. Les membres du groupe à l'époque étaient Matteo Snellings et moi-même, Simon Kellen.

const char * real_address(const char *address, struct sockaddr_in6 *rval) {
  struct addrinfo hints, *res, *looper;
    int err;
    char addr_buf[64];
    memset(addr_buf, 0, sizeof(addr_buf));
    memset(&hints, 0, sizeof(struct addrinfo));
    memset(rval,0,sizeof(struct sockaddr_in6));

    rval->sin6_family = AF_INET6;
    rval->sin6_addr = in6addr_any;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;

    err = getaddrinfo(address, NULL, &hints, &res);
    if(err==-1) {
        // freeaddrinfo(looper);
        return (const char*)gai_strerror(err);
    }

    for(looper=res;looper!=NULL;looper=looper->ai_next) {
        if(looper->ai_family==AF_INET6){
            *rval = *(struct sockaddr_in6*)looper->ai_addr;
        }
    }
    if(rval == NULL) {
        char* msg = "No valid address found!";
        return msg;
    }
    freeaddrinfo(res);
    // freeaddrinfo(looper);
    return NULL;

}

int create_socket(struct sockaddr_in6 *source_addr, int src_port, struct sockaddr_in6* dest_addr, int dst_port){
  int sockfd;
  sockfd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if(sockfd == -1) {
    fprintf(stderr,"%s\n", strerror(errno));
    return -1;
  }


  if(source_addr!=NULL && src_port>0){

    source_addr->sin6_port = (in_port_t)htons(src_port);

    struct sockaddr* newAdd = (struct sockaddr *)source_addr;
    if(bind(sockfd,newAdd,(socklen_t)sizeof(struct sockaddr_in6))==-1){
      fprintf(stderr,"!!%s\n", strerror(errno));
      return -1;
    }
  }
  if(dest_addr!=NULL && dst_port>0){
    dest_addr->sin6_port = (in_port_t)htons(dst_port);
    struct sockaddr* newAdd2 = (struct sockaddr *)dest_addr;
    if(connect(sockfd,newAdd2,sizeof(*dest_addr))<0){
      fprintf(stderr,"!!%s\n", strerror(errno));
      return -1;
    }
  }


  // printf("src: %d, dest:%d ------ Port ----- src:%d, dest:%d\n",source_addr==NULL, dest_addr==NULL, src_port, dst_port );
  return sockfd;

}

int wait_for_client(int sfd) {
  struct sockaddr_in6 addres;
  socklen_t addrlen = sizeof(addres);
  char msg[512+4*sizeof(uint32_t)];
  int l = recvfrom(sfd,msg,sizeof(msg),MSG_PEEK,(struct sockaddr*)&addres,&addrlen);
  if(l<1){
    return -1;
  }
  if(connect(sfd,(struct sockaddr*)&addres,addrlen)==-1) {
    return -1;
  }
  return 0;
}


