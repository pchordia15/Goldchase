//    GoldChase - Part3
//    Priyanka Chordia.
//    Using Sockets.


#include "goldchase.h"
#include "Map.h"
#include <iostream>
#include <fstream>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mqueue.h>
#include <sstream>
#include <sys/socket.h>
#include <netdb.h>

#define server "server"
#define client "client"

using namespace std;

struct GameBoard {
	int rows; //4 bytes
	int cols; //4 bytes
	unsigned char players;
	pid_t pid[5];
	int daemonID;
	unsigned char map[0];
};

string mq_name[5] = {"/pcq0","/pcq1","/pcq2","/pcq3","/pcq4"};
Map *goldMine = NULL;
mqd_t readqueue_fd;
bool wonf = false;
GameBoard *gb;
int player_number = 0;
int position = 0;
char active_player;
sem_t *my_sem_ptr;
const char* portno="42424";
int sockfd; //file descriptor for the socket
int new_sockfd;
unsigned char *mapPointer;
string addrs;
string runningSystem;

	template<typename T>
int READ(int fd, T* obj_ptr, int count)
{
	int remaining_bytes = count;
	int byte_read = 0;
	int total_bytes_read = 0;
	char *ptr = (char *)obj_ptr;
	while(remaining_bytes > 0)
	{
		byte_read = read(fd,ptr+total_bytes_read,remaining_bytes);
		if(byte_read == -1) 
		{   
			if(errno == EINTR)
				continue;
			else
				return -1; 
		}   
		total_bytes_read += byte_read;
		remaining_bytes -= byte_read;
	}
	return byte_read;
}

	template<typename T>
int WRITE(int fd, T* obj_ptr, int count)
{
	int remaining_bytes = count;
	int total_bytes_read = 0;
	int byte_sent = 0;
	char *ptr = (char *)obj_ptr;
	while(remaining_bytes > 0)
	{
		byte_sent = write(fd,ptr+total_bytes_read,remaining_bytes);
		if(byte_sent == -1)
		{
			if(errno == EINTR)
				continue;
			else
				return -1;
		}
		total_bytes_read += byte_sent;
		remaining_bytes -= byte_sent;
	}
	return byte_sent;
}


void handle_map(int sigNum)
{
	if(goldMine)
		goldMine->drawMap();
}

void readqueue(int sigNum)
{
	if(goldMine == NULL)
		return;
	struct sigevent mq_notification_event;
	mq_notification_event.sigev_notify=SIGEV_SIGNAL;
	mq_notification_event.sigev_signo=SIGUSR2;
	mq_notify(readqueue_fd, &mq_notification_event);

	//read a message
	int err;
	char msg[121];
	memset(msg, 0, 121);//set all characters to '\0'
	while((err=mq_receive(readqueue_fd, msg, 120, NULL))!=-1)
	{
		string message(msg);
		goldMine->postNotice(message.c_str());
		memset(msg, 0, 121);//set all characters to '\0'
	}

	if(errno!=EAGAIN)
	{
		perror("mq_receive");
		exit(1);
	}
}

void writetoMqueue(unsigned char player, int player_num2)
{
	int i;
	unsigned int players, player_num;
	string temp, ss, message;
	players = gb->players;
	players &= ~player;
	player_num = goldMine->getPlayer(players);
	if(player_num ==0)
		return;
	temp = goldMine->getMessage();
	ss = to_string(player_num2);
	message = "Player " + ss + " says: ";
	message.append(temp);

	switch(player_num)
	{
		case G_PLR0: i = 0;
			     break;
		case G_PLR1: i = 1;
			     break;
		case G_PLR2: i = 2;
			     break;
		case G_PLR3: i = 3;
			     break;
		case G_PLR4: i = 4;
			     break;
	}

	mqd_t writequeue_fd;
	if((writequeue_fd =  mq_open(mq_name[i].c_str(), O_WRONLY|O_NONBLOCK)) == -1)
	{
		perror("mq open error at write mq");
		exit(1);
	}
	char message_text[121];
	memset(message_text , 0 ,121);
	strncpy(message_text, message.c_str(), 120);

	if(mq_send(writequeue_fd, message_text, strlen(message_text), 0) == -1)
	{
		perror("mq_send error");
		exit(1);
	}
	mq_close(writequeue_fd);
}


void broadcasttoMqueue(int player_num2)
{

	string temp,ss,message;
	if(wonf == false)
	{
		temp = goldMine->getMessage();
		ss = to_string(player_num2);
		message = "Player " + ss + " says: ";
		message.append(temp);
	}
	else if(wonf == true)
	{
		ss = to_string(player_num2);
		message = "Player " + ss + " has already won ";
	}
	mqd_t writequeue_fd;

	char message_text[121];

	for(int i = 0; i < 5; i++)
	{
		if(gb->pid[i] != 0 && gb->pid[i] != getpid())
		{
			if((writequeue_fd=mq_open(mq_name[i].c_str(), O_WRONLY|O_NONBLOCK))==-1)
			{
				perror("mq_open");
				exit(1);
			}
			memset(message_text, 0, 121);
			strncpy(message_text, message.c_str(), 120);
			if(mq_send(writequeue_fd, message_text, strlen(message_text), 0)==-1)
			{
				perror("mq_send");
				exit(1);
			}
			mq_close(writequeue_fd);
		}
	}
	return;
}

void cleanup(int sigNum)
{
	gb->map[position] &= ~active_player;
	gb->players &= ~active_player;

	for(int i = 0; i<5; i++)
	{
		if(gb->pid[i] != 0 && gb->pid[i] != getpid())
			kill(gb->pid[i], SIGUSR1);
	}
	if(goldMine)
		delete goldMine;
	mq_close(readqueue_fd);
	for(int i = 0; i<5; i++)
	{
		if(gb->pid[i] == getpid())
			mq_unlink(mq_name[i].c_str());
	}

}

void sendUpdates()
{
	for(int i = 0; i<5; i++)
	{
		if(gb->pid[i] != 0 && gb->pid[i] != getpid())
			kill(gb->pid[i], SIGUSR1);
	}
}


void daemonSIG1(int sigNum)
{
	int mapToSend = gb->cols * gb->rows;
	vector<pair<int, unsigned char> > newMap;
	for(int i = 0; i < mapToSend; i++)
	{
		if(gb->map[i] != mapPointer[i])
			newMap.push_back(make_pair(i, gb->map[i]));
	}
	int s_fd, vectSize;
	vectSize = newMap.size();
	if(runningSystem == server)
		s_fd = new_sockfd;
	if(runningSystem == client)
		s_fd = sockfd;

	if(vectSize > 0)
	{
		if(s_fd != 0)
		{
			char startSend = '0';
			WRITE(s_fd, &startSend, sizeof(startSend));
			WRITE(s_fd, &vectSize, sizeof(vectSize));
			for(int j = 0; j < vectSize; j++)
			{
				WRITE(s_fd, &newMap[j].first, sizeof(newMap[j].first));
				WRITE(s_fd, &newMap[j].second, sizeof(newMap[j].second));
			}
		}
	}
	for(int k = 0; k < mapToSend; k++)
	{
		mapPointer[k] = gb->map[k];
	}
}


void daemonSIG2(int sigNum)
{

}

void daemonHUP(int sigNum)
{
	unsigned char sendPlayer = gb->players | G_SOCKPLR;
	if(new_sockfd != 0 && runningSystem == server)
		WRITE(new_sockfd, &sendPlayer, sizeof(sendPlayer));
	else if(sockfd != 0 && runningSystem == client)
		WRITE(sockfd, &sendPlayer, sizeof(sendPlayer));
	if(gb->players != 0)
		kill(gb->daemonID, SIGUSR1);
	if(G_SOCKPLR == sendPlayer)
	{
		shm_unlink("/shm");
		sem_close(my_sem_ptr);
		sem_unlink("/PC");
		exit(1);
	}
}


void serverDaemon()
{
	if(fork() > 0)
		return;
	if(fork() > 0)
		exit(0);
	if(setsid() == -1)
		exit(1);
	for(int i = 0; i < 	sysconf(_SC_OPEN_MAX); ++i)
		close(i);
	open("/dev/null", O_RDWR);
	open("/dev/null", O_RDWR);
	open("/dev/null", O_RDWR);
	umask(0);
	chdir("/");
	gb->daemonID = getpid();
	int status; //for error checking


	//change this # between 2000-65k before using
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints)); //zero out everything in structure
	hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
	hints.ai_socktype=SOCK_STREAM; // TCP stream sockets
	hints.ai_flags=AI_PASSIVE; //file in the IP of the server for me

	struct addrinfo *servinfo;
	if((status=getaddrinfo(NULL, portno, &hints, &servinfo))==-1)
	{
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		exit(1);
	}
	sockfd=socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

	/*avoid "Address already in use" error*/
	int yes=1;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))==-1)
	{
		perror("setsockopt");
		exit(1);
	}

	//We need to "bind" the socket to the port number so that the kernel
	//can match an incoming packet on a port to the proper process
	if((status=bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen))==-1)
	{
		perror("bind");
		exit(1);
	}
	//when done, release dynamically allocated memory
	freeaddrinfo(servinfo);

	if(listen(sockfd,1)==-1)
	{
		perror("listen");
		exit(1);
	}
	struct sockaddr_in client_addr;
	socklen_t clientSize=sizeof(client_addr);
	if((new_sockfd=accept(sockfd, (struct sockaddr*) &client_addr, &clientSize))==-1)
	{
		perror("accept");
		exit(1);
	}
	runningSystem = server;
	struct sigaction action_jackson5;
	action_jackson5.sa_handler=daemonSIG1;
	sigemptyset(&action_jackson5.sa_mask);
	action_jackson5.sa_flags=0;
	action_jackson5.sa_restorer=NULL;
	sigaction(SIGUSR1, &action_jackson5, NULL);

	struct sigaction action_jackson6;
	action_jackson6.sa_handler=daemonSIG2;
	sigemptyset(&action_jackson6.sa_mask);
	action_jackson6.sa_flags=0;
	action_jackson6.sa_restorer=NULL;
	sigaction(SIGUSR2, &action_jackson6, NULL);

	struct sigaction action_jackson7;
	action_jackson7.sa_handler=daemonHUP;
	sigemptyset(&action_jackson7.sa_mask);
	action_jackson7.sa_flags=0;
	action_jackson7.sa_restorer=NULL;
	sigaction(SIGHUP, &action_jackson7, NULL);

	int areaSize = gb->rows * gb->cols;
	unsigned char myLocalMap[areaSize];
	for(int i = 0; i < areaSize; i++)
	{
		myLocalMap[i] = gb->map[i];
	}
	mapPointer = myLocalMap;
	WRITE(new_sockfd, &gb->rows, sizeof(gb->rows));
	WRITE(new_sockfd, &gb->cols, sizeof(gb->cols));
	WRITE(new_sockfd, myLocalMap, areaSize);
	unsigned char bit = gb->players;
	WRITE(new_sockfd, &bit, sizeof(bit));
	while(1)
	{
		unsigned char socketByte;
		READ(new_sockfd, &socketByte, sizeof(socketByte));
		if(socketByte == '0')
		{
			int bytesChanged, byteLocation;
			unsigned char mapByte;
			READ(new_sockfd, &bytesChanged, sizeof(bytesChanged));
			for(int l = 0; l < bytesChanged; l++)
			{
				READ(new_sockfd, &byteLocation, sizeof(byteLocation));
				READ(new_sockfd, &mapByte, sizeof(mapByte));
				gb->map[byteLocation] = mapByte;
				mapPointer[byteLocation] = mapByte;
			}
			sendUpdates();
		}
		else if((socketByte & G_SOCKPLR) == G_SOCKPLR)
		{
			unsigned char playerUpdates = socketByte &~ G_SOCKPLR;
			if((gb->pid[0] == 0) && (playerUpdates & G_PLR0) == G_PLR0)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR0;
				gb->pid[0] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[1] == 0) && (playerUpdates & G_PLR1) == G_PLR1)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR1;
				gb->pid[1] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[2] == 0) && (playerUpdates & G_PLR2) == G_PLR2)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR2;
				gb->pid[2] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[3] == 0) && (playerUpdates & G_PLR3) == G_PLR3)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR3;
				gb->pid[3] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[4] == 0) && (playerUpdates & G_PLR4) == G_PLR4)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR4;
				gb->pid[4] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[0] != 0) && !(playerUpdates & G_PLR0))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR0;
				gb->pid[0] = 0;
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[1] != 0) && !(playerUpdates & G_PLR1))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR1;
				gb->pid[1] = 0;
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[2] != 0) && !(playerUpdates & G_PLR2))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR2;
				gb->pid[2] = 0;
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[3] != 0) && !(playerUpdates & G_PLR3))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR3;
				gb->pid[3] = 0;
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[4] != 0) && !(playerUpdates & G_PLR4))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR4;
				gb->pid[4] = 0;
				sem_post(my_sem_ptr);
			}
			if(gb->players == 0)
			{
				shm_unlink("/shm");
				sem_close(my_sem_ptr);
				sem_unlink("/PC");
				exit(1);
			}
		}
	}
}


void clientDaemon()
{
	if(fork() > 0)
		return;
	if(fork() > 0)
		exit(0);
	if(setsid() == -1)
		exit(1);
	for(int i = 0; i < 	sysconf(_SC_OPEN_MAX); ++i)
		close(i);
	open("/dev/null", O_RDWR);
	open("/dev/null", O_RDWR);
	open("/dev/null", O_RDWR);
	umask(0);
	chdir("/");

	int status; //for error checking


	//change this # between 2000-65k before using
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints)); //zero out everything in structure
	hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
	hints.ai_socktype=SOCK_STREAM; // TCP stream sockets

	struct addrinfo *servinfo;
	//instead of "localhost", it could by any domain name
	if((status=getaddrinfo(addrs.c_str(), portno, &hints, &servinfo))==-1)
	{
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		exit(1);
	}
	sockfd=socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

	if((status=connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen))==-1)
	{
		perror("connect");
		exit(1);
	}
	//release the information allocated by getaddrinfo()
	freeaddrinfo(servinfo);
	runningSystem = client;
	struct sigaction action_jackson5;
	action_jackson5.sa_handler=daemonSIG1;
	sigemptyset(&action_jackson5.sa_mask);
	action_jackson5.sa_flags=0;
	action_jackson5.sa_restorer=NULL;
	sigaction(SIGUSR1, &action_jackson5, NULL);

	struct sigaction action_jackson6;
	action_jackson6.sa_handler=daemonSIG2;
	sigemptyset(&action_jackson6.sa_mask);
	action_jackson6.sa_flags=0;
	action_jackson6.sa_restorer=NULL;
	sigaction(SIGUSR2, &action_jackson6, NULL);

	struct sigaction action_jackson7;
	action_jackson7.sa_handler=daemonHUP;
	sigemptyset(&action_jackson7.sa_mask);
	action_jackson7.sa_flags=0;
	action_jackson7.sa_restorer=NULL;
	sigaction(SIGHUP, &action_jackson7, NULL);

	int serverRows, serverCols, serverArea, shared_mem_fd;
	READ(sockfd, &serverRows, sizeof(serverRows));
	READ(sockfd, &serverCols, sizeof(serverCols));
	serverArea = serverRows * serverCols;
	unsigned char myLocalMap[serverArea];
	READ(sockfd, myLocalMap, serverArea);
	mapPointer = myLocalMap;
	unsigned char serverPlayers;
	READ(sockfd, &serverPlayers, sizeof(serverPlayers));

	my_sem_ptr = sem_open("/PC",O_EXCL|O_CREAT,S_IRUSR|S_IWUSR,1);
	shared_mem_fd = shm_open("/shm",O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	ftruncate(shared_mem_fd,((serverRows*serverCols)+(sizeof(GameBoard))));
	gb = (GameBoard*)mmap(NULL,((serverRows*serverCols)+(sizeof(GameBoard))),
			PROT_WRITE|PROT_READ, MAP_SHARED,shared_mem_fd,0);
	gb->rows = serverRows;
	gb->cols = serverCols;
	gb->players = serverPlayers;
	gb->daemonID = getpid();
	for(int i = 0; i < serverArea; i++)
	{
		gb->map[i] = mapPointer[i];
	}
	if((serverPlayers & G_PLR0) == G_PLR0)
	{
		gb->pid[0] = getpid();
	}
	else if((serverPlayers & G_PLR1) == G_PLR1)
	{
		gb->pid[1] = getpid();
	}
	else if((serverPlayers & G_PLR2) == G_PLR2)
	{
		gb->pid[2] = getpid();
	}
	else if((serverPlayers & G_PLR3) == G_PLR3)
	{
		gb->pid[3] = getpid();
	}
	else if((serverPlayers & G_PLR4) == G_PLR4)
	{
		gb->pid[4] = getpid();
	}
	while(1)
	{
		unsigned char socketByte;
		READ(sockfd, &socketByte, sizeof(socketByte));
		if(socketByte == '0')
		{
			int bytesChanged, byteLocation;
			unsigned char mapByte;
			READ(sockfd, &bytesChanged, sizeof(bytesChanged));
			for(int l = 0; l < bytesChanged; l++)
			{
				READ(sockfd, &byteLocation, sizeof(byteLocation));
				READ(sockfd, &mapByte, sizeof(mapByte));
				sem_wait(my_sem_ptr);
				gb->map[byteLocation] = mapByte;
				mapPointer[byteLocation] = mapByte;
				sem_post(my_sem_ptr);
			}
			sendUpdates();
		}
		else if((socketByte & G_SOCKPLR) == G_SOCKPLR)
		{
			unsigned char playerUpdates = socketByte &~ G_SOCKPLR;
			if((gb->pid[0] == 0) && (playerUpdates & G_PLR0) == G_PLR0)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR0;
				gb->pid[0] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[1] == 0) && (playerUpdates & G_PLR1) == G_PLR1)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR1;
				gb->pid[1] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[2] == 0) && (playerUpdates & G_PLR2) == G_PLR2)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR2;
				gb->pid[2] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[3] == 0) && (playerUpdates & G_PLR3) == G_PLR3)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR3;
				gb->pid[3] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[4] == 0) && (playerUpdates & G_PLR4) == G_PLR4)
			{
				sem_wait(my_sem_ptr);
				gb->players |= G_PLR4;
				gb->pid[4] = getpid();
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[0] != 0) && !(playerUpdates & G_PLR0))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR0;
				gb->pid[0] = 0;
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[1] != 0) && !(playerUpdates & G_PLR1))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR1;
				gb->pid[1] = 0;
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[2] != 0) && !(playerUpdates & G_PLR2))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR2;
				gb->pid[2] = 0;
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[3] != 0) && !(playerUpdates & G_PLR3))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR3;
				gb->pid[3] = 0;
				sem_post(my_sem_ptr);
			}
			else if((gb->pid[4] != 0) && !(playerUpdates & G_PLR4))
			{
				sem_wait(my_sem_ptr);
				gb->players &= ~G_PLR4;
				gb->pid[4] = 0;
				sem_post(my_sem_ptr);
			}
			if(gb->players == 0)
			{
				shm_unlink("/shm");
				sem_close(my_sem_ptr);
				sem_unlink("/PC");
				exit(1);
			}
		}
	}
}


int main(int argc, char *argv[])
{
	fstream map_input;
	int mapsize = 0;
	int index = 0;
	map_input.open("mymap.txt");
	string map_char="";
	string no_gold, line;
	int rows=0, columns=0;
	int fool_gold = 0;
	int shared_mem_fd;
	srand(time(NULL));
	const char *input;
	int player_number = 0;

	struct sigaction action_jackson;
	action_jackson.sa_handler=handle_map;
	sigemptyset(&action_jackson.sa_mask);
	action_jackson.sa_flags=0;
	action_jackson.sa_restorer=NULL;

	sigaction(SIGUSR1, &action_jackson, NULL);


	struct sigaction action_jackson2;
	action_jackson2.sa_handler=readqueue;
	sigemptyset(&action_jackson2.sa_mask);
	action_jackson2.sa_flags=0;
	action_jackson2.sa_restorer=NULL;

	sigaction(SIGUSR2, &action_jackson2, NULL);

	struct mq_attr mq_attributes;
	mq_attributes.mq_flags=0;
	mq_attributes.mq_maxmsg=10;
	mq_attributes.mq_msgsize=120;

	struct sigaction action_jackson3;
	action_jackson3.sa_handler=cleanup;
	sigemptyset(&action_jackson3.sa_mask);
	action_jackson3.sa_flags=0;
	action_jackson3.sa_restorer=NULL;

	sigaction(SIGINT, &action_jackson3, NULL);
	sigaction(SIGTERM, &action_jackson3, NULL);
	sigaction(SIGHUP, &action_jackson3, NULL);

	if(argc > 1)
	{
		addrs = argv[1];
		clientDaemon();
	}
	else
	{
		my_sem_ptr = sem_open("/PC",O_EXCL|O_CREAT,S_IRUSR|S_IWUSR,1);


		if(my_sem_ptr != SEM_FAILED)
		{
			if(map_input.is_open())
			{
				getline(map_input,no_gold);
				while(getline(map_input,line))
				{
					map_char += line;
					rows++;
					if(columns == 0)
					{
						columns = line.length();
					}
				}
			}
			map_input.close();
			int num_of_gold = atoi(no_gold.c_str());
			mapsize = map_char.length();

			input = map_char.c_str();
			sem_wait(my_sem_ptr);

			shared_mem_fd = shm_open("/shm",O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
			ftruncate(shared_mem_fd,((rows*columns)+(sizeof(GameBoard))));
			gb = (GameBoard*)mmap(NULL,((rows*columns)+(sizeof(GameBoard))),
					PROT_WRITE|PROT_READ, MAP_SHARED,shared_mem_fd,0);
			gb->rows = rows;
			gb->cols = columns;
			active_player = G_PLR0;
			gb->players |= G_PLR0;

			for(int i = 0; i < 5; i++)
			{
				gb->pid[i] = 0;
			}
			gb->pid[0] = getpid();

			fool_gold = num_of_gold - 1;
			while(index < mapsize)
			{
				if(map_char[index] == ' ')
					gb->map[index] = 0;
				else if(map_char[index] == '*')
					gb->map[index] = G_WALL;
				index++;
			}


			position = rand() % mapsize;
			while(gb->map[position] != 0)
			{
				position = rand()%mapsize;
			}
			gb->map[position] |= G_GOLD;

			int i;
			for(i = 0; i < fool_gold; i++)
			{
				position = rand() % mapsize;
				while(gb->map[position] != 0)
				{
					position = rand()%mapsize;
				}
				gb->map[position] |= G_FOOL;
			}
			position = rand() % mapsize;
			while(gb->map[position] != 0)
			{
				position = rand()%mapsize;
			}
			gb->map[position] |= active_player;

			goldMine = new Map(gb->map,gb->rows,gb->cols);
			int real_gold, fools_gold, winner_on_edge;
			real_gold =0;
			fools_gold = 0;
			winner_on_edge =0;
			goldMine->postNotice("Welcome to the GoldChase!!");
			sem_post(my_sem_ptr);

			if((readqueue_fd=mq_open(mq_name[0].c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
							S_IRUSR|S_IWUSR, &mq_attributes))==-1)
			{
				perror("mq_open");
				exit(1);
			}
			//set up message queue to receive signal whenever message comes in
			struct sigevent mq_notification_event;
			mq_notification_event.sigev_notify=SIGEV_SIGNAL;
			mq_notification_event.sigev_signo=SIGUSR2;
			mq_notify(readqueue_fd, &mq_notification_event);


			if(gb->daemonID == 0)
				serverDaemon();
			int key = 0;
			key = goldMine->getKey();
			while(key!=81)
			{
				if(key==104)
				{
					if(((position%gb->cols) > 0))
					{
						if(!((gb->map[position-1]) & G_WALL))
						{
							sem_wait(my_sem_ptr);
							gb->map[position] &= ~active_player;
							position--;
							if(((gb->map[position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[position]) == G_GOLD)
								real_gold = 1;
							gb->map[position] |= active_player;
							sem_post(my_sem_ptr);
						}
					}
					else if(winner_on_edge)
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~active_player;
						goldMine->drawMap();
						sendUpdates();
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGUSR1);
						wonf = true;
						broadcasttoMqueue(player_number+1);
						goldMine->postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						sem_post(my_sem_ptr);
						break;
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGHUP);
					}
					goldMine->drawMap();
					sendUpdates();
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGUSR1);
				}
				else if(key==106)
				{
					if(((position+gb->cols) < (rows*columns)))
					{
						if(!((gb->map[position+gb->cols]) & G_WALL))
						{
							sem_wait(my_sem_ptr);
							gb->map[position] &= ~active_player;
							position= position + gb->cols;
							if(((gb->map[position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[position]) == G_GOLD)
								real_gold = 1;
							gb->map[position] |= active_player;
							sem_post(my_sem_ptr);
						}
					}
					else if(winner_on_edge)
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~active_player;
						goldMine->drawMap();
						sendUpdates();
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGUSR1);
						wonf = true;
						broadcasttoMqueue(player_number+1);
						goldMine->postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						sem_post(my_sem_ptr);
						break;
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGHUP);
					}
					goldMine->drawMap();
					sendUpdates();
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGUSR1);
				}
				else if(key == 107)
				{
					if(((position-gb->cols) > 0))
					{
						if(!((gb->map[position-gb->cols]) & G_WALL))
						{
							sem_wait(my_sem_ptr);
							gb->map[position] &= ~active_player;
							position = position - gb->cols;
							if(((gb->map[position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[position]) == G_GOLD)
								real_gold = 1;
							gb->map[position] |= active_player;
							sem_post(my_sem_ptr);
						}
					}
					else if(winner_on_edge)
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~active_player;
						goldMine->drawMap();
						sendUpdates();
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGUSR1);
						wonf = true;
						broadcasttoMqueue(player_number+1);
						goldMine->postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						sem_post(my_sem_ptr);
						break;
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGHUP);
					}
					goldMine->drawMap();
					sendUpdates();
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGUSR1);
				}
				else if(key==108)
				{
					if(((position+1)%gb->cols) !=0)
					{
						if(!((gb->map[position+1]) & G_WALL))
						{
							sem_wait(my_sem_ptr);
							gb->map[position] &= ~active_player;
							position++;
							if(((gb->map[position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[position]) == G_GOLD)
								real_gold = 1;
							gb->map[position] |= active_player;
							sem_post(my_sem_ptr);
						}
					}
					else if(winner_on_edge && (((position+1)%gb->cols)==0))
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~active_player;
						goldMine->drawMap();
						sendUpdates();
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGUSR1);
						wonf = true;
						broadcasttoMqueue(player_number+1);
						goldMine->postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						sem_post(my_sem_ptr);
						break;
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGHUP);
					}
					goldMine->drawMap();
					sendUpdates();
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGUSR1);
				}
				else if(key == 109) //m
				{
					writetoMqueue(active_player, player_number + 1);
				}
				else if(key == 98) //b
				{
					broadcasttoMqueue(player_number+1);
				}
				if(fools_gold == 1)
				{
					sem_wait(my_sem_ptr);
					gb->map[position] &= ~G_FOOL;
					sem_post(my_sem_ptr);
					goldMine->postNotice("Sorry. Fool's Gold.");
					fools_gold = 0;
				}
				if(real_gold == 1)
				{
					sem_wait(my_sem_ptr);
					gb->map[position] &= ~G_GOLD;
					sem_post(my_sem_ptr);
					real_gold = 0;
					winner_on_edge = 1;
					goldMine->postNotice("You found the gold! Time to make your escape!!");
				}
				key = goldMine->getKey();
			}
			sem_wait(my_sem_ptr);
			gb->players &= ~active_player;
			gb->map[position] &= ~active_player;
			goldMine->drawMap();
			sem_post(my_sem_ptr);
			sendUpdates();
			if(gb->daemonID != 0)
				kill(gb->daemonID, SIGHUP);
			mq_close(readqueue_fd);
			mq_unlink(mq_name[0].c_str());
			delete goldMine;
		}

		else
		{
			if(errno == EEXIST)
			{
				sleep(3);
				string mq="";
				my_sem_ptr = sem_open("/PC",O_RDWR);
				sem_wait(my_sem_ptr);
				shared_mem_fd = shm_open("/shm",O_RDWR, S_IRUSR | S_IWUSR);
				int sub_rows;
				int sub_cols;
				read(shared_mem_fd,&sub_rows,sizeof(int));
				read(shared_mem_fd,&sub_cols,sizeof(int));
				gb = (GameBoard*)mmap(NULL,((sub_cols*sub_rows)+(sizeof(GameBoard))), PROT_WRITE|PROT_READ,
						MAP_SHARED,shared_mem_fd,0);
				if(!(gb->players & G_PLR0))
				{
					gb->pid[0] = getpid();
					active_player = G_PLR0;
					mq = mq_name[0];
					player_number = 1;
				}
				else if(!(gb->players & G_PLR1))
				{
					gb->pid[1] = getpid();
					active_player = G_PLR1;
					mq = mq_name[1];
					player_number = 2;

				}
				else if(!(gb->players & G_PLR2))
				{
					gb->pid[2] = getpid();
					active_player = G_PLR2;
					mq = mq_name[2];
					player_number = 3;
				}
				else if(!(gb->players & G_PLR3))
				{
					gb->pid[3] = getpid();
					active_player = G_PLR3;
					mq = mq_name[3];
					player_number = 4;
				}
				else if(!(gb->players & G_PLR4))
				{
					gb->pid[4] = getpid();
					active_player = G_PLR4;
					mq = mq_name[4];
					player_number = 5;
				}
				else
				{
					cout << "Maximum Players reached..Game not available" << endl;
					sem_post(my_sem_ptr);
					return 0;
				}
				gb->players |= active_player;
				int player_num = 0;
				int sub_mapsize = (sub_cols*sub_rows);
				position = rand() % sub_mapsize;
				while(gb->map[position] != 0)
				{
					position = rand()% sub_mapsize;
				}
				gb->map[position] |= active_player;
				goldMine = new Map(gb->map,sub_rows,sub_cols);
				goldMine->postNotice("Welcome to the GoldChase!!");
				sem_post(my_sem_ptr);
				sendUpdates();
				if(gb->daemonID != 0)
					kill(gb->daemonID, SIGHUP);
				if((readqueue_fd=mq_open(mq.c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
								S_IRUSR|S_IWUSR, &mq_attributes))==-1)
				{
					perror("mq_open");
					exit(1);
				}
				//set up message queue to receive signal whenever message comes in
				struct sigevent mq_notification_event;
				mq_notification_event.sigev_notify=SIGEV_SIGNAL;
				mq_notification_event.sigev_signo=SIGUSR2;
				mq_notify(readqueue_fd, &mq_notification_event);
				int real_gold, fools_gold, winner_on_edge;
				real_gold =0;
				fools_gold = 0;
				winner_on_edge =0;
				int key = 0;
				key = goldMine->getKey();
				while(key!=81)
				{
					if(key==104)
					{
						if(((position%gb->cols) > 0))
						{
							if(!((gb->map[position-1]) & G_WALL))
							{
								sem_wait(my_sem_ptr);
								gb->map[position] &= ~active_player;
								position--;
								if(((gb->map[position]) & G_FOOL) == G_FOOL)
									fools_gold = 1;
								else if((gb->map[position]) == G_GOLD)
									real_gold = 1;
								gb->map[position] |= active_player;
								sem_post(my_sem_ptr);
							}
						}
						else if(winner_on_edge)
						{
							sem_wait(my_sem_ptr);
							gb->map[position] &= ~active_player;
							goldMine->drawMap();
							sendUpdates();
							if(gb->daemonID != 0)
								kill(gb->daemonID, SIGUSR1);
							wonf = true;
							broadcasttoMqueue(player_number);
							goldMine->postNotice("You escaped with the gold and WON!");
							gb->players &= ~active_player;
							sem_post(my_sem_ptr);
							break;
							if(gb->daemonID != 0)
								kill(gb->daemonID, SIGHUP);
						}
						goldMine->drawMap();
						sendUpdates();
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGUSR1);
					}
					else if(key==106)
					{
						if(((position+gb->cols) < sub_mapsize))
						{
							if(!((gb->map[position+gb->cols]) & G_WALL))
							{
								sem_wait(my_sem_ptr);
								gb->map[position] &= ~active_player;
								position+=gb->cols;
								if(((gb->map[position]) & G_FOOL) == G_FOOL)
									fools_gold = 1;
								else if((gb->map[position]) == G_GOLD)
									real_gold = 1;
								gb->map[position] |= active_player;
								sem_post(my_sem_ptr);
							}
						}
						else if(winner_on_edge)
						{
							sem_wait(my_sem_ptr);
							gb->map[position] &= ~active_player;
							goldMine->drawMap();
							sendUpdates();
							if(gb->daemonID != 0)
								kill(gb->daemonID, SIGUSR1);
							wonf = true;
							broadcasttoMqueue(player_number);
							goldMine->postNotice("You escaped with the gold and WON!");
							gb->players &= ~active_player;
							sem_post(my_sem_ptr);
							break;
							if(gb->daemonID != 0)
								kill(gb->daemonID, SIGHUP);
						}
						goldMine->drawMap();
						sendUpdates();
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGUSR1);
					}
					else if(key == 107)
					{
						if(((position-gb->cols) > 0))
						{
							if(!((gb->map[position-gb->cols]) & G_WALL))
							{
								sem_wait(my_sem_ptr);
								gb->map[position] &= ~active_player;
								position-=gb->cols;
								if(((gb->map[position]) & G_FOOL) == G_FOOL)
									fools_gold = 1;
								else if((gb->map[position]) == G_GOLD)
									real_gold = 1;
								gb->map[position] |= active_player;
								sem_post(my_sem_ptr);
							}
						}
						else if(winner_on_edge)
						{
							sem_wait(my_sem_ptr);
							gb->map[position] &= ~active_player;
							goldMine->drawMap();
							sendUpdates();
							if(gb->daemonID != 0)
								kill(gb->daemonID, SIGUSR1);
							wonf = true;
							goldMine->postNotice("You escaped with the gold and WON!");
							broadcasttoMqueue(player_number);
							gb->players &= ~active_player;
							sem_post(my_sem_ptr);
							break;
							if(gb->daemonID != 0)
								kill(gb->daemonID, SIGHUP);
						}
						goldMine->drawMap();
						sendUpdates();
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGUSR1);
					}
					else if(key==108)
					{
						if(((position+1)%gb->cols) !=0)
						{
							if(!((gb->map[position+1]) & G_WALL))
							{
								sem_wait(my_sem_ptr);
								gb->map[position] &= ~active_player;
								position++;
								if(((gb->map[position]) & G_FOOL) == G_FOOL)
									fools_gold = 1;
								else if((gb->map[position]) == G_GOLD)
									real_gold = 1;
								gb->map[position] |= active_player;
								sem_post(my_sem_ptr);
							}
						}
						else if(winner_on_edge && (((position+1)%gb->cols)==0))
						{
							sem_wait(my_sem_ptr);
							gb->map[position] &= ~active_player;
							goldMine->drawMap();
							sendUpdates();
							if(gb->daemonID != 0)
								kill(gb->daemonID, SIGUSR1);
							wonf = true;
							broadcasttoMqueue(player_number);
							goldMine->postNotice("You escaped with the gold and WON!");
							gb->players &= ~active_player;
							sem_post(my_sem_ptr);
							break;
							if(gb->daemonID != 0)
								kill(gb->daemonID, SIGHUP);
						}
						goldMine->drawMap();
						sendUpdates();
						if(gb->daemonID != 0)
							kill(gb->daemonID, SIGUSR1);
					}
					else if(key == 109) //m
					{
						writetoMqueue(active_player, player_number);
					}
					else if(key == 98) //b
					{
						broadcasttoMqueue(player_number);

					}
					if(fools_gold == 1)
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~G_FOOL;
						sem_post(my_sem_ptr);
						goldMine->postNotice("Sorry. Fool's Gold.");
						fools_gold = 0;
					}
					if(real_gold == 1)
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~G_GOLD;
						sem_post(my_sem_ptr);
						real_gold = 0;
						winner_on_edge = 1;
						goldMine->postNotice("You found the gold! Time to make your escape!!");
					}
					key = goldMine->getKey();
				}
				sem_wait(my_sem_ptr);
				gb->players &= ~active_player;
				gb->map[position] &= ~active_player;
				sem_post(my_sem_ptr);
				goldMine->drawMap();
				sendUpdates();
				if(gb->daemonID != 0)
					kill(gb->daemonID, SIGHUP);
				mq_close(readqueue_fd);
				for(int i = 0; i<5; i++)
				{
					if(gb->pid[i] == getpid())
						mq_unlink(mq_name[i].c_str());
				}
				delete goldMine;
			}
			else
			{
				perror("Error : ");
			}
		}
	}
	if(argc > 1)
	{
		string mq="";
		my_sem_ptr = sem_open("/PC",O_RDWR);
		while(!my_sem_ptr && my_sem_ptr == SEM_FAILED)
		{
			my_sem_ptr = sem_open("/PC",O_RDWR);
		}
		sem_wait(my_sem_ptr);
		shared_mem_fd = shm_open("/shm",O_RDWR, S_IRUSR | S_IWUSR);
		int sub_rows;
		int sub_cols;
		read(shared_mem_fd,&sub_rows,sizeof(int));
		read(shared_mem_fd,&sub_cols,sizeof(int));
		gb = (GameBoard*)mmap(NULL,((sub_cols*sub_rows)+(sizeof(GameBoard))), PROT_WRITE|PROT_READ,
				MAP_SHARED,shared_mem_fd,0);
		if(!(gb->players & G_PLR0))
		{
			gb->pid[0] = getpid();
			active_player = G_PLR0;
			mq = mq_name[0];
			player_number = 1;
		}
		else if(!(gb->players & G_PLR1))
		{
			gb->pid[1] = getpid();
			active_player = G_PLR1;
			mq = mq_name[1];
			player_number = 2;

		}
		else if(!(gb->players & G_PLR2))
		{
			gb->pid[2] = getpid();
			active_player = G_PLR2;
			mq = mq_name[2];
			player_number = 3;
		}
		else if(!(gb->players & G_PLR3))
		{
			gb->pid[3] = getpid();
			active_player = G_PLR3;
			mq = mq_name[3];
			player_number = 4;
		}
		else if(!(gb->players & G_PLR4))
		{
			gb->pid[4] = getpid();
			active_player = G_PLR4;
			mq = mq_name[4];
			player_number = 5;
		}
		else
		{
			cout << "Maximum Players reached..Game not available" << endl;
			sem_post(my_sem_ptr);
			return 0;
		}
		gb->players |= active_player;
		int player_num = 0;
		int sub_mapsize = (sub_cols*sub_rows);
		position = rand() % sub_mapsize;
		while(gb->map[position] != 0)
		{
			position = rand()% sub_mapsize;
		}
		gb->map[position] |= active_player;
		goldMine = new Map(gb->map,sub_rows,sub_cols);
		goldMine->postNotice("Welcome to the GoldChase!!");
		sem_post(my_sem_ptr);
		sendUpdates();
		if(gb->daemonID != 0)
			kill(gb->daemonID, SIGHUP);

		if((readqueue_fd=mq_open(mq.c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
						S_IRUSR|S_IWUSR, &mq_attributes))==-1)
		{
			perror("mq_open");
			exit(1);
		}
		//set up message queue to receive signal whenever message comes in
		struct sigevent mq_notification_event;
		mq_notification_event.sigev_notify=SIGEV_SIGNAL;
		mq_notification_event.sigev_signo=SIGUSR2;
		mq_notify(readqueue_fd, &mq_notification_event);
		int real_gold, fools_gold, winner_on_edge;
		real_gold =0;
		fools_gold = 0;
		winner_on_edge =0;
		int key = 0;
		key = goldMine->getKey();
		while(key!=81)
		{
			if(key==104)
			{
				if(((position%gb->cols) > 0))
				{
					if(!((gb->map[position-1]) & G_WALL))
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~active_player;
						position--;
						if(((gb->map[position]) & G_FOOL) == G_FOOL)
							fools_gold = 1;
						else if((gb->map[position]) == G_GOLD)
							real_gold = 1;
						gb->map[position] |= active_player;
						sem_post(my_sem_ptr);
					}
				}
				else if(winner_on_edge)
				{
					sem_wait(my_sem_ptr);
					gb->map[position] &= ~active_player;
					goldMine->drawMap();
					sendUpdates();
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGUSR1);
					wonf = true;
					broadcasttoMqueue(player_number);
					goldMine->postNotice("You escaped with the gold and WON!");
					gb->players &= ~active_player;
					sem_post(my_sem_ptr);
					break;
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGHUP);
				}
				goldMine->drawMap();
				sendUpdates();
				if(gb->daemonID != 0)
					kill(gb->daemonID, SIGUSR1);
			}
			else if(key==106)
			{
				if(((position+gb->cols) < sub_mapsize))
				{
					if(!((gb->map[position+gb->cols]) & G_WALL))
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~active_player;
						position+=gb->cols;
						if(((gb->map[position]) & G_FOOL) == G_FOOL)
							fools_gold = 1;
						else if((gb->map[position]) == G_GOLD)
							real_gold = 1;
						gb->map[position] |= active_player;
						sem_post(my_sem_ptr);
					}
				}
				else if(winner_on_edge)
				{
					sem_wait(my_sem_ptr);
					gb->map[position] &= ~active_player;
					goldMine->drawMap();
					sendUpdates();
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGUSR1);
					wonf = true;
					broadcasttoMqueue(player_number);
					goldMine->postNotice("You escaped with the gold and WON!");
					gb->players &= ~active_player;
					sem_post(my_sem_ptr);
					break;
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGHUP);
				}
				goldMine->drawMap();
				sendUpdates();
				if(gb->daemonID != 0)
					kill(gb->daemonID, SIGUSR1);
			}
			else if(key == 107)
			{
				if(((position-gb->cols) > 0))
				{
					if(!((gb->map[position-gb->cols]) & G_WALL))
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~active_player;
						position-=gb->cols;
						if(((gb->map[position]) & G_FOOL) == G_FOOL)
							fools_gold = 1;
						else if((gb->map[position]) == G_GOLD)
							real_gold = 1;
						gb->map[position] |= active_player;
						sem_post(my_sem_ptr);
					}
				}
				else if(winner_on_edge)
				{
					sem_wait(my_sem_ptr);
					gb->map[position] &= ~active_player;
					goldMine->drawMap();
					sendUpdates();
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGUSR1);
					wonf = true;
					goldMine->postNotice("You escaped with the gold and WON!");
					broadcasttoMqueue(player_number);
					gb->players &= ~active_player;
					sem_post(my_sem_ptr);
					break;
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGHUP);
				}
				goldMine->drawMap();
				sendUpdates();
				if(gb->daemonID != 0)
					kill(gb->daemonID, SIGUSR1);
			}
			else if(key==108)
			{
				if(((position+1)%gb->cols) !=0)
				{
					if(!((gb->map[position+1]) & G_WALL))
					{
						sem_wait(my_sem_ptr);
						gb->map[position] &= ~active_player;
						position++;
						if(((gb->map[position]) & G_FOOL) == G_FOOL)
							fools_gold = 1;
						else if((gb->map[position]) == G_GOLD)
							real_gold = 1;
						gb->map[position] |= active_player;
						sem_post(my_sem_ptr);
					}
				}
				else if(winner_on_edge && (((position+1)%gb->cols)==0))
				{
					sem_wait(my_sem_ptr);
					gb->map[position] &= ~active_player;
					goldMine->drawMap();
					sendUpdates();
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGUSR1);
					wonf = true;
					broadcasttoMqueue(player_number);
					goldMine->postNotice("You escaped with the gold and WON!");
					gb->players &= ~active_player;
					sem_post(my_sem_ptr);
					break;
					if(gb->daemonID != 0)
						kill(gb->daemonID, SIGHUP);
				}
				goldMine->drawMap();
				sendUpdates();
				if(gb->daemonID != 0)
					kill(gb->daemonID, SIGUSR1);
			}
			else if(key == 109) //m
			{
				writetoMqueue(active_player, player_number);
			}
			else if(key == 98) //b
			{
				broadcasttoMqueue(player_number);

			}
			if(fools_gold == 1)
			{
				sem_wait(my_sem_ptr);
				gb->map[position] &= ~G_FOOL;
				sem_post(my_sem_ptr);
				goldMine->postNotice("Sorry. Fool's Gold.");
				fools_gold = 0;
			}
			if(real_gold == 1)
			{
				sem_wait(my_sem_ptr);
				gb->map[position] &= ~G_GOLD;
				sem_post(my_sem_ptr);
				real_gold = 0;
				winner_on_edge = 1;
				goldMine->postNotice("You found the gold! Time to make your escape!!");
			}
			key = goldMine->getKey();
		}
		sem_wait(my_sem_ptr);
		gb->players &= ~active_player;
		gb->map[position] &= ~active_player;
		sem_post(my_sem_ptr);
		goldMine->drawMap();
		sendUpdates();
		if(gb->daemonID != 0)
			kill(gb->daemonID, SIGHUP);
		mq_close(readqueue_fd);
		for(int i = 0; i<5; i++)
		{
			if(gb->pid[i] == getpid())
				mq_unlink(mq_name[i].c_str());
		}
		delete goldMine;
	}
	return 0;
}
