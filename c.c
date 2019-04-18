#include <sys/epoll.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <strings.h>
#include <sys/select.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include <sys/ipc.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <semaphore.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/sem.h>
#include<sys/wait.h>
#define key 2000
#define MSG_SIZE 50

void writeToSocket(char b[30], int d, int sfd);
void recvFromSocket(int sfd, int childno);
bool isClosed(int sock);

union semun
{
  int val;          
  struct semid_ds *buf;     /* Buffer for IPC_STAT, IPC_SET */
  unsigned short *array;
  struct seminfo *__buf;
};

int main(int argc,char *argv[]) {
   if(argc!=4) {
        printf("Usage: ./a.out N M T\n");
        exit(0);
    }
    int portno;
    char ipaddr[128];
    printf("Server port number: ");
    fflush(stdout);
    scanf("%d", &portno);
    // printf("Server ip address: ");
    strcpy(ipaddr, "172.17.72.212");
    // fflush(stdout);
    // scanf("%s", ipaddr);
    int N = atoi(argv[1]);
    int M = atoi(argv[2]);
    int T = atoi(argv[3]);
    struct sembuf sb;
    int semid = semget (key, 1, IPC_CREAT | 0777);
    //set semaphore value
    if(semid < 0) {
        perror("semget");
        exit(0);
    }
    union semun setval;
    setval.val = T;
    int ret = semctl (semid, 0, SETVAL, setval); //T is total no of connections
    
    if(ret < 0)
        perror("ret");
    int retval = semctl(semid,0,GETVAL,setval);
    if(retval < 0) {
        perror("semctl");
        exit(0);
    }
    // exit(0);
    if (ret == -1){
        perror ("semctl setting sem value");
        exit(0);
    }    
    sb.sem_num = 0;
    sb.sem_op = -1;//decrement
    sb.sem_flg = 0;
    
    // int p[2];
    // pipe(p);
    // write(p[1], "1", 2);
    for(int i=0;i<N;i++) {
        if(fork() == 0) {
            while(1) {
                char buf2[2];
                //read(p[0], buf2, sizeof(buf2));         //lock start
                union semun getval2;
                retval = semctl(semid,0,GETVAL,getval2.val);
                if(retval < 0) {
                    perror("semctl");
                    exit(0);
                }
                if(retval == 0)
                    exit(0);
                int ret = semop (semid, &sb, 1);
                if( ret == -1 && errno ==EAGAIN) {
                    printf("exiting..\n");
                    exit(0);
                }
                if(ret < 0)
                    perror("semop");
                union semun getval;
                retval = semctl(semid,0,GETVAL,getval.val);
                if(retval < 0) {
                    perror("semctl");
                    exit(0);
                }
                //write(p[1], "1", 2);                // lock release
                //printf("12Semval = %d\n", retval);
                int childno = T - retval;
                //printf("childno=%d\n",childno);
                //printf("retval=%d\n",retval);
                int sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                struct sockaddr_in servaddr;
                servaddr.sin_family = AF_INET;
                servaddr.sin_port = htons(portno);
                servaddr.sin_addr.s_addr = inet_addr(ipaddr);
                int conret = connect(sfd, (struct sockaddr*) &servaddr, sizeof(servaddr));
                if(conret < 0)
                    perror("connect");
                    // printf("childno = %d\n",childno);
                // int p[2];
                // pipe(p);
                // if(fork()==0){
                //     recvFromSocket(sfd, childno);
                //     exit(0);
                // }
                writeToSocket("JOIN child%d", childno, sfd);
                
                for(int j=0;j<M;j++) {
                    char a[MSG_SIZE];
                    sprintf(a, "UMSG child%d msg number %d from child%%d\n",childno, j);
                    writeToSocket(a, childno, sfd);
                    
                    //recvFromSocket(sfd);
                   
                }
                writeToSocket("LEAV %d", childno, sfd);
                
                //recvFromSocket(sfd);
                //close(sfd);
                 sleep(3);
                recvFromSocket(sfd, childno);
                recvFromSocket(sfd, childno);
                //shutdown(sfd,SHUT_RDWR);
                // send(p[1], "1", 1, 0);
                //wait(NULL);
            } 
        }
    }
    for(int i=0;i<N;i++)
        wait(NULL);
    return 0;
}

void writeToSocket(char b[MSG_SIZE], int d, int sfd) {
    char a[MSG_SIZE];
    sprintf(a, b, d);
    printf("sending: %s\n", a);
    int writeret = send(sfd, a, strlen(a)+1, 0);
    if(writeret < 0)    
        perror("write");
    sleep(1);
    // recvFromSocket(sfd, d);
}

void recvFromSocket(int sfd, int childno) {
    // char buf2[1];
    while(1){
        // if(read(p[0], buf2, sizeof(buf2)))
        char buf[MSG_SIZE];
        // fd_set
        // select(1, );
        int readret = recv(sfd, buf, MSG_SIZE,MSG_DONTWAIT);
        if(readret == 0) {
            printf("exiting read\n");
            exit(0);
        }
        if(readret < 0) {
          //  perror("read");
            return ;
        }
        printf("%d ----> %s\n", childno, buf);
    }
}