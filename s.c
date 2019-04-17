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
#include <sys/types.h>
#include <sys/msg.h>
#include <unistd.h>

#define MSGQ_PATH "key.h"
#define MSG_SIZE 2000
#define MAX_EVENTS 10
#define CLIENT_SIZE 20
#define MAX_NAME 30

typedef struct my_msgbuf{
    long mtype;     //1->read,2->proc,3->write
    char mtext[MSG_SIZE];
    int fd;
}my_msgbuf;

struct client{
	int fd;
	char name[CLIENT_SIZE];
	struct client *next;
};

typedef struct client *Client;

void readMessages(int msqid);
void writeMessages(int msqid);
Client processMessages(int msqid,int efd,Client head);
Client addClient(Client head,char *name,int fd, char retmsg[100]);
Client findClientbyName(Client head,char *name);
Client newClient(int fd,char *name);
Client deleteClientbyfd(Client head,int fd, char retmsg[100]);
Client findClientbyfd(Client head,int fd);


int main(int argc, char *argv[]) {
	//message queue init
	key_t key;
	int msqid;
	if((key = ftok(MSGQ_PATH,'A'))==-1){    //generate key
        perror("ftok:");
        exit(1);
    }
	if((msqid = msgget(key,IPC_CREAT | 0644))==-1){     //create msgqueue
        perror("msgget");
        exit(1);
    }
	msgctl(msqid, IPC_RMID, NULL);
	if((msqid = msgget(key,IPC_CREAT | 0644))==-1){     //create msgqueue
        perror("msgget");
        exit(1);
    }

	struct sockaddr_in claddr, servaddr;
	Client head=NULL;
	int clilen;
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
	int lret = listen (lfd, 10);
	if(lret < 0)
		perror("error in listen");
	int efd = epoll_create(20);
	printf("EFD in main %d\n", efd);
	if(efd < 0)
		perror("epoll_create");
	struct epoll_event ev, evlist[MAX_EVENTS];
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	if (epoll_ctl (efd, EPOLL_CTL_ADD, lfd, &ev) == -1){
		perror("epoll_ctl");
	}
	while(1) {
		struct my_msgbuf msg;
		// if(msgrcv(msqid, &msg, sizeof(msg), 4, IPC_NOWAIT) == 0) {
		// 	ev.events = EPOLLOUT;
		// 	ev.data.fd = msg.fd;
		// 	//ev.data.ptr = (char *) malloc (sizeof(char) * MSG_SIZE);
		// 	strcpy(ev.data.ptr, msg.mtext);
		// 	int eret = epoll_ctl(efd, EPOLL_CTL_ADD, msg.fd, &ev);
		// 	if(eret < 0)
		// 		perror("epoll_ctl write error ");
		// }
		int ready = epoll_wait(efd, evlist, MAX_EVENTS, -1);
		if(ready == -1){
			if(errno == EINTR)
				continue;
			else
				perror("epoll_wait");
		}

		for(int i=0;i<ready;i++) {
			if(evlist[i].events & EPOLLIN) {		// accepting a client
				if(evlist[i].data.fd == lfd) {
					clilen = sizeof(claddr);
					int cfd = accept(lfd, (struct sockaddr *) &claddr, &clilen);
					ev.events = EPOLLIN;
					ev.data.fd = cfd;
					int eret = epoll_ctl(efd, EPOLL_CTL_ADD, cfd , &ev);
					if(eret < 0)
						perror("Epoll add error");
					char ip[128];
					inet_ntop (AF_INET, &(claddr.sin_addr), ip, 128);
					printf("Got client %s:%d \n", ip, ntohs (claddr.sin_port));
				}

				else {								// when server rcvs and gives the msg to READY
					struct my_msgbuf msg;
					memset(msg.mtext,0,MSG_SIZE);
					msg.mtype=1;		//reading queue
					msg.fd = evlist[i].data.fd;
					ev.data.fd = msg.fd;
					ev.events = EPOLLIN;
					// int rmret = epoll_ctl(efd, EPOLL_CTL_DEL, msg.fd, &ev);
					// if(rmret < 0)
					// 	perror("Main: remove error");
					msgsnd(msqid,&(msg),sizeof(msg),0);
					
				}
			}
			else if(evlist[i].events & EPOLLOUT) {	//add in msgqueue
				printf("in Epollout\n");
				struct epoll_event ev;
				ev.data.fd = evlist[i].data.fd;
				ev.events = EPOLLOUT;
				epoll_ctl(efd, EPOLL_CTL_DEL, evlist[i].data.fd, &ev);
				ev.events = EPOLLIN;
				epoll_ctl(efd, EPOLL_CTL_ADD, evlist[i].data.fd, &ev);
				struct my_msgbuf msg;
				msg.fd = evlist[i].data.fd;
				struct my_msgbuf msg2;
				// strcpy(msg.mtext, evlist[i].data.ptr);
				msgrcv(msqid, &msg2, sizeof(msg2), 4 + msg.fd, 0);
				// printf("");
				msg2.mtype = 3;		//write
				msg2.fd = msg.fd;
				msgsnd(msqid, &msg2, sizeof(msg2), 0);
			}
			
		}
		//after iterating all events
		readMessages(msqid);
		head = processMessages(msqid,efd,head);
		writeMessages(msqid);
	}
	return 0;
}

void writeMessages(int msqid) {
	struct my_msgbuf msg;
	while(1){
		int rcvret = msgrcv(msqid, &msg, sizeof(msg), 3, IPC_NOWAIT);
		if(rcvret ==-1 && errno == ENOMSG){
			//perror("Msgrcv error");
			break;
		}
		else if(rcvret < 0) {
			perror("writeMsgs: msgrcv error");
			break;
		}
		printf("Write: fd=%d, data=%s\n", msg.fd, msg.mtext);
		int len = strlen(msg.mtext);
		msg.mtext[len] = '\n';
		msg.mtext[len + 1] = '\0';
		int w = write(msg.fd, msg.mtext, strlen(msg.mtext));	//write to socket
		printf("written\n");
		if(w < 0)
			perror("socket write");
	}
}

void readMessages(int msqid){
	struct my_msgbuf msg;
	while(1){
		printf("In readMsgs [msgrcv]\n");
		// printf("Before msgrcv\n");
		int recv = msgrcv(msqid,&(msg),sizeof(msg),1,IPC_NOWAIT);//get type 1 messages
		printf("readMsgs: after msgrcv\n");
		if(recv ==-1 && errno == ENOMSG){
			//perror("Msgrcv error");
			break;
		}
		else if(recv < 0) {
			perror("writeMsgs: msgrcv error");
			break;
		}// printf("After msgrcv\n");
		int r;
		memset(msg.mtext,0,MSG_SIZE);
		printf("readMsgs ");
		r = read(msg.fd,msg.mtext,MSG_SIZE);
		//printf("after read\n");
		if(r < 0)
			perror("read error");
		if(r == 0)
			printf("No bytes read\n");
		else
			printf("msg read from socket = %s\n",msg.mtext);
		msg.mtype=2;	
		msgsnd(msqid,&(msg),sizeof(msg),0);	//send message type=2 for processing
	}
}

Client processMessages(int msqid, int efd,Client head){
	struct my_msgbuf msg;
	struct epoll_event ev;
	ev.events = EPOLLIN;
	Client temp;
	while(1) { 
		int rcv = msgrcv(msqid,&(msg),sizeof(msg),2,IPC_NOWAIT);		//get type 2 messages
		if(rcv ==-1 && errno == ENOMSG){
			break;
		}
		else if(rcv < 0) {
			perror("writeMsgs: msgrcv error");
			break;
		}
		printf("After msgrcv, msg.mtext=%s\n", msg.mtext);
		//decode it JOIN,UMSG,BMSG,LEAV and set EPOLLOUT for the fds

		int i=0;
		char buf[5];
		strncpy(buf,msg.mtext,4);
		buf[4]='\0';
		
		if(strcmp(buf,"JOIN")==0){
			char name[MAX_NAME];
			sscanf(msg.mtext, "%s %s", buf, name);
			printf("Join is called with name=%s and fd=%d\n", name, msg.fd);
			char retmsg[100];
			head = addClient(head,name,msg.fd, retmsg);
			strcpy(msg.mtext,retmsg);
			ev.data.fd = msg.fd;
			msg.mtype = msg.fd + 4;
		}
		else if(strcmp(buf,"LIST")==0){
			char *list_msg = (char*)malloc(sizeof(char) * MSG_SIZE);
			list_msg[0] = '\0';
			temp=head;
			printf("Inside LIST\n");
			char toAdd[100];
			while(temp!=NULL){
				printf("temp->name =%s\n",temp->name);
				sprintf(toAdd, "%s\n", temp->name);
				strcat(list_msg,toAdd);
				temp=temp->next;
			}
			printf("Concated list=%s\n", list_msg);
			ev.data.fd = msg.fd;
			strcpy(msg.mtext, list_msg);
			msg.mtype = ev.data.fd+4;
		}
		else if(strcmp(buf,"UMSG")==0){
			int toSelf=0;
			Client cli = findClientbyfd(head, msg.fd);
			char tname[MAX_NAME], umsg[MAX_NAME];
			sscanf(msg.mtext, "%s %s %[^\n]", buf, tname, umsg);
			strcpy(msg.mtext, umsg);
			if(cli==NULL){
				strcpy(msg.mtext, "You haven't joined a group");
				toSelf = 1;
			}
			cli = findClientbyName(head,tname);
			if(toSelf == 0 && cli==NULL){
				printf("Client %s is not online\n",tname);
				sprintf(msg.mtext, "Client %s is not online", umsg);
				toSelf = 1;
			}
			if(toSelf == 1){//send to itself
				msg.mtype = msg.fd+4;
				ev.data.fd = msg.fd;
			}
			else{	//send to user
				msg.mtype = cli->fd + 4;
				ev.data.fd = cli->fd;
			}
			ev.events |= EPOLLOUT;
			int ctlret = epoll_ctl(efd, EPOLL_CTL_DEL, ev.data.fd, &ev);
			if(ctlret < 0)
				perror("278 ctlrmerror");
			

			int eret = epoll_ctl(efd, EPOLL_CTL_ADD, ev.data.fd, &ev);
			msgsnd(msqid,&(msg),sizeof(msg),0);
			continue;
		}
		else if(strcmp(buf,"BMSG")==0){
			if(findClientbyfd(head, msg.fd) == NULL) {
				memset(msg.mtext, 0, MSG_SIZE);
				strcpy(msg.mtext, "You haven't joined a group");
				msg.mtype = 4+msg.fd;
				ev.data.fd = msg.fd;
				int ctlret = epoll_ctl(efd, EPOLL_CTL_DEL, ev.data.fd, &ev);
				if(ctlret < 0)
					perror("296 ctlrmerror");
				ev.events |= EPOLLOUT;
				int eret = epoll_ctl(efd, EPOLL_CTL_ADD, ev.data.fd, &ev);
				msgsnd(msqid, &msg, sizeof(msg), 0);
				continue;
			}
			char bmsg[MSG_SIZE];
			sscanf(msg.mtext, "%s %[^\n]", buf, bmsg);
			temp=head;
			char retmsg[MSG_SIZE];
			memset(retmsg, 0, sizeof(retmsg));
			strcpy(msg.mtext, bmsg);
			while(temp!=NULL){	
				if(temp->fd==msg.fd){
					temp=temp->next;
					continue;
				}
				printf("BMSG Stuff: fd=%d bmsg=%s\n", temp->fd, bmsg);
				
				// msg.fd = temp->fd+4;
				ev.data.fd = temp->fd;
				msg.mtype = temp->fd+4;
				int ctlret = epoll_ctl(efd, EPOLL_CTL_DEL, temp->fd, &ev);
				if(ctlret < 0)
					perror("296 ctlrmerror");
				ev.events |= EPOLLOUT;
				int eret = epoll_ctl(efd, EPOLL_CTL_ADD, temp->fd, &ev);
				msgsnd(msqid,&(msg),sizeof(msg),0);
				temp=temp->next;
			}
			continue;
		}
		else if(strcmp(buf,"LEAV")==0) {
			if(findClientbyfd(head, msg.fd) == NULL) {
				strcpy(msg.mtext, "You are not online");
				msg.mtype = 3;
				msgsnd(msqid, &msg, sizeof(msg), 0);
				continue;
			}
			char retmsg[100];
			head = deleteClientbyfd(head, msg.fd, retmsg);
			strcpy(msg.mtext, retmsg);
			ev.data.fd = msg.fd;
			msg.mtype = msg.fd+4;
		}
		int ctlret = epoll_ctl(efd, EPOLL_CTL_DEL, ev.data.fd, &ev);
		if(ctlret < 0)
			perror("310 ctlrmerror");
		ev.events |= EPOLLOUT;
		int eret = epoll_ctl(efd, EPOLL_CTL_ADD, ev.data.fd, &ev);
		if(eret < 0)
			perror("epoll_write");
		msgsnd(msqid,&(msg),sizeof(msg),0);
	}
	return head;
}

Client addClient(Client head,char *name,int fd,char retmsg[100]){	//makes client as new head
	
	if(head==NULL){
		printf("Client %s joined\n",name);
		strcpy(retmsg,"You are online");
		head = newClient(fd,name);
		return head;
	}
	Client x = findClientbyName(head,name);
	Client y = findClientbyfd(head,fd);
	if(x != NULL){
		printf("Client %s already joined\n",name);
		sprintf(retmsg, "There is already a user with name %s", name);
		return head;
	}
	else if(y != NULL){
		printf("Client %s already joined\n",name);
		sprintf(retmsg, "This terminal already has a client with name %s", y->name);
		return head;
	}
	printf("Client %s joined\n",name);
	strcpy(retmsg,"You are online");
	Client cli = newClient(fd,name);
	cli->next=head;
	return cli;
}

Client newClient(int fd,char *name){	//creates new Client
	Client cli = (Client)malloc(sizeof(struct client));
	cli->fd=fd;
	strcpy(cli->name,name);
	cli->next=NULL;
	return cli;
}

Client findClientbyName(Client head,char *name){
	Client temp = head;
	
	while(temp!=NULL && strcmp(temp->name,name)!=0)
		temp=temp->next;

	return temp;	//if client found,returns client,else NULL
}

Client findClientbyfd(Client head,int fd){
	Client temp = head;
	
	while(temp!=NULL && temp->fd!=fd)
		temp=temp->next;

	return temp;	//if client found,returns client,else NULL
}

Client deleteClientbyfd(Client head,int fd,char retmsg[100]){
	Client prev,temp=head;
	if(head->fd==fd){	//if fd in first node
		temp=head->next;
		printf("Client %s left group d\n",head->name);
		strcpy(retmsg,"You left the group");
		free(head);
		return temp;
	}
	temp=head->next;prev=head;
	while(temp!=NULL ){
		if(temp->fd==fd){	//if in middle node
			prev->next=temp->next;
			printf("Client %s left group d\n",temp->name);
			strcpy(retmsg,"You left the group");
			free(temp);
			return head;
		}
		prev=temp;
		temp=temp->next;
	}
	return head;
}

/*
void send_fd(int socket, int *fds, int n)  // send fd by socket
{
        struct msghdr msg = {0};
        struct cmsghdr *cmsg;
        char buf[CMSG_SPACE(n * sizeof(int))], dup[256];
        memset(buf, ‘\0’, sizeof(buf));
        struct iovec io = { .iov_base = &dup, .iov_len = sizeof(dup) };

        msg.msg_iov = &io;
        msg.msg_iovlen = 1;
        msg.msg_control = buf;
        msg.msg_controllen = sizeof(buf);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(n * sizeof(int));

        memcpy ((int *) CMSG_DATA(cmsg), fds, n * sizeof (int));

        if (sendmsg (socket, &msg, 0) < 0)
                handle_error (“Failed to send message”);
}*/
/*
int * recv_fd(int socket, int n) {
        int *fds = malloc (n * sizeof(int));
        struct msghdr msg = {0};
        struct cmsghdr *cmsg;
        char buf[CMSG_SPACE(n * sizeof(int))], dup[256];
        memset(buf, ‘\0’, sizeof(buf));
        struct iovec io = { .iov_base = &dup, .iov_len = sizeof(dup) };

        msg.msg_iov = &io;
        msg.msg_iovlen = 1;
        msg.msg_control = buf;
        msg.msg_controllen = sizeof(buf);

        if (recvmsg (socket, &msg, 0) < 0)
                handle_error (“Failed to receive message”);

        cmsg = CMSG_FIRSTHDR(&msg);

        memcpy (fds, (int *) CMSG_DATA(cmsg), n * sizeof(int));

        return fds;
}*/