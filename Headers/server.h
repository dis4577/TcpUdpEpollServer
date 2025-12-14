#ifndef SERVER_H
#define SERVER_H

#include <signal.h>
#include <sys/signalfd.h>
#include <thread>
#include <string.h>
#include <map>
#include <chrono>
#include <future>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <atomic>
#include <sys/epoll.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <set>

class Server
{
public:
    Server() = default;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    int init(__socket_type sockType, int port);
    int run();

private:
    int checkRunning(size_t updInterval);
    std::string getCurrentTimeAndDate();
    int closeConnection(int socktFD);
    void setSockAddr(sockaddr_in* addr, int port);
    int isValidFD(int sockFD);
    int setNonBlocking(int sockFD);
    int readFromSocket(int sockFD, std::array<char, 1024>* buffer, size_t bufDataSize, __socket_type sockType, sockaddr* cliAddr);
    int writeToSocket(int sockFD, std::array<char, 1024>* buffer, size_t bufDataSize, __socket_type sockType, sockaddr* cliAddr);
    int handleTcp(int sockFD);
    int handleUdp(int sockFD);
    int closeAppSignalsInit();
    int createListenSocket(__socket_type sockType, int port);
    int epollCtlAdd(int epFD, int FD, uint32_t events);
    int epollCtlDel(int epFD, int FD);
    int epollCreate();

private:
    int m_closeAppFD = {};
    int m_listenSocketFD = {};
    int m_epollFD = {};
    std::mutex m_threadsMutex = {};
    std::multimap<int, std::future<int>> m_activeThreads = {};
    __socket_type m_sockType = {};
    std::atomic<unsigned> m_connectedClients = {};
    std::atomic<unsigned> m_allClients = {};
    std::atomic<bool> m_Running = {false};
};

#endif // SERVER_H
