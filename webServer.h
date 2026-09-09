#ifndef HEADER_H
#define HEADER_H 

#include <iostream>
#include <fstream>
#include <regex>
#include <string>

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "logging.h"

#define GET 1
#define HEAD 2
#define POST 3

inline int BUFFER_SIZE = 10;

void sig_handler(int signo) {
  DEBUG << "Caught signal #" << signo << ENDL;
  DEBUG << "Closing file descriptors 3-31." << ENDL;
  closefrom(3);
  exit(1);
}

#endif
