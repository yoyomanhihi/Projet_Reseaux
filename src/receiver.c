#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "chat.c"
#include "packet_implem.c"

int seqnum_in_window(int seqnum, int window_offset);
void freebuf(pkt_t** buf);

int main(int argc, char *argv[]) {
  int opt;
  char* filename = NULL;
  struct sockaddr_in6* socketAddress = (struct sockaddr_in6*)malloc(sizeof(struct sockaddr_in6));
  char* address;
  int portNr;
  int sockfd;
  int off = 0;
  int m=1;
  while((opt=getopt(argc,argv,"o:m:"))!=-1){
    switch (opt) {
    case 'o':
      filename = optarg;
      off = off+2;
      break;
    case 'm':
      m=atoi(optarg);
      off+=2;
      break;
    default:
      fprintf(stderr, "mauvais argument");
      return -1;
    break;
    }
  }

  if(argc-off < 3){
    fprintf(stderr, "Arguments missing, expected:%d, received:%d\n", 2+opt, argc-1 );
    free(socketAddress);
    return -1;
  }

  address =  argv[1+off];
  portNr = atoi(argv[2+off]);

  if(m>1){
    int opt=1;
    fd_set set;
    int i;
    int sockets[m];
    int max;
    int new;
    for(i=0; i<m; i++){
      sockets[i]=0;
    }
    int headsock = create_socket(socketAddress, portNr, NULL, -1);
    if(headsock<0){
      return -1;
      free(socketAddress);
    }
    int listen_everywhere = setsockopt(headsock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    if(listen_everywhere<0){
      fprintf(stderr, "Erreur de setsockopt");
      return -1;
    }
    const char *addr = real_address(address, socketAddress);
    if(listen(headsock, m) < 0){
      fprintf(stderr, "Erreur Listen");
      return -1;
    }
    size_t length = sizeof(address);
    while(1==1){
      FD_ZERO(&set);
      FD_SET(headsock, &set);
      max = headsock;
      for(i=0; i<m; i++){
	int sock_descr = sockets[i];
	if(sock_descr > 0)
	  FD_SET(sock_descr, &set);
	if(sock_descr > max)
	  max = sock_descr;
      }
      int act = select(m+1, &set, NULL, NULL, NULL);
      if(act < 0){
	fprintf(stderr, "Erreur select");
	return -1;
      }
      if(FD_ISSET(headsock, &set)!=NULL){
	if((new = accept(headsock, (struct sockaddr *)&address, (socklen_t *)&length))<0){
	  fprintf(stderr, "Erreur accept");
	  return -1;
	}
	for(i = 0; i<max; i++){
	  if(sockets[i]==0){
	    sockets[i] = new;
	    break;
	  }
	}
      }
      for(i = 0; i<max; i++){
	int sock_descr = sockets[i];
	if(FD_ISSET(sock_descr, &set)){
	    int reading = read_write_loop(sock_descr, stdout);
	    if(reading == 0){
	      close(sock_descr);
	      sockets[i]=0;
	    }
	    else{
	      int reading = read_write_loop(sock_descr, stdout);
	    }
	  }
      }
      
    }

    
    
    
    //while(1==1){
    //int errm=select(m+1, &set, NULL, NULL, NULL);
    //if(errm==-1){
    //fprintf(stderr, "select crashes");
    //return -1;
    //}
    //wait_for_client(errm);
    //free(socketAddress);
    //if(read_write_loop(errm, filename)!=PKT_OK){
    //fprintf(stderr, "Error in loop \n");
    //}
    //}
    //return 0;
    }
  

  const char* errmsg = real_address(address,socketAddress);
  if(errmsg!=NULL){
    fprintf(stderr, "%s\n", errmsg);
  }
  sockfd=create_socket(socketAddress,portNr, NULL, -1);
  if(sockfd<0){
    return -1;
    free(socketAddress);
  }
  wait_for_client(sockfd);
  free(socketAddress);
  if(sockfd==-1){
    free(socketAddress);
    fprintf(stderr, "Could not create a socket!\n");
  }
  if(read_write_loop(sockfd,filename)!=PKT_OK){
    fprintf(stderr, "Error in loop \n");
  }
  return 0;
}

int read_write_loop(int sockfd, char *filename){
  int window_size = MAX_WINDOW_SIZE;
  int window_offset = 0;
  int nextSeq = 0;
  int written = 0;
  pkt_t** buf = (pkt_t**)malloc(sizeof(pkt_t*)*(window_size+1));
  char* returnData = (char*)malloc(3*sizeof(uint32_t));
  char* toReceiveData = (char*)malloc(MAX_PAYLOAD_SIZE+4*sizeof(uint32_t));
  size_t bufLen = MAX_PAYLOAD_SIZE+4*sizeof(uint32_t);
  size_t readAmount;
  pkt_status_code statusCode;
  struct pollfd fds[1];
  int timer = -1;
  int retval=1;
  int end = 0;
  int printTo = fileno(stdout);

  if(filename!=NULL){
    printTo=open(filename,O_CREAT|O_RDWR | O_TRUNC,S_IRUSR|S_IWUSR);
    if(printTo < 0){

      free(buf);
      free(returnData);
      free(toReceiveData);
      return -1;
    }
  }

  while (!end) {
    fds[0].fd=sockfd;
    fds[0].events = 0;
    fds[0].events |= POLLIN ;
    retval = poll(fds,1,timer);
    if(retval == -1) {
      perror( "poll:");

      free(buf);
      free(returnData);
      free(toReceiveData);
      return -1;
    }


    /*
    On "écoute" la lecture sur le socket. Une fois des données disponibles, on
    rentre dans la condition if
    */
    if(fds[0].revents & POLLIN) {
      //On lit les données sur le socket
      readAmount = recv(sockfd,toReceiveData,bufLen,0);
      // printf("%ld\n",readAmount );
      if(readAmount<1){
        end = 1;
      }
      pkt_t* pkt = pkt_new();
      //On décode les informations reçues afin qu'elles soient plus "lisibles"
      statusCode = pkt_decode(toReceiveData,readAmount,pkt);
      if(statusCode!=PKT_OK){
        return statusCode;
      }
      if (pkt_get_type(pkt)==PTYPE_DATA) {
        if (pkt_get_length(pkt)<0) {
          return E_LENGTH;
        }
        if (pkt_get_length(pkt)==0 && pkt_get_seqnum(pkt)==nextSeq) {
          end=1;
        } else {
          int tr = pkt_get_tr(pkt);
          int seqnum = pkt_get_seqnum(pkt);
          uint32_t timestamp = pkt_get_timestamp(pkt);
          if(window_offset==seqnum && tr==0){
            window_offset = (window_offset+1)%256;
            nextSeq = (nextSeq+1)%256;
            int i = window_offset;
            const char* pay = pkt_get_payload(pkt);
            written = write(printTo,pay,pkt_get_length(pkt));
            if (written<0) {
              return -1;
            }
            pkt_del(pkt);
            while (buf [i%32]!=NULL) {
              pay = pkt_get_payload(buf[i%32]);
              written = write(printTo,pay,pkt_get_length(buf[i%32]));
              if (written<0) {
                return -1;
              }
              pkt_del(buf [i%32]);
                window_offset = (window_offset+1)%256;
              window_size++;
              nextSeq = (nextSeq+1)%256;
            }
          } else if(seqnum_in_window(seqnum, window_offset)){
            buf[seqnum%32]=pkt;
            nextSeq = pkt_get_seqnum(buf[window_offset%32]);
          }
          pkt_t* ack_nack = pkt_new();
          pkt_set_timestamp(ack_nack,timestamp);
          pkt_set_window(ack_nack,window_size);
          pkt_set_length(ack_nack,0);
          pkt_set_tr(ack_nack,0);
          if(tr==0){
            pkt_set_type(ack_nack,PTYPE_ACK);
            pkt_set_seqnum(ack_nack,nextSeq);
          } else if(tr==1) {
            pkt_set_type(ack_nack,PTYPE_NACK);
            pkt_set_seqnum(ack_nack,nextSeq-1);
          }
          size_t len = sizeof(returnData);
          statusCode = pkt_encode(ack_nack,returnData,&len);
          send(sockfd,returnData,len,0);
          pkt_del(ack_nack);
        }
      }
    }
  }
  shutdown(sockfd,2);

  if(filename!=NULL){
    close(printTo);
  }
  freebuf(buf);
  free(returnData);
  free(toReceiveData);
  return PKT_OK;
}

int seqnum_in_window(int seqnum, int window_offset) {
  if(window_offset+32 < 255 && seqnum > window_offset && seqnum < window_offset+32){
    return 1;
  } else if(window_offset+32 > 255){
    if(seqnum > window_offset || seqnum < (window_offset+32)%256){
      return 1;
    }
  }

  return 0;
}

void freebuf(pkt_t** buf){
  int i;
  for(i=0;i<32;i++){
    if(buf[i]){
      pkt_del(buf[i]);
    }
  }
  free(buf);
}
