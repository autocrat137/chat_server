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
#include <signal.h>
#include <string.h>
#include<sys/ipc.h>
#include<sys/types.h>
#include<sys/msg.h>
#include<unistd.h>

#define MSGQ_PATH "c.c"
#define MSG_SIZE 2000
#define MAX_EVENTS 10

typedef struct my_msgbuf{
    long mtype;     //1->read,2->proc,3->write
    char mtext[MSG_SIZE];
    int *fd;
    int no_fd;
}my_msgbuf;

int main(int argc, char *argv[]) {
	//message queue init
	key_t key;
	int msqid;
	if((key = ftok(MSGQ_PATH,'A'))==-1){    //generate key
        perror("ftok:");
        exit(1);
    }
	 if((msqid = msgget(key,IPC_CREAT | 0644))==-1){     //create msgqueue
        perror("msgget:");
        exit(1);
    }


	struct sockaddr_in claddr, servaddr;
	int lfd = socket (AF_INET, SOCK_STREAM, 0);
	bzero (&servaddr, sizeof (servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl (INADDR_ANY);
	char portnumber[7];
	if(argc < 2) {
		printf("\nEnter port number: ");
		fflush(stdout);
		scanf("%s", portnumber);
	}
	servaddr.sin_port = htons (atoi (portnumber));
	int bindret = bind (lfd, (struct sockaddr *) &servaddr, sizeof (servaddr));
	if (bindret < 0)
		perror("bind");
	listen (lfd, 10);
	int efd = epoll_create(20);
	if(efd < 0)
		perror("epoll_create");
	struct epoll_event ev, evlist[MAX_EVENTS];
	ev.data.fd = efd;
	ev.events = EPOLLIN;
	if (epoll_ctl (efd, EPOLL_CTL_ADD, lfd, &ev) == -1)
		perror("epoll_ctl");

	if(fork()==0) {	//vedant
		if(fork()==0){
			readMessages(msqid);
			exit(0);
		}
		if(fork()==0){
			ProcessMessages(msqid);
			exit(0);
		}
		writeMessages(msqid);
	}
	else {//kunal
		while(1) {
			int ready = epoll_wait(efd, evlist, MAX_EVENTS, -1);
			if(ready == -1){
				if(errno == EINTR)
					continue;
				else
					perror("epoll_wait");
			}
			for(int i=0;i<ready;i++) {
				if(evlist[i].events & EPOLLIN) {
					if(evlist[i].data.fd == lfd) {
						int cfd = accept(lfd, (struct sockaddr *) &claddr, &claddr);
					}
				}
			}
		}
	}
	return 0;
}

void readMessages(msqid){
		struct my_msgbuf msg;
		msgrcv(msqid,&(msg),sizeof(msg),1,0);//get type 1 messages
}