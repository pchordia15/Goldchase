/*
 * write template functions that are guaranteed to read and write the 
 * number of bytes desired
 */

#ifndef fancyRW_h
#define fancyRW_h
#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

using namespace std;

template<typename T>
int READ(int fd, T* obj_ptr, int count)
{
	char* addr=(char*)obj_ptr;
	//loop. Read repeatedly until count bytes are read in
	int count_bytes = count;
	int total = 0;
	int bytes = 0;
	for(count_bytes = count; count_bytes > 0; count_bytes = count_bytes - bytes)
	{
		bytes = read(fd,addr + total,count_bytes);
		cout<<"Number of Bytes read: "<<bytes;
		cout<<endl;
		if(bytes == -1)
    	{
      		if(errno == EINTR)
       			continue;
      		else
        		return -1;
    	}
    	total = total + bytes;
	}
	return bytes;
}

	template<typename T>
int WRITE(int fd, T* obj_ptr, int count)
{
	char* addr=(char*)obj_ptr;
	//loop. Write repeatedly until count bytes are written out
	int count_bytes = count;
	int total = 0;
	int bytes = 0;
	for(count_bytes = count; count_bytes > 0; count_bytes = count_bytes - bytes)
	{
		bytes = write(fd,addr + total,count_bytes);
		if(bytes == -1)
    	{
      		if(errno == EINTR)
       			continue;
      		else
        		return -1;
    	}
    	total = total + bytes;
	}
	return bytes;
}
#endif

