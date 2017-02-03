//    GoldChase - Part1
//    Priyanka Chordia.


#include "goldchase.h"
#include "Map.h"
#include <iostream>
#include <fstream>
#include <semaphore.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

struct GameBoard {
	int rows; //4 bytes
	int cols; //4 bytes
	unsigned char players;
	char map[0];
};


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
	GameBoard *gb;
	sem_t *my_sem_ptr;
	char active_player;
	const char *input;  

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

		fool_gold = num_of_gold - 1;
		while(index < mapsize)
		{
			if(map_char[index] == ' ')      
				gb->map[index] = 0;
			else if(map_char[index] == '*') 
				gb->map[index] = G_WALL;
			index++;
		}

		int position = 0;  
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
		int real_gold, fools_gold, winner_on_edge;
		real_gold =0;
		fools_gold = 0;
		winner_on_edge =0;
		goldMap.postNotice("Welcome to the GoldChase!!");	
		int key = 0;
		key = goldMap.getKey();
		while(key!=81)
		{
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
					goldMap.postNotice("You escaped with the gold and WON!");
					gb->players &= ~active_player;
					break;
				}            
			} 
			goldMap.drawMap();
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
			key = goldMap.getKey();
		}
		gb->players &= ~active_player;
		gb->map[position] &= ~active_player;
		goldMap.drawMap();
	}

	else
	{  
		if(errno == EEXIST)
		{
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
				active_player = G_PLR0;
			}
			else if(!(gb->players & G_PLR1))
			{
				active_player = G_PLR1;
			}
			else if(!(gb->players & G_PLR2))
			{
				active_player = G_PLR2;
			}
			else if(!(gb->players & G_PLR3))
			{
				active_player = G_PLR3;
			}
			else if(!(gb->players & G_PLR4))
			{
				active_player = G_PLR4;
			}
			else
			{
				cout << "Maximum Players reached..Game not available" << endl;
				sem_post(my_sem_ptr);
				return 0;
			}
			sem_post(my_sem_ptr);
			gb->players |= active_player;
			Map goldMap(gb->map,sub_rows,sub_cols);

			int sub_position = 0;
			int sub_mapsize = (sub_cols*sub_rows);  
			sub_position = rand() % sub_mapsize;
			while(gb->map[sub_position] != 0)
			{
				sub_position = rand()% sub_mapsize;
			}
			gb->map[sub_position] |= active_player;
			goldMap.drawMap();
			goldMap.postNotice("Welcome to the GoldChase!!");
			int real_gold, fools_gold, winner_on_edge;
			real_gold =0;
			fools_gold = 0;
			winner_on_edge =0;
			int key = 0;
			key = goldMap.getKey();
			while(key!=81)
			{
				if(key==104)
				{ 
					if(((sub_position%gb->cols) != 0)) 
					{  
						if(!((gb->map[sub_position-1]) & G_WALL))
						{
							gb->map[sub_position] &= ~active_player;
							sub_position--;
							if(((gb->map[sub_position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[sub_position]) == G_GOLD)
								real_gold = 1;
							gb->map[sub_position] |= active_player;
						}
					}
					else if(winner_on_edge)
					{
						gb->map[sub_position] &= ~active_player;
						goldMap.drawMap();
						goldMap.postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						break;
					}
				}
				else if(key==106)
				{
					if(((sub_position+gb->cols) < sub_mapsize))
					{  
						if(!((gb->map[sub_position+gb->cols]) & G_WALL))
						{
							gb->map[sub_position] &= ~active_player;
							sub_position+=gb->cols;
							if(((gb->map[sub_position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[sub_position]) == G_GOLD)
								real_gold = 1;
							gb->map[sub_position] |= active_player;
						}
					}
					else if(winner_on_edge)
					{
						gb->map[sub_position] &= ~active_player;
						goldMap.drawMap();
						goldMap.postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						break;
					}
				}
				else if(key == 107)
				{
					if(((sub_position-gb->cols) > 0))
					{  
						if(!((gb->map[sub_position-gb->cols]) & G_WALL))
						{
							gb->map[sub_position] &= ~active_player;
							sub_position-=gb->cols;
							if(((gb->map[sub_position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[sub_position]) == G_GOLD)
								real_gold = 1;
							gb->map[sub_position] |= active_player;
						}
					}
					else if(winner_on_edge)
					{
						gb->map[sub_position] &= ~active_player;
						goldMap.drawMap();
						goldMap.postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						break;
					}
				}
				else if(key==108)
				{
					if(((sub_position+1)%gb->cols) !=0)
					{  
						if(!((gb->map[sub_position+1]) & G_WALL))  
						{
							gb->map[sub_position] &= ~active_player;
							sub_position++;
							if(((gb->map[sub_position]) & G_FOOL) == G_FOOL)
								fools_gold = 1;
							else if((gb->map[sub_position]) == G_GOLD)
								real_gold = 1;
							gb->map[sub_position] |= active_player;
						}
					}
					else if(winner_on_edge && (((sub_position+1)%gb->cols)==0))
					{
						gb->map[sub_position] &= ~active_player;
						goldMap.drawMap();
						goldMap.postNotice("You escaped with the gold and WON!");
						gb->players &= ~active_player;
						break;
					}            
				} 
				goldMap.drawMap();
				if(fools_gold == 1)
				{
					gb->map[sub_position] &= ~G_FOOL;
					goldMap.postNotice("Sorry. Fool's Gold.");
					fools_gold = 0;
				}
				if(real_gold == 1)
				{
					gb->map[sub_position] &= ~G_GOLD;
					real_gold = 0;
					winner_on_edge = 1;
					goldMap.postNotice("You found the gold! Time to make your escape!!");
				}
				key = goldMap.getKey();
			}
			sem_wait(my_sem_ptr);
			gb->players &= ~active_player;
			gb->map[sub_position] &= ~active_player;
			sem_post(my_sem_ptr);
			goldMap.drawMap();
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
