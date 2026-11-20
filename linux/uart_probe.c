/* Minimal diagnostic reader for either Linux PC or Milk-V Duo S.
 * NOT envmond, not a service, and never writes pinmux or system settings.
 * stdout/stderr are host console only; the UART transmits framed SYS heartbeats.
 */
#define _POSIX_C_SOURCE 200809L
#include "env_protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running=1;
static void stop(int signo){(void)signo;running=0;}
static unsigned long long ms_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long long)t.tv_sec*1000ull+(unsigned)t.tv_nsec/1000000ull;}
static int configure_port(int fd){
    struct termios t;
    if(tcgetattr(fd,&t)!=0)return -1;
    cfsetispeed(&t,B115200);cfsetospeed(&t,B115200);
    t.c_iflag=0;t.c_oflag=0;t.c_lflag=0;
    t.c_cflag=(t.c_cflag&~(CSIZE|PARENB|CSTOPB))|CS8|CREAD|CLOCAL;
#ifdef CRTSCTS
    t.c_cflag &= ~CRTSCTS;
#endif
    t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;
    return tcsetattr(fd,TCSANOW,&t);
}
static int write_all(int fd,const char *buf,size_t len){
    for(size_t pos=0;pos<len;){
        ssize_t n=write(fd,buf+pos,len-pos);
        if(n>0){pos+=(size_t)n;continue;}
        if(n<0&&(errno==EINTR||errno==EAGAIN)){struct pollfd p={.fd=fd,.events=POLLOUT};if(poll(&p,1,200)<0&&errno!=EINTR)return -1;continue;}
        return -1;
    }
    return 0;
}
int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"Usage: %s /dev/ttyXXX\n",argv[0]);return 2;}
    int fd=open(argv[1],O_RDWR|O_NOCTTY|O_NONBLOCK);
    if(fd<0){perror("open UART");return 1;}
    if(configure_port(fd)<0){perror("termios");close(fd);return 1;}
    struct sigaction sa={.sa_handler=stop};sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);
    env_rx_t rx;env_rx_init(&rx);
    char payload[ENV_MAX_FRAME+1],frame[ENV_MAX_FRAME+1];
    unsigned long long next_sys=0;
    fprintf(stderr,"UART diagnostic opened %s (115200 8N1); Ctrl+C to exit\n",argv[1]);
    while(running){
        unsigned long long now=ms_now();
        if(now>=next_sys){
            size_t n=env_frame(frame,sizeof(frame),"SYS,0");
            if(!n||write_all(fd,frame,n)<0){perror("write SYS");break;}
            next_sys=now+1000ull;
        }
        struct pollfd p={.fd=fd,.events=POLLIN};
        int rc=poll(&p,1,100);
        if(rc<0){if(errno==EINTR)continue;perror("poll");break;}
        if(p.revents&POLLIN){
            char bytes[128];ssize_t n=read(fd,bytes,sizeof(bytes));
            if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
            if(n<0){perror("read UART");break;}
            for(ssize_t j=0;j<n;j++) if(env_rx_byte(&rx,bytes[j],payload,sizeof(payload))) {
                fprintf(stdout,"RX %s\n",payload);fflush(stdout);
            }
        }
    }
    fprintf(stderr,"RX invalid frames: %u\n",rx.errors);
    close(fd);return 0;
}
