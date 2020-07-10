#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#define WIN32_LEAN_AND_MEAN

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "opencv2/core/core_c.h"
#include "opencv2/objdetect.hpp"
#include <iostream>
#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <io.h>
#include <string.h>
#include <sys/types.h>
#include <vector>
#include <winsock2.h>
#include <windows.h> 
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <algorithm>
#include <objbase.h>




#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "50001"
#define DEFAULT_ADDR "127.0.0.1"

using namespace std;


class TCPClient
{
  private:
    WSADATA wsaData;
    SOCKET ConnectSocket = INVALID_SOCKET;
    struct addrinfo* result = NULL,
                   * ptr = NULL,
                     hints;
    char recvbuf[DEFAULT_BUFLEN];
    int iResult;
    int recvbuflen = DEFAULT_BUFLEN;
    bool ready;
  public:
    TCPClient();
    bool setup(PCSTR address = DEFAULT_ADDR, PCSTR port = DEFAULT_PORT);
    bool Send(const char* data);
    void receive();
    bool exit();
    bool isReady();
};

#endif


//{"data" : [{"id":0, "x" : 670, "y" : 102}] { END }
//{"data" : [{"id":1, "x" : 100, "y" : 100}] , "kill" : [{"id":1}] } {END}