//    GoldChase - Part2
//    Priyanka Chordia.
//    Using Signals.


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

using namespace std;

struct GameBoard {
	int rows; //4 bytes
	int cols; //4 bytes
	unsigned char players;
	pid_t pid[5];
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

	mq_close(readqueue_fd);
	mq_unlink(mq_name[player_number].c_str());

	if(gb->players == 0)
	{
		shm_unlink("/shm");
		sem_close(my_sem_ptr);
		sem_unlink("/PC");
	}
	exit(1);
}

int main()
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
		ftruncate(shared_mem_fd,(mapsize+(sizeof(GameBoard)))); 
		gb = (GameBoard*)mmap(NULL,(mapsize+(sizeof(GameBoard))), 
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

		sem_post(my_sem_ptr);
		Map goldMap(gb->map,gb->rows,gb->cols);
		goldMine = &goldMap;
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


		int real_gold, fools_gold, winner_on_edge;
		real_gold =0;
		fools_gold = 0;
		winner_on_edge =0;
		goldMap.postNotice("Welcome to the GoldChase!!");	
		int key = 0;
		key = goldMap.getKey();
		while(key!=81)
		{
			sem_wait(my_sem_ptr);
			if(key==104)
			{ 
				if(((position%gb->cols) != 0)) 
				{  
					if(!((gb->map[position-1]) & G_WALL))
					{
						gb->map[position] &= ~active_player;
						position--;
						if(((gb->map[position]) & G_FOOL) == G_FOOL)
							fools_gold = 1;
						else if((gb->map[position]) == G_GOLD)
							real_gold = 1;
						gb->map[position] |= active_player;
					}
				}
				else if(winner_on_edge)
				{
					gb->map[position] &= ~active_player;
					goldMap.drawMap();
					wonf = true;
					broadcasttoMqueue(player_number+1);
					goldMap.postNotice("You escaped with the gold and WON!");
					gb->players &= ~active_player;
					break;
				}
			}
			else if(key==106)
			{
				if(((position+gb->cols) < mapsize))
				{  
					if(!((gb->map[position+gb->cols]) & G_WALL))
					{
						gb->map[position] &= ~active_player;
						position+=gb->cols;
						if(((gb->map[position]) & G_FOOL) == G_FOOL)
							fools_gold = 1;
						else if((gb->map[position]) == G_GOLD)
							real_gold = 1;
						gb->map[position] |= active_player;
					}
				}
				else if(winner_on_edge)
				{
					gb->map[position] &= ~active_player;
					goldMap.drawMap();
					wonf = true;
					broadcasttoMqueue(player_number+1);
					goldMap.postNotice("You escaped with the gold and WON!");
					gb->players &= ~active_player;
					break;
				}
			}
			else if(key == 107)
			{
				if(((position-gb->cols) > 0))
				{  
					if(!((gb->map[position-gb->cols]) & G_WALL))
					{
						gb->map[position] &= ~active_player;
						position-=gb->cols;
						if(((gb->map[position]) & G_FOOL) == G_FOOL)
							fools_gold = 1;
						else if((gb->map[position]) == G_GOLD)
							real_gold = 1;
						gb->map[position] |= active_player;
					}
				}
				else if(winner_on_edge)
				{
					gb->map[position] &= ~active_player;
					goldMap.drawMap();
					wonf = true;
					broadcasttoMqueue(player_number+1);
					goldMap.postNotice("You escaped with the gold and WON!");
					gb->players &= ~active_player;
					break;
				}
			}
			else if(key==108)
			{
				if(((position+1)%gb->cols) !=0)
				{  
					if(!((gb->map[position+1]) & G_WALL))  
					{
						gb->map[position] &= ~active_player;
						position++;
						if(((gb->map[position]) & G_FOOL) == G_FOOL)
							fools_gold = 1;
						else if((gb->map[position]) == G_GOLD)
							real_gold = 1;
						gb->map[position] |= active_player;
					}
				}
				else if(winner_on_edge && (((position+1)%gb->cols)==0))
				{
					gb->map[position] &= ~active_player;
					goldMap.drawMap();
					wonf = true;
					broadcasttoMqueue(player_number+1);
					goldMap.postNotice("You escaped with the gold and WON!");
					gb->players &= ~active_player;
					break;
				}            
			} 
			else if(key == 109) //m
			{
				writetoMqueue(active_player, player_number + 1);	
			}
			else if(key == 98) //b
			{
				broadcasttoMqueue(player_number+1);
			}
			goldMap.drawMap();
			for(int i = 0; i<5; i++)
			{
				if(gb->pid[i] != 0 && gb->pid[i] != getpid())
					kill(gb->pid[i], SIGUSR1);
			}
			if(fools_gold == 1)
			{
				gb->map[position] &= ~G_FOOL;
				goldMap.postNotice("Sorry. Fool's Gold.");
				fools_gold = 0;
			}
			if(real_gold == 1)
			{
				gb->map[position] &= ~G_GOLD;
				real_gold = 0;
				winner_on_edge = 1;
				goldMap.postNotice("You found the gold! Time to make your escape!!");
			}
			sem_post(my_sem_ptr);
			key = goldMap.getKey();
		}
		gb->players &= ~active_player;
		gb->map[position] &= ~active_player;
		goldMap.drawMap();
		mq_close(readqueue_fd);
		mq_unlink(mq_name[0].c_str());
	}

	else
	{  
		if(errno == EEXIST)
		{
			string mq="";
			my_sem_ptr = sem_open("/PC",O_RDWR,S_IRUSR|S_IWUSR,1);
			shared_mem_fd = shm_open("/shm",O_RDWR, S_IRUSR | S_IWUSR); 
			int sub_rows;
			int sub_cols;
			read(shared_mem_fd,&sub_rows,sizeof(int));
			read(shared_mem_fd,&sub_cols,sizeof(int));
			gb = (GameBoard*)mmap(NULL,sub_cols*sub_rows, PROT_WRITE|PROT_READ,
					MAP_SHARED,shared_mem_fd,0);
			sem_wait(my_sem_ptr);
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
			sem_post(my_sem_ptr);
			gb->players |= active_player;
			int player_num = 0;
			int sub_mapsize = (sub_cols*sub_rows);  
			position = rand() % sub_mapsize;
			while(gb->map[position] != 0)
			{
				position = rand()% sub_mapsize;
			}
			gb->map[position] |= active_player;
			Map goldMap(gb->map,sub_rows,sub_cols);
			goldMine = &goldMap;
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


			for(int i = 0; i<5; i++)
			{
				if(gb->pid[i] != 0 && gb->pid[i] != getpid())
					kill(gb->pid[i], SIGUSR1);
			}
			goldMap.postNotice("Welcome to the GoldChase!!");
			int real_gold, fools_gold, winner_on_edge;
			real_gold =0;
			fools_gold = 0;
			winner_on_edge =0;
			int key = 0;
			key = goldMap.getKey();
			while(key!=81)
			{
				sem_wait(my_sem_ptr);
				if(key==104)
				{ 
					if(((position%gb->cols) != 0)) 
					{  
						if(!((gb->map[position-1]) & G_WALL))
						{
							gb->map[position] &= ~active_player;
							position--;
							if(((gb->map[position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[position]) == G_GOLD)
								real_gold = 1;
							gb->map[position] |= active_player;
						}
					}
					else if(winner_on_edge)
					{
						gb->map[position] &= ~active_player;
						goldMap.drawMap();
						wonf = true;
						broadcasttoMqueue(player_number);
						goldMap.postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						break;
					}
				}
				else if(key==106)
				{
					if(((position+gb->cols) < sub_mapsize))
					{  
						if(!((gb->map[position+gb->cols]) & G_WALL))
						{
							gb->map[position] &= ~active_player;
							position+=gb->cols;
							if(((gb->map[position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[position]) == G_GOLD)
								real_gold = 1;
							gb->map[position] |= active_player;
						}
					}
					else if(winner_on_edge)
					{
						gb->map[position] &= ~active_player;
						goldMap.drawMap();
						wonf = true;
						broadcasttoMqueue(player_number);
						goldMap.postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						break;
					}
				}
				else if(key == 107)
				{
					if(((position-gb->cols) > 0))
					{  
						if(!((gb->map[position-gb->cols]) & G_WALL))
						{
							gb->map[position] &= ~active_player;
							position-=gb->cols;
							if(((gb->map[position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[position]) == G_GOLD)
								real_gold = 1;
							gb->map[position] |= active_player;
						}
					}
					else if(winner_on_edge)
					{
						gb->map[position] &= ~active_player;
						goldMap.drawMap();
						wonf = true;
						goldMap.postNotice("You escaped with the gold and WON!");
						broadcasttoMqueue(player_number);
						gb->players &= ~active_player;
						break;
					}
				}
				else if(key==108)
				{
					if(((position+1)%gb->cols) !=0)
					{  
						if(!((gb->map[position+1]) & G_WALL))  
						{
							gb->map[position] &= ~active_player;
							position++;
							if(((gb->map[position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[position]) == G_GOLD)
								real_gold = 1;
							gb->map[position] |= active_player;
						}
					}
					else if(winner_on_edge && (((position+1)%gb->cols)==0))
					{
						gb->map[position] &= ~active_player;
						goldMap.drawMap();
						wonf = true;
						broadcasttoMqueue(player_number);
						goldMap.postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						break;
					}            
				} 
				else if(key == 109) //m
				{
					writetoMqueue(active_player, player_number);	
				}
				else if(key == 98) //b
				{
					broadcasttoMqueue(player_number);

				}
				goldMap.drawMap();
				for(int i = 0; i<5; i++)
				{
					if(gb->pid[i] != 0 && gb->pid[i] != getpid())
						kill(gb->pid[i], SIGUSR1);
				}
				if(fools_gold == 1)
				{
					gb->map[position] &= ~G_FOOL;
					goldMap.postNotice("Sorry. Fool's Gold.");
					fools_gold = 0;
				}
				if(real_gold == 1)
				{
					gb->map[position] &= ~G_GOLD;
					real_gold = 0;
					winner_on_edge = 1;
					goldMap.postNotice("You found the gold! Time to make your escape!!");
				}
				sem_post(my_sem_ptr);
				key = goldMap.getKey();
			}
			sem_wait(my_sem_ptr);
			gb->players &= ~active_player;
			gb->map[position] &= ~active_player;
			sem_post(my_sem_ptr);
			goldMap.drawMap();
			mq_close(readqueue_fd);
			mq_unlink(mq_name[player_number].c_str());
		}
		else
		{
			perror("Error : ");
		}
	}

	if(gb->players == 0)
	{
		shm_unlink("/shm");
		sem_close(my_sem_ptr);
		sem_unlink("/PC");
	}
	return 0;
}
