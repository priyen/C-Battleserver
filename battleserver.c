/*
A4 by Priyenbhai Patel and Sneh Patel 
Built upon the base code from http://www.cdf.toronto.edu/~csc209h/winter/assignments/a4/simpleselect.c
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h> 
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef PORT
#define PORT 28111
#endif

struct client {
	int fd;
	struct in_addr ipaddr;
	struct client *next;
	char* name;
	char* dataBuffer; //To buffer data
	int bufferIndex; //Start writing from THIS index for the dataBuffer
	int engaging; //1 = in a match, 0 = not in a match
	int active; //1 = client's turn
	int speaking; //1 = buffer speak message
	struct client *opponent;
	struct client *previousOpponent; //Previous opponent client
	int hitpoints;
	int powermoves;
};

static void addclient( int fd, struct in_addr addr);
static void removeclient( int fd);
static void selectiveBroadcast( char *s, int size, struct client *expc);
static void printMenuForClient(struct client *p);
static void finishedMatch(struct client *p);
static void attemptMatchMake(struct client *p);
static void doHitAction(struct client *p, int dmg, int powermove);
static struct client *getLastClient();
static void dropClient(struct client *p);
int handleclient(struct client *p);

int bindandlisten(void);

//Head is the client list.
struct client *head = NULL;
fd_set allset;
fd_set rset;

int main(void) {
	//Seed random number generator
	srand((int)time(NULL)); 
	
	int clientfd, maxfd, nready;
	struct client *p;
	//struct client *head = NULL;
	socklen_t len;
	struct sockaddr_in q;
	struct timeval tv;

	int i;


	int listenfd = bindandlisten();
	// initialize allset and add listenfd to the
	// set of file descriptors passed into select
	FD_ZERO(&allset);
	FD_SET(listenfd, &allset);
	// maxfd identifies how far into the set to search
	maxfd = listenfd;

	while (1) {
		// make a copy of the set before we pass it into select
		rset = allset;
		/* timeout in seconds (You may not need to use a timeout for
		* your assignment)*/
		tv.tv_sec = 1;
		tv.tv_usec = 0;  /* and microseconds */

		nready = select(maxfd + 1, &rset, NULL, NULL, &tv);
		if (nready == 0) {
			continue;
		}

		if (nready == -1) {
			perror("select");
			continue;
		}

		if (FD_ISSET(listenfd, &rset)){
			//printf("a new client is connecting\n");
			len = sizeof(q);
			if ((clientfd = accept(listenfd, (struct sockaddr *)&q, &len)) < 0) {
				perror("accept");
				exit(1);
			}
			FD_SET(clientfd, &allset);
			if (clientfd > maxfd) {
				maxfd = clientfd;
			}
			//printf("connection from %s\n", inet_ntoa(q.sin_addr));
			addclient(clientfd, q.sin_addr);
			
			//Getting the client's name
			char question[] = "What is your name? ";
			write(clientfd, question, strlen(question));
		}

		for(i = 0; i <= maxfd; i++) {
			if (FD_ISSET(i, &rset)) {
				for (p = head; p != NULL; p = p->next) {
					if (p->fd == i) {
						int result = handleclient(p);
						if (result == -1) {						
							dropClient(p);
						}
						break;
					}
				}
			}
		}
	}
	return 0;
}

static void dropClient(struct client *p){	
	//Was this client in a match?
	if(p->engaging){//Yes the client was!
		char msg[1024];
		sprintf(msg, "\r\n--%s dropped. You win!\r\n", p->name);
		write((p->opponent)->fd, msg, strlen(msg));
		
		//Await next opponent 
		const char *awaiting = "\nAwaiting next opponent...\r\n";
		write((p->opponent)->fd, awaiting, strlen(awaiting));
		
		//Reset the client's stats
		finishedMatch(p->opponent);
	}
	if(p->name){//Only broadcast if this client was in the arena before (you'd have a name if you were);
		char broadcastMsg[1024];
		sprintf(broadcastMsg, "**%s leaves**\r\n", p->name);
		selectiveBroadcast( broadcastMsg, strlen(broadcastMsg), p);
	}
	
	int tmp_fd = p->fd;
	removeclient(p->fd);
	FD_CLR(tmp_fd, &allset);
	close(tmp_fd);	
}

//Print the appropriate menus based on the circumstances for the client
static void printMenuForClient(struct client *p){
	if(p->engaging == 1){//In a match
		char buffer[1024];
		sprintf(buffer, "Your hitpoints: %i\r\n", p->hitpoints);
		write(p->fd, buffer, strlen(buffer));
		
		char buffer2[1024];
		sprintf(buffer2, "Your powermoves: %i\r\n", p->powermoves);
		write(p->fd, buffer2, strlen(buffer2));			
		
		char buffer3[1024];
		sprintf(buffer3, "\n%s's hitpoints: %i\r\n", (p->opponent)->name, (p->opponent)->hitpoints);
		write(p->fd, buffer3, strlen(buffer3));
		
		if(p->active == 1){//p's turn			
			const char *attack = "\n(a)ttack";
			write(p->fd, attack, strlen(attack));
			
			if(p->powermoves > 0){
				const char *powermove = "\n(p)owermove";
				write(p->fd, powermove, strlen(powermove));
			}
			
			const char *speak = "\n(s)peak something\r\n";
			write(p->fd, speak, strlen(speak));
		}
		else{ 
			char buffer4[1024];
			sprintf(buffer4, "\nWaiting for %s to strike...", (p->opponent)->name);
			write(p->fd, buffer4, strlen(buffer4));
		}
	}
}

//A match has ended some way, whether it is a drop or win or lose, prepare client's stats for next time
static void finishedMatch(struct client *p){
	//Reset client stats
	free(p->dataBuffer);
	p->dataBuffer = malloc(sizeof(char)*1024);
	p->bufferIndex = 0;
	p->engaging = 0;
	p->active = 0;
	p->hitpoints = 0;
	p->powermoves = 0;
	p->speaking = 0;
	p->previousOpponent = p->opponent;
	p->opponent = NULL;
	
	struct client *last = getLastClient();
	if(p == head){//First client in the list
		if(!(p->next)){//Only client in list
			//Nothing to do
		}
		else{//Not the only client in list 
			struct client *temp = p;
			head = head->next;
			temp->next = NULL;
			last->next = temp;
		}
		return;
	}
	//Not top client, and not only one in the list
	//First check if p isn't already last:	
	if(last == p){//nothing to do if its already last
		return;
	}
	//Not top, not last
	struct client *px;
	for (px = head; px; px = px->next) {
		if(px->next == p){
			px->next = p->next;
			last->next = p;
			p->next = NULL;
			return;
		}
	}
	//Will never reach here, but just to take away warning
	return;
}

//Get the last client in the list
static struct client *getLastClient(){
	struct client *px;
	for (px = head; px; px = px->next) {
		if(!(px->next)){
			return px;
		}
	}
	//Will never get here, but just to take away the warning
	return head;
}

//Try to find a match for client p
static void attemptMatchMake(struct client *p){
	struct client *px;
	for (px = head; px; px = px->next) {
		if(px != p){
			if((px->previousOpponent == p && p->previousOpponent == px) || !(px->name) || !(p->name) || ((px->engaging == 1 || (px->engaging == 1)))){
				//Cant match if both verses each other recently, or client name is null indicating
				//They aren't in the arena yet and server hasn't received their name input
				//Also cant match if px is already in a match
			}
			else{//Can match, start the match
				int num = rand() % 100 + 1;
				struct client *first;
				struct client *second;
				if(num >= 50){
					first = p;
					second = px;
				}
				else{
					first = px;
					second = p;
				}
				//Notify the clients participating
				char firstMsg[1024];
				sprintf(firstMsg, "You engage %s!\r\n", second->name);
				write(first->fd, firstMsg, strlen(firstMsg));
				
				char secondMsg[1024];
				sprintf(secondMsg, "You engage %s!\r\n", first->name);
				write(second->fd, secondMsg, strlen(secondMsg));
				
				//Setup first client's stats 
				first->engaging = 1;
				first->active = 1;
				first->opponent = second;
				first->hitpoints = rand() % 10 + 20;
				first->powermoves = rand() % 2 + 1;
				first->speaking = 0;
				
				//Setup second client's stats 
				second->engaging = 1;
				second->active = 0;
				second->opponent = first;
				second->hitpoints = rand() % 10 + 20;
				second->powermoves = rand() % 2 + 1;
				second->speaking = 0;
				
				//Print new menus
				printMenuForClient(first);
				printMenuForClient(second);
			}
		}
	}
}

//Do a hit action, whether it is a regular move or powermove
static void doHitAction(struct client *p, int dmg, int powermove){
	int num = rand() % 99 + 1;
	if(num <= 50 && powermove == 1){
		dmg = 0; //Powermove missed
	}
	
	if(dmg > 0){
		//Notify client they hit the opponent
		char buffer[1024];
		sprintf(buffer, "\r\nYou hit %s for %i damage!\r\n", (p->opponent)->name, dmg);
		write(p->fd, buffer, strlen(buffer));
		(p->opponent)->hitpoints = (p->opponent)->hitpoints - dmg;
		
		//Notify opponent client hit them
		char buffer2[1024];
		sprintf(buffer2, "\r\n%s hits you for %i damage!\r\n", p->name, dmg);
		write((p->opponent)->fd, buffer2, strlen(buffer2));
	}
	else{ 
		//Notify client they missed the opponent
		const char *missed = "\r\nYou missed!\r\n";
		write(p->fd, missed, strlen(missed));
		
		//Notify opponent client missed them
		char buffer2[1024];
		sprintf(buffer2, "\r\n\n%s missed you!\r\n", p->name);
		write((p->opponent)->fd, buffer2, strlen(buffer2));
	}
	if((p->opponent)->hitpoints <= 0){ //p won the game
		//Tell opponent they are no match for this client 
		char buffer3[1024];
		sprintf(buffer3, "You are no match for %s. You scurry away...\r\n", p->name);
		write((p->opponent)->fd, buffer3, strlen(buffer3));
		//Tell client they just won
		char buffer4[1024];
		sprintf(buffer4, "%s gives up. You win!\r\n", (p->opponent)->name);
		write(p->fd, buffer4, strlen(buffer4));
		//Await next opponent 
		const char *awaiting = "\nAwaiting next opponent...\r\n";
		write(p->fd, awaiting, strlen(awaiting));
		write((p->opponent)->fd, awaiting, strlen(awaiting));
		//Reset values for these clients 
		struct client *temp = p->opponent;
		finishedMatch(p);
		finishedMatch(temp);
		//Try to match these clients with some enemy
		attemptMatchMake(p);
		attemptMatchMake(temp);
	}
	else{//Opponents move now
		p->active = 0;
		(p->opponent)->active = 1;
		printMenuForClient(p);
		printMenuForClient(p->opponent);
	}	
}

//Take in any client's input and analyse what to do
int handleclient(struct client *p) {
	char buf[256];
	int len = read(p->fd, buf, sizeof(buf) - 1);
	if (len > 0) {
		//Entered arena for first time, buffer the client name input
		if(!(p->name)){
			//Prevent overflow, we already gave user a lot of bytes to input
			if(p->bufferIndex + len > 800){
				dropClient(p);
				return 0;
			}
			strcpy(&(p->dataBuffer)[p->bufferIndex], buf);
			p->bufferIndex = p->bufferIndex + len;
			if(buf[len-1] != '\n'){
				//do nothing, still buffering
			}
			else{
				p->dataBuffer[p->bufferIndex - 1] = '\0';
				p->name = malloc(sizeof(char)*strlen(p->dataBuffer));
				
				strcpy(p->name, p->dataBuffer);
				free(p->dataBuffer);
				p->dataBuffer = malloc(sizeof(char)*1024);
				p->bufferIndex = 0;
				
				char broadcastMsg[1024];
				sprintf(broadcastMsg, "**%s enters the arena**\r\n", p->name);
				selectiveBroadcast(broadcastMsg, strlen(broadcastMsg), p);
				
				char clientMsg[1024];
				sprintf(clientMsg, "Welcome, %s! Awaiting opponent...\r\n", p->name);
				write(p->fd, clientMsg, strlen(clientMsg));
				
				attemptMatchMake(p);
			}//Client is in the arena but provided some sort of input
		}
		else{//In a match?
			if(p->engaging == 1){ //In a match
				if(p->active == 1){//p's turn
					if(p->speaking == 0){//p isn't speaking
						int dmg = 0;
						if(buf[0] == 'a'){//p does regular attack
							dmg = (rand() % 4) + 2; //range 2 to 6
							doHitAction(p, dmg, 0);
						}
						else{
							if(buf[0] == 'p' && p->powermoves > 0){//p does powermove
								p->powermoves = p->powermoves - 1;
								dmg = (rand() % 4 + 2) * 3;
								doHitAction(p, dmg, 1);
							}
							else{
								if(buf[0] == 's'){//p goes to speak mode
									p->speaking = 1;
									const char *speak = "\r\nSpeak: ";
									write(p->fd, speak, strlen(speak));
								}
							}
						}
					}
					else{//p is speaking, so buffer that client's data
						//Prevent overflow, we already gave user a lot of bytes to input
						if(p->bufferIndex + len > 800){
							dropClient(p);
							return 0;
						}
						strcpy(&(p->dataBuffer)[p->bufferIndex], buf);
						p->bufferIndex = p->bufferIndex + len;
						if(buf[len-1] != '\n'){
							//Do nothing, still buffering
						}
						else{
							//done buffering
							p->dataBuffer[p->bufferIndex - 1] = '\0';
							
							//Notify speaker what they spoke of
							char youSpeakMsg[1024];
							sprintf(youSpeakMsg, "You speak: %s\r\n", p->dataBuffer);
							write(p->fd, youSpeakMsg, strlen(youSpeakMsg));
							
							//Notify opponent their enemy spoke to them 
							char opponentSpokeMsg[1024];
							sprintf(opponentSpokeMsg, "\r\n%s takes a break to tell you:\r\n", p->name);
							write((p->opponent)->fd, opponentSpokeMsg, strlen(opponentSpokeMsg));
							write((p->opponent)->fd, p->dataBuffer, strlen(p->dataBuffer));							
							write((p->opponent)->fd,"\r\n", 3);
							free(p->dataBuffer);
							p->dataBuffer = malloc(sizeof(char)*1024);
							p->bufferIndex = 0;
							p->speaking = 0;
							printMenuForClient(p);
							printMenuForClient(p->opponent);
						}
					}
				}
				else{//Not p's turn
					//Do nothing
				}
			}//Not in a match
			else{
				//Do nothing
			}
		}
		return 0;
	} else if (len == 0) {
		return -1;
	} else { // shouldn't happen
		perror("read");
		return -1;
	}
}

/* bind and listen, abort on error
* returns FD of listening socket
*/
int bindandlisten(void) {
	struct sockaddr_in r;
	int listenfd;

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	int yes = 1;
	if ((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
		perror("setsockopt");
	}
	memset(&r, '\0', sizeof(r));
	r.sin_family = AF_INET;
	r.sin_addr.s_addr = INADDR_ANY;
	r.sin_port = htons(PORT);

	if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
		perror("bind");
		exit(1);
	}

	if (listen(listenfd, 5)) {
		perror("listen");
		exit(1);
	}
	return listenfd;
}

static void addclient( int fd, struct in_addr addr) {
	struct client *p = malloc(sizeof(struct client));
	if (!p) {
		perror("malloc");
		exit(1);
	}
	//Setup appropriate default values for the client after they enter arena
	p->fd = fd;
	p->ipaddr = addr;
	p->next = head;
	p->name = NULL;
	p->dataBuffer = malloc(sizeof(char)*1024);
	p->bufferIndex = 0;
	p->engaging = 0;
	p->active = 0;
	p->opponent = NULL;
	p->hitpoints = 0;
	p->powermoves = 0;
	p->speaking = 0;
	p->previousOpponent = NULL;
	head = p;
	return;
}

//Remove the client from the list
static void removeclient( int fd) {
	struct client **p;

	for (p = &head; *p && (*p)->fd != fd; p = &(*p)->next)
	;
	// Now, p points to (1) top, or (2) a pointer to another client
	// This avoids a special case for removing the head of the list
	if (*p) {
		struct client *t = (*p)->next;
		//Remove 'previousOpponent' references in client list 
		struct client *temp = head;
		while(temp != NULL){
			if(temp->previousOpponent){
				if((temp->previousOpponent)->fd == fd){
					temp->previousOpponent = NULL;
				}
			}
			temp = temp->next;
		}
		free((*p)->dataBuffer);//Just in case
		free(*p);
		*p = t;
	} else {
		fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
		fd);
	}
	return;
}

//Broadcast to all BUT specified client in argument 4
static void selectiveBroadcast( char *s, int size, struct client *expc) {
	struct client *p;
	for (p = head; p; p = p->next) {
		if(p != expc){
			int rval = write(p->fd, s, size);
			if(rval < 0){ //Checking write return value and removing client if it doesnt exist anymore
				dropClient(p);
			}
		}
	}
}