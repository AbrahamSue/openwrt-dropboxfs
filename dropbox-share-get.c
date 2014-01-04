#define _GNU_SOURCE

#include <stdio.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/rtc.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>                             //refer to memset
#include <netinet/in.h>
#include <sys/select.h>				//refer to select
#include <sys/types.h>
#include <sys/socket.h>				//refer to socket and more
#include <unistd.h>				//refer to fcntl
#include <fcntl.h>
#include <sys/stat.h>				//refer to open
#include <netdb.h>				//refer to gethostbyname
#include <netinet/in.h>
#include <arpa/inet.h>				
/* HTTP sample
> GET /s/pvoa2wp78hzxh09/packages-all.openwrt.squashfs.img?dl=1&token_hash=AAH-YvFdlo3C1np6gqHFuV2y08kIOjVZiBcO-w5GuN5wRg HTTP/1.1
> User-Agent: Mozilla/5.0 (Windows; U; MSIE 9.0; WIndows NT 9.0; en-US)
> Host: dl.dropboxusercontent.com
> Accept: * /*
> Range: bytes=1024-2047
> Connection: Keep-Alive
>
< HTTP/1.1 200 OK
< accept-ranges: bytes
< cache-control: max-age=0
< content-disposition: attachment; filename="packages-all.openwrt.squashfs.img"
< Content-Type: application/octet-stream
< Date: Thu, 02 Jan 2014 19:47:42 GMT
< etag: 68d
< pragma: public
< Server: nginx
< x-dropbox-request-id: 6a13c613a376936b686aa4781f5c7a75
< X-RequestId: b7bfc405abee924d5eb8557e7bfc7191
< x-server-response-time: 597
< Content-Length: 219537408
< Connection: keep-alive
*/
char req_template[]=
  "GET %s HTTP/1.1\r\n"
  "User-Agent: Mozilla/5.0 (Windows; U; MSIE 9.0; WIndows NT 9.0; en-US)\r\n"
  "Host: %s\r\n"
  "Accept: application/octet-stream\r\n"
  "Range: bytes=%u-%u\r\n"
  "Connection: Keep-Alive\r\n"
  "\r\n";

enum {
  RU_ERR_START = 0,
//# grep -oE "RU_ERR[_A-Z]+" dropbox-share-get.c |sort -u|sed -e 's/^/  /g;s/$/,/g'
  RU_ERR_CONNECTION_FAILURE,
  RU_ERR_HOST_TOO_LONG,
  RU_ERR_INVALIDE_PROXY_PROVIDED,
  RU_ERR_NETWORK_PROTOCOL_ERROR,
  RU_ERR_NETWORK_READ_ERROR,
  RU_ERR_NETWORK_WRITE_ERROR,
  RU_ERR_NOT_ENOUGH_MEMORY,
  RU_ERR_NO_HOST_FOUND,
  RU_ERR_NO_HOST_IN_URL,
  RU_ERR_NO_SOCKET_AVAILABLE,
  RU_ERR_NO_URL_PROVIDED,
  RU_ERR_WRONG_HOST_TYPE,

/*
  RU_ERR_NO_URL_PROVIDED,
  RU_ERR_NO_HOST_IN_URL,
  RU_ERR_HOST_TOO_LONG,
  RU_ERR_NO_SOCKET_AVAILABLE,
  RU_ERR_NO_HOST_FOUND,
  RU_ERR_WRONG_HOST_TYPE,
  RU_ERR_CONNECTION_FAILURE,
*/
  RU_ERR_END
};
typedef struct _readurl_s {
  int socket_fd;
  int read_offset;
  int read_length;
  char proxy[128];
  int proxy_port;
  char *path;
  char host[128];
  char url[1024];
} READURL;
typedef int (*READURL_CALLBACK)(unsigned char *buffer, int size, READURL *ru, void *stuff);
int samplecb(unsigned char *buffer, int size, READURL *ru, void *stuff){
  //fprintf(stderr, "ReadUrl %s callback with %p:%d %p\n", ru?ru->url:NULL, buffer, size, stuff);
  fprintf(stderr, "%p:%d ", buffer, size);
  write(1, buffer, size);
  return;
}
int read_from_url_with_callback(READURL *ru, READURL_CALLBACK cb, void* stuff){
#define RETURN(v) do{ ret_val=(-v); ret_line=__LINE__; ret_info=__FILE__ ":"  "=>" #v; goto __return__label; }while(0)
int ret_val = 0, ret_line; char *ret_info = NULL;
  unsigned long temp;
  int res, req_length, need_connect;
  struct sockaddr_in sa;
  char *socket_buffer = NULL;
  const int socket_buffer_length = 4096;
  struct hostent *he = NULL;
  char *p1, *p2, *target_hostname;
  int len, head_len, target_port;
//  if(ru) fprintf(stderr, "url=%s %x %x\n", ru->url, *(unsigned int*)ru->url, (unsigned int)'http');
  if(!ru) RETURN(RU_ERR_NO_URL_PROVIDED);
  
  need_connect = 1;
  if(ru->socket_fd > 0){
    //check if fd already closed
    res = recv(ru->socket_fd, &temp, sizeof(temp), MSG_DONTWAIT | MSG_PEEK);
    if(res > 0 || (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) need_connect = 0;
  }
  
  if(need_connect){ //open socket
    p1 = strcasestr(ru->url, "http://");
 //   if( ! (p1 && p1[1]=='/' && p1[2]=='/' && p1[3]) ) RETURN(RU_ERR_NO_HOST_IN_URL); //make sure p1 at least point to ://something
//    p1 += 3;	//skip "://"
    if(!p1) RETURN(RU_ERR_NO_URL_PROVIDED);
    p1 += 7;
    p2 = strchr(p1, '/');
    if(p2){
      len = p2 - p1;
      ru->path = p2;
    }else{
      len = strlen(p1);
      ru->path = "/";
    }
    if(len >= sizeof(ru->host)) RETURN(RU_ERR_HOST_TOO_LONG);
    memcpy(ru->host, p1, len);
    ru->host[len] = 0;
    
    //char *getenv(const char *name);  from stdlib
    p1 = getenv("http_proxy");
    if(p1 && p1[0]){
      p1 = strcasestr(p1, "http://");
      if(!p1) RETURN(RU_ERR_INVALIDE_PROXY_PROVIDED);
      p1 += 7;
      if(p2 = strchr(p1, ':')){
        ru->proxy_port = atoi(p2+1);
        if(ru->proxy_port <= 0) RETURN(RU_ERR_INVALIDE_PROXY_PROVIDED);
        len = p2 - p1;
        if(len >= sizeof(ru->proxy)) RETURN(RU_ERR_INVALIDE_PROXY_PROVIDED);
        memcpy(ru->proxy, p1, len);
        ru->proxy[len] = 0;
      }else{
        p2 = strchr(p1, '/');
        if(!p2) p2 = p1 + strlen(p1);
        len = p2 - p1;
        if(len >= sizeof(ru->proxy)) RETURN(RU_ERR_INVALIDE_PROXY_PROVIDED);
        memcpy(ru->proxy, p1, len);
        ru->proxy[len] = 0;
        ru->proxy_port = 8080;
      }
    }
    
    ru->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(ru->socket_fd < 0) RETURN(RU_ERR_NO_SOCKET_AVAILABLE);
    /* Set it to listen to specified port.  */
    memset((void *)&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;

    /*get ip to connect*/
    if(strlen(ru->proxy) > 0){
      target_hostname = ru->proxy;
      target_port = ru->proxy_port;
    }else{
      target_hostname = ru->host;
      target_port = 80;
    }
    he = gethostbyname( target_hostname );
    if(!he) RETURN(RU_ERR_NO_HOST_FOUND);
    if(he->h_addrtype != AF_INET) RETURN(RU_ERR_WRONG_HOST_TYPE);
    if(he->h_addr_list[0] == NULL) RETURN(RU_ERR_NO_HOST_FOUND);
    //inet_pton(sa.sin_family, "135.156.228.210", &sa.sin_addr);
    memcpy((void*)( &sa.sin_addr ), (void *)( he->h_addr_list[0] ), sizeof(sa.sin_addr));
    sa.sin_port = htons(target_port);   //FIXME: assume only 80 port for http server

    res = connect(ru->socket_fd, (struct sockaddr *)&sa, sizeof(sa));
    fprintf(stderr, "%s:%d connected\n", target_hostname, target_port);
    if(res < 0) RETURN(RU_ERR_CONNECTION_FAILURE);
  }
  
  socket_buffer = malloc(socket_buffer_length + 1);
  if(!socket_buffer) RETURN(RU_ERR_NOT_ENOUGH_MEMORY);
  socket_buffer[socket_buffer_length] = 0;
  res = snprintf(socket_buffer, socket_buffer_length, req_template, ru->path, ru->host, ru->read_offset, ru->read_offset+ru->read_length-1);
  if(res > socket_buffer_length) RETURN(RU_ERR_NOT_ENOUGH_MEMORY);
  write(2, socket_buffer, res);
  res = write(ru->socket_fd, socket_buffer, req_length = res);
  if(res < 0) RETURN(RU_ERR_NETWORK_WRITE_ERROR);
  res = read(ru->socket_fd, socket_buffer, socket_buffer_length);
  if(res < 0) RETURN(RU_ERR_NETWORK_READ_ERROR);
  p1 = strcasestr(socket_buffer, "HTTP/1.");
  if(!p1) RETURN(RU_ERR_NETWORK_PROTOCOL_ERROR);
  p1+=8;
  if(strncasecmp(p1, " 206", 4)) RETURN(RU_ERR_NETWORK_PROTOCOL_ERROR);
  p1 = strcasestr(socket_buffer, "\r\nContent-Length: ");
  if(! (p1 && (len = atoi(p1 + strlen("\r\nContent-Length: "))) > 0)) RETURN(RU_ERR_NETWORK_PROTOCOL_ERROR);
  p1 = strcasestr(p1, "\r\n\r\n");
  if(!p1) RETURN(RU_ERR_NETWORK_PROTOCOL_ERROR);
  p1 += 4;
  head_len = (p1 - socket_buffer);
  write(2, socket_buffer, head_len);
  //Here is the binary data
  if(!cb) cb = samplecb;
  cb(p1, res - head_len, ru, stuff);
  len -= (res - head_len);
  while(len > 0){
    res = read(ru->socket_fd, socket_buffer, socket_buffer_length);
    if(res < 0) RETURN(RU_ERR_NETWORK_READ_ERROR);
    cb(socket_buffer, res, ru, stuff);
    len -= res;
  }

  fprintf(stderr, "\n");
__return__label:
  if(socket_buffer) free(socket_buffer);
  if(ret_val < 0) {
    fprintf(stderr, "%s Line %d ret_val=%d, ", ret_info, ret_line, ret_val);
    if(ru && ru->socket_fd > 0) close(ru->socket_fd);
  }
  switch(ret_val){
  case 0: break;
  case -1: fprintf(stderr, "No URL provided.\n"); break;
  default: fprintf(stderr, "Unknown error.\n"); break;
  }
  return ret_val;
}

int main(int argc, char **argv)
{
  READURL dropbox = { 
    .socket_fd = -1,
    .url="http://dl.dropboxusercontent.com/s/pvoa2wp78hzxh09/packages-all.openwrt.squashfs.img"
		"?dl=1&token_hash=AAH-YvFdlo3C1np6gqHFuV2y08kIOjVZiBcO-w5GuN5wRg"
  };

  while(argc > 1){
    dropbox.read_offset = atoi(argv[1]);
    dropbox.read_length = strchr(argv[1], ':')?atoi(strchr(argv[1], ':')+1):(128*1024);
    argc--; argv++;
    read_from_url_with_callback(&dropbox, NULL, NULL);
  }
  close(dropbox.socket_fd);
  return 0;
}
