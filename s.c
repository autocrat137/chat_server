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
#define MSG_SIZE 50
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

void readMessages(int efd,int msqid);
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
	// struct msqid_ds buf;
	// int ret = msgctl (msqid, IPC_STAT, &buf);
	// buf.msg_qbytes = 100000;
	// ret = msgctl (msqid, IPC_SET, &buf);
	// if(ret<0){
	// 	perror("msgq");
	// }
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
					msgsnd(msqid,&(msg),sizeof(msg),0);
					
				}
			}
			else if(evlist[i].events & EPOLLOUT) {	//add in msgqueue
				struct epoll_event ev;
				ev.data.fd = evlist[i].data.fd;
				ev.events = EPOLLOUT;
				epoll_ctl(efd, EPOLL_CTL_DEL, evlist[i].data.fd, &ev);
				ev.events = EPOLLIN;
				epoll_ctl(efd, EPOLL_CTL_ADD, evlist[i].data.fd, &ev);
				struct my_msgbuf msg;
				msg.fd = evlist[i].data.fd;
				struct my_msgbuf msg2;
				msgrcv(msqid, &msg2, sizeof(msg2), 4 + msg.fd, 0);
				msg2.mtype = 3;		//write
				msg2.fd = evlist[i].data.fd;
				msgsnd(msqid, &msg2, sizeof(msg2), 0);
			}
			
		}
		//after iterating all events
		readMessages(efd,msqid);
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
		int len = strlen(msg.mtext);
		msg.mtext[len] = '\n';
		msg.mtext[len + 1] = '\0';
		int w = write(msg.fd, msg.mtext, strlen(msg.mtext));	//write to socket
		if(w < 0)
			perror("socket write");
	}
}

void readMessages(int efd,int msqid){
	struct my_msgbuf msg;
	struct epoll_event ev;
	while(1){
		int recv = msgrcv(msqid,&(msg),sizeof(msg),1,IPC_NOWAIT);//get type 1 messages
		if(recv ==-1 && errno == ENOMSG){
			break;
		}
		else if(recv < 0) {
			perror("readMsgs: msgrcv error");
			break;
		}
		int r;
		memset(msg.mtext,0,MSG_SIZE);
		r = read(msg.fd,msg.mtext,MSG_SIZE);
		//if(r < 0)
			//perror("read error");
		if(r == 0){
			printf("FIN Received\n");
			//close(msg.fd);
			shutdown(msg.fd,SHUT_RD);
			ev.data.fd = msg.fd;
			ev.events=EPOLLIN;
			int ctlret = epoll_ctl(efd, EPOLL_CTL_DEL, ev.data.fd, &ev);
			break;
		}
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
		int i=0;
		char buf[5];
		strncpy(buf,msg.mtext,4);
		buf[4]='\0';
		
		if(strcmp(buf,"JOIN")==0){
			char name[MAX_NAME];
			sscanf(msg.mtext, "%s %s", buf, name);
			char retmsg[100];
			head = addClient(head,name,msg.fd, retmsg);
			strcpy(msg.mtext,retmsg);
			ev.data.fd = msg.fd;
			msg.mtype = msg.fd + 4;
		}
		else if(strcmp(buf,"LIST")==0){
			char *list_msg = (char*)malloc(sizeof(char) * MSG_SIZE);
			list_msg[0] = '\0';
			sprintf(list_msg,"List of all online users:\n");
			temp=head;
			char toAdd[100];
			while(temp!=NULL){
				sprintf(toAdd, "%s\t", temp->name);
				strcat(list_msg,toAdd);
				temp=temp->next;
			}
			printf("Retrieving the list of all online users\n");
			ev.data.fd = msg.fd;
			strcpy(msg.mtext, list_msg);
			msg.mtype = ev.data.fd+4;
		}
		else if(strcmp(buf,"UMSG")==0){
			int toSelf=0;
			Client sender = findClientbyfd(head, msg.fd);
			if(sender==NULL){
				strcpy(msg.mtext, "You haven't joined a group");
				toSelf = 1;
				break;
			}
			char tname[MAX_NAME], umsg[MAX_NAME];
			sscanf(msg.mtext, "%s %s %[^\n]", buf, tname, umsg);
			sprintf(msg.mtext,"[%s]->",sender->name);
			strcat(msg.mtext, umsg);
			Client cli = findClientbyName(head,tname);
			if(toSelf == 0 && cli==NULL){
				printf("Client %s is not online\n",tname);
				sprintf(msg.mtext, "Client %s is not online", tname);
				toSelf = 1;
			}
			if(toSelf == 1){//send to itself
				msg.mtype = msg.fd+4;
				ev.data.fd = msg.fd;
			}
			else{	//send to user
				printf("UMSG from %s to %s\n",sender->name, cli->name);
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
			Client cli = findClientbyfd(head,msg.fd);
			if(cli == NULL) {
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
			memset(bmsg,0,MSG_SIZE);
			sscanf(msg.mtext, "%s %[^\n]", buf, bmsg);
			temp=head;
			char retmsg[MSG_SIZE];
			memset(retmsg, 0, sizeof(retmsg));
			sprintf(msg.mtext,"[%s]->",cli->name);
			strcat(msg.mtext, bmsg);
			printf("BMSG from client %s\n", cli->name);
			while(temp!=NULL){	
				// if(temp->fd==msg.fd){
				// 	temp=temp->next;
				// 	continue;
				// }
				
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
			char retmsg[100];
			head = deleteClientbyfd(head, msg.fd, retmsg);
			strcpy(msg.mtext, retmsg);
			ev.data.fd = msg.fd;
			msg.mtype = msg.fd+4;
		}
		else{
			Client cli = findClientbyfd(head,msg.fd);
			if(cli == NULL) 
				strcpy(msg.mtext, "You are not online");
			else{
				char *text = strdup(msg.mtext);
				sprintf(msg.mtext, "Command %s not recognized",buf);
			}
			msg.mtype = 4+msg.fd;
			ev.data.fd = msg.fd;
			int ctlret = epoll_ctl(efd, EPOLL_CTL_DEL, ev.data.fd, &ev);
			if(ctlret < 0)
				perror("296 ctlrmerror");
			ev.events |= EPOLLOUT;
			int eret = epoll_ctl(efd, EPOLL_CTL_ADD, ev.data.fd, &ev);
			msgsnd(msqid, &msg, sizeof(msg), 0);
			if(strlen(buf)!=0)
				printf("Command %s not recognized\n",buf);
			continue;
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
	if(y != NULL){
		printf("This terminal already has a client %s\n",y->name);
		sprintf(retmsg, "This terminal already has a client with name %s", y->name);
		return head;
	}
	else if(x != NULL){
		printf("Client %s already joined\n",name);
		sprintf(retmsg, "There is already a user with name %s", name);
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
		printf("Client %s left group \n",head->name);
		strcpy(retmsg,"You left the group");
		free(head);
		return temp;
	}
	temp=head->next;prev=head;
	while(temp!=NULL ){
		if(temp->fd==fd){	//if in middle node
			prev->next=temp->next;
			printf("Client %s left group \n",temp->name);
			strcpy(retmsg,"You left the group");
			free(temp);
			return head;
		}
		prev=temp;
		temp=temp->next;
	}
	return head;
}