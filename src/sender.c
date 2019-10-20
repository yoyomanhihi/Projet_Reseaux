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

void freebuf(pkt_t** buf);

int main(int argc, char *argv[]) {
  int opt;
  char* filename = NULL;
  struct sockaddr_in6* socketAddress = (struct sockaddr_in6*)malloc(sizeof(struct sockaddr_in6)) ;
  char* address;
  int portNr;
  int sockfd;
  int off = 0;
  opt=getopt(argc,argv,"f:");
  switch (opt) {
    case 'f':
    filename = optarg;
    off = off+2;
    break;
    default:
    filename=NULL;
  }
  // if(opt!=-1){
  //   filename = optarg;
  //   opt++;
  // }

  if(argc-off < 3){
    fprintf(stderr, "Arguments missing, expected:%d, received:%d\n", 2+opt, argc-1 );
    return -1;
  }
  address =  argv[1+off];
  portNr = atoi(argv[2+off]);
  const char* errmsg = real_address(address,socketAddress);
  if(errmsg!=NULL){
    fprintf(stderr, "%s\n", errmsg);
    return -1;
  }
  sockfd=create_socket(NULL,-1,socketAddress,portNr);
  // free(socketAddress);
  if(sockfd==-1){
    fprintf(stderr, "Could not create a socket!\n");
    return -1;
  }
  free(socketAddress);
  if(read_write_loop(sockfd,filename)!=PKT_OK){
    fprintf(stderr,"Error in loop\n");
  }
  return 0;
}


int read_write_loop(int sockfd, char* filename) {
  int window_size = MAX_WINDOW_SIZE;
  int window_offset = 0;
  int receiver_window_size= 1;
  uint32_t retransmition_timer=1000000;
  int currentSeq = 0;
  int lastAck;
  struct timeval tv;
  pkt_t** buf = (pkt_t**)malloc(sizeof(pkt_t*)*(window_size+1));
  char* readData1 = (char*)malloc(MAX_PAYLOAD_SIZE);
  char* readData2 = (char*)malloc(3*sizeof(uint32_t));
  char* toSendData = (char*)malloc(MAX_PAYLOAD_SIZE+4*sizeof(uint32_t));
  size_t bufLen = MAX_PAYLOAD_SIZE+4*sizeof(uint32_t);
  size_t readAmount = 1;
  pkt_status_code statusCode;
  struct pollfd fds[2];
  int timer = -1;
  int retval;
  int sendRead = fileno(stdin);
  int firstSend = 1;

  if(filename!=NULL){
    FILE* f=fopen(filename,"rwb");
    if(f==NULL){
      printf("%s\n", "Failed opening file...");
    }
    sendRead = fileno(f);
    if(sendRead < 0){
      printf("%s\n", "Could not open file");
      free(buf);
      free(readData1);
      free(readData2);
      free(toSendData);
      return -1;
    }
  }
  while (readAmount!=0 || window_size<MAX_WINDOW_SIZE) {
    fds[0].fd=sendRead;
    fds[1].fd=sockfd;

    fds[0].events = 0;
    fds[1].events = 0;

    fds[0].events |= POLLIN;
    fds[1].events |= POLLIN;
    retval = poll(fds,2,timer);
    if(retval == -1) {
      perror( "poll:");

      free(buf);
      free(readData1);
      free(readData2);
      free(toSendData);
      return -1;
    }
    //Dans le cas où l'on lit sur l'entrée standard/fichier
    if((fds[0].revents & POLLIN && receiver_window_size>0)) {
      pkt_t* pkt = pkt_new();//Créér une structure afin de l'envoyer
      if(pkt==NULL){
        freebuf(buf);
        free(readData1);
        free(readData2);
        free(toSendData);
        return E_NOMEM;
      }
      bufLen = MAX_PAYLOAD_SIZE+4*sizeof(uint32_t);

      if(gettimeofday(&tv,NULL)==-1){
        fprintf(stderr, "%s\n",strerror(errno) );
        pkt_del(pkt);
        freebuf(buf);
        free(readData1);
        free(readData2);
        free(toSendData);
        return -1;
      }
      int now = ((tv.tv_sec)%10)*1000000+tv.tv_usec;

      /*
      Vérifier s'il ne faut pas renvoyer un paquet non acquité
      Pour ce faire on vérifie si la différence entre le timestamp du dernier
      Ack et le premier élément du buffer est plus grand qu'un certain temps
      De retransmition.
      */
      int i = window_offset;
      if(buf[i%32]!=NULL){
        uint32_t pkt_timestamp = pkt_get_timestamp(buf[i%32]);
        if(abs(pkt_timestamp-now)>retransmition_timer){
          pkt_set_timestamp(buf[i%32],now);
          statusCode = pkt_encode(pkt,toSendData,&bufLen);
          if(statusCode!=PKT_OK){
            fprintf(stderr, "%s\n", "Could not encode data");
            pkt_del(pkt);
            freebuf(buf);
            free(readData1);
            free(readData2);
            free(toSendData);
            return statusCode;
          }
          send(sockfd,toSendData,sizeof(toSendData),0);
        }
      }


      //Prendre l'heure afin de le mettre dans le timestamp
      if(gettimeofday(&tv,NULL)==-1){
        fprintf(stderr, "%s\n",strerror(errno) );
        pkt_del(pkt);
        freebuf(buf);
        free(readData1);
        free(readData2);
        free(toSendData);
        return -1;
      }
      pkt_set_timestamp(pkt,now); //mettre le timestamp tu paquet à l'heure en us

      //Lire les données à lire par paquet de 512 bytes
      readAmount = read(sendRead,readData1,MAX_PAYLOAD_SIZE);
      if(readAmount>0){
        pkt_set_type(pkt,PTYPE_DATA); //type data
        pkt_set_tr(pkt,0); // non tronqué
        pkt_set_window(pkt,window_size); // taille de la fenêtre de réception actuelle
        pkt_set_length(pkt,readAmount); // taille du payload
        pkt_set_seqnum(pkt,currentSeq); //numéro de séquence
        pkt_set_payload(pkt,readData1,readAmount);
        statusCode = pkt_encode(pkt,toSendData,&bufLen);//Crée une suite de bytes à partir de la structure pkt_t
        if(statusCode!=PKT_OK){
          pkt_del(pkt);
          freebuf(buf);
          free(readData1);
          free(readData2);
          free(toSendData);
          return statusCode;
        }
        buf[currentSeq%32] = pkt; // encoder le paquet envoyer dans un buffer;
        int written = write(sockfd,toSendData,bufLen); //envoyer la donnée sur le socket
        if(written != bufLen){
          fprintf(stderr, "%s\n", "Could not write to socket");
          freebuf(buf);
          free(readData1);
          free(readData2);
          free(toSendData);
          return -1;
        }
        currentSeq = (currentSeq+1)%256;
        window_size--;
        receiver_window_size--;
      } else {
        pkt_del(pkt);
      }

    }

    if(fds[0].revents & POLLIN && firstSend==1){
      int sentTimestamp = pkt_get_timestamp(buf[0]);
      if(gettimeofday(&tv,NULL)==-1){
        fprintf(stderr, "%s\n",strerror(errno) );
        freebuf(buf);
        free(readData1);
        free(readData2);
        free(toSendData);
        return -1;
      }
      int currentTime = ((tv.tv_sec)%10)*1000000+tv.tv_usec;
      if(abs(currentTime-sentTimestamp)>retransmition_timer){
        pkt_set_timestamp(buf[0],currentTime);
        statusCode = pkt_encode(buf[0],toSendData,&bufLen);
        if(statusCode!=PKT_OK){
          fprintf(stderr, "%s\n", "Could not encode data");
          freebuf(buf);
          free(readData1);
          free(readData2);
          free(toSendData);
          return statusCode;
        }
        send(sockfd,toSendData,bufLen,0);

      }
    }


    //Le cas où sender lit sur le socket (un paquet de type ack/nack)
    if(fds[1].revents & POLLIN) {
      pkt_t* pkt = pkt_new();
      size_t readAck = 0;
      if(pkt==NULL){
        fprintf(stderr, "%s\n", "Could not create pkt");
        freebuf(buf);
        free(readData1);
        free(readData2);
        free(toSendData);
      }
      readAck = recv(sockfd,readData2,3*sizeof(uint32_t),0); //lecture des données sur le socket
      statusCode = pkt_decode(readData2,readAck,pkt); //Création de la structure à partir des données lues

      ptypes_t type = pkt_get_type(pkt);
      uint16_t len = pkt_get_length(pkt);
      if(statusCode!=PKT_OK){
      } else if(len>0){
        fprintf(stderr, "Length must be set to 0\n");
      } else if(type==PTYPE_DATA){ // Si le paquet reçu est de type data --> discard
      } else if(type == PTYPE_ACK ){
        int seq = pkt_get_seqnum(pkt);
        lastAck = seq;
        if (pkt_get_tr(pkt)==1) {
        } else {
          if (firstSend) {
            receiver_window_size = pkt_get_window(pkt);
            firstSend = 0;
          }
          while (buf[window_offset%32]!=NULL && pkt_get_seqnum(buf[window_offset%32])!=seq) { //On supprime tous les packets dont les numéros de séquence sont antérieurs à celui reçu par l'ack
          pkt_del(buf[window_offset%32]);
          buf[window_offset%32] = NULL;
          window_offset = (window_offset+1)%256; //on décalle la fenêtre de 1 vers la droite;
          window_size ++; // On a une place en plus dans le buffer;
          receiver_window_size ++; //La fenre du receiver a une place en plus;
        }
      }
    } else if(type == PTYPE_NACK) {
      int seq = pkt_get_seqnum(pkt);
      lastAck = seq;
      statusCode = pkt_encode(buf[seq%32],toSendData,&bufLen);
      if(statusCode!=PKT_OK){
        fprintf(stderr, "%s\n", "Could not encode nack");
        freebuf(buf);
        free(readData1);
        free(readData2);
        free(toSendData);
        return statusCode;
      }
      send(sockfd,toSendData,bufLen,0);
    }
    pkt_del(pkt);
  }
}
//Avant de fermer le socket, on envoie un paquet de type PTYPE_DATA avec length=0
if(gettimeofday(&tv,NULL)==-1){
  fprintf(stderr, "%s\n",strerror(errno) );
  freebuf(buf);
  free(readData1);
  free(readData2);
  free(toSendData);
  return -1;
}
pkt_t* lastPkt = pkt_new();
pkt_set_type(lastPkt,PTYPE_DATA);
pkt_set_tr(lastPkt,0);
pkt_set_length(lastPkt,0);
pkt_set_seqnum(lastPkt,lastAck);
pkt_set_timestamp(lastPkt,((tv.tv_sec)%10)*1000000+tv.tv_usec);

pkt_encode(lastPkt,toSendData, &bufLen);
send(sockfd,toSendData,bufLen,0);

pkt_del(lastPkt);



close(sockfd);
if(filename!=NULL){
  close(sendRead);
}
freebuf(buf);
free(readData1);
free(readData2);
free(toSendData);
return PKT_OK;

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
