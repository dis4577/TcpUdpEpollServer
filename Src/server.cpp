#include <Headers/server.h>

using namespace std::chrono_literals;

int Server::init(__socket_type sockType, int port)
{
    m_closeAppFD = closeAppSignalsInit();
    if(m_closeAppFD < 0){
        return 1;
    }

    m_listenSocketFD = createListenSocket(sockType, port);
    if(m_listenSocketFD < 0){
        return 2;
    }

    m_epollFD = epollCreate();
    if(m_epollFD == -1){
        return 3;
    }

    if(epollCtlAdd(m_epollFD, m_listenSocketFD, EPOLLIN | EPOLLET) == -1){
        return 4;
    }

    if(epollCtlAdd(m_epollFD, m_closeAppFD, EPOLLIN) == -1){
        return 5;
    }

    return 0;
}

int Server::run()
{
    std::cout << "Server is running." << std::endl;

    m_Running = true;
    std::future<int> checkRun = std::async(std::launch::async, &Server::checkRunning, this, 1000);

    int result = 0;
    epoll_event events[32] = {};

    for (;;)
    {
        int numFDs = epoll_wait(m_epollFD, events, 32, -1);
        if(numFDs == -1)
        {
            m_Running = false;
            result = 6;
        }

        for (int i = 0; i < numFDs; i++)
        {
            if (events[i].data.fd == m_closeAppFD)
            {
                signalfd_siginfo FDsi = {};
                ssize_t bytes = read(m_closeAppFD, &FDsi, sizeof(signalfd_siginfo));
                if (bytes != sizeof(signalfd_siginfo))
                {
                    m_Running = false;
                    result = 7;

                    break;
                }

                if((FDsi.ssi_signo == SIGINT) || (FDsi.ssi_signo == SIGTERM) || (FDsi.ssi_signo == SIGHUP))
                {
                    m_Running = false;
                    result = 0;

                    break;
                }
            }

            if(m_sockType == SOCK_DGRAM)
            {
                if (events[i].data.fd == m_listenSocketFD)
                {
                    int clientFD = events[i].data.fd;
                    std::lock_guard<std::mutex> lock(m_threadsMutex);
                    m_activeThreads.insert(std::make_pair(clientFD, std::async(std::launch::async, &Server::handleUdp, this, clientFD)));
                }
            }
            else if(m_sockType == SOCK_STREAM)
            {
                if (events[i].data.fd == m_listenSocketFD)
                {
                    /* new connection */
                    char buf[16] = {};

                    sockaddr_in cliAddr = {};
                    socklen_t sockLen = sizeof(cliAddr);

                    int accSockFD = accept(m_listenSocketFD, (sockaddr*)&cliAddr, &sockLen);
                    if(accSockFD == -1)
                    {
                        m_Running = false;
                        result = 8;

                        break;
                    }

                    inet_ntop(AF_INET, (char*)&(cliAddr.sin_addr), buf, sockLen);

                    if(setNonBlocking(accSockFD) == -1)
                    {
                        m_Running = false;
                        result = 9;

                        break;
                    }

                    if(epollCtlAdd(m_epollFD, accSockFD, EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP) == -1)
                    {
                        m_Running = false;
                        result = 10;

                        break;
                    }

                    std::cout << "New connection: " << accSockFD << std::endl;

                    ++m_connectedClients;
                    ++m_allClients;
                }
                else
                {
                    if (events[i].events == EPOLLIN)
                    {
                        /* handle EPOLLIN event */
                        int clientFD = events[i].data.fd;

                        std::lock_guard<std::mutex> lock(m_threadsMutex);
                        m_activeThreads.insert(std::make_pair(clientFD, std::async(std::launch::async, &Server::handleTcp, this, clientFD)));
                    }
                    else
                    {
                        if (events[i].events == (EPOLLRDHUP | EPOLLIN) )
                        {
                            --m_connectedClients;

                            if (closeConnection(events[i].data.fd) != 1)
                            {
                                m_Running = false;
                                result = 11;

                                break;
                            }
                        }
                    }
                }
            }
        }

        if(!m_Running){
            break;
        }
    }

    std::cout << "Server is stopping. Waiting for threads finishing" << std::endl;
    while(checkRun.wait_for(1s) != std::future_status::ready){
        std::cout << "." << std::flush;
    }

    std::cout << std::endl << "Server has stopped." << std::endl;

    return result;
}

int Server::checkRunning(size_t updInterval)
{
    const auto waitDur = std::chrono::milliseconds(updInterval);

    //Server is running
    while(m_Running)
    {
        std::this_thread::sleep_for(waitDur);

        std::lock_guard<std::mutex> lock(m_threadsMutex);

        for (auto it = m_activeThreads.begin(); it != m_activeThreads.end();)
        {
            auto status = (it->second).wait_for(0s);
            if (status == std::future_status::ready){
               it = m_activeThreads.erase(it);
            }
            else{
                it++;
            }
        }
    }

    // Server is closing
    if (m_sockType == SOCK_STREAM){
        closeConnection(m_listenSocketFD);
    }

    std::set<int> activeConn;
    for(auto& it : m_activeThreads){
        activeConn.insert(it.first);
    }

    for (auto it = activeConn.begin(); it != activeConn.end(); ++it)
    {
        auto range = m_activeThreads.equal_range(*it);
        for (auto itR = range.first; itR != range.second;)
        {
            auto status = (itR->second).wait_for(0s);
            if (status == std::future_status::ready){
                ++itR;
            }
        }

        closeConnection(*it);

        m_activeThreads.erase(range.first, range.second);
    }

    return 0;
}

std::string Server::getCurrentTimeAndDate()
{
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %X");

    return ss.str();
}

int Server::closeConnection(int sockFD)
{
    if (m_sockType == SOCK_STREAM)
    {
        if(shutdown(sockFD, 2) == -1){
            return -1;
        }
    }

    if(close(sockFD) == -1){
        return -2;
    }

    return 1;
}

void Server::setSockAddr(sockaddr_in* addr, int port)
{
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons(port);
}

int Server::setNonBlocking(int sockFD)
{
    if (fcntl(sockFD, F_SETFL, fcntl(sockFD, F_GETFL, 0) | O_NONBLOCK) == -1) {
        return -1;
    }

    return 1;
}

int Server::isValidFD(int sockFD)
{
    return fcntl(sockFD, F_GETFL) != -1 || errno != EBADF;
}

int Server::readFromSocket(int sockFD, std::array<char, 1024>* buffer, size_t bufDataSize, __socket_type sockType, sockaddr* cliAddr = NULL)
{
    int bytesRecv = 0;

    if(sockType == SOCK_STREAM){
        bytesRecv = read(sockFD, buffer->data(), bufDataSize);
    }
    else if(sockType == SOCK_DGRAM)
    {
        if (cliAddr == NULL){
            return - 1;
        }

        socklen_t len = sizeof(*cliAddr);
        bytesRecv = recvfrom(sockFD, buffer->data(), bufDataSize, MSG_WAITALL, cliAddr, &len);
    }

    return bytesRecv;
}

int Server::writeToSocket(int sockFD, std::array<char, 1024>* buffer, size_t bufDataSize, __socket_type sockType, sockaddr* cliAddr = NULL)
{
    int bytesSend = 0;

    if(sockType == SOCK_STREAM){
        bytesSend = write(sockFD, buffer->data(), bufDataSize);
    }
    else if(sockType == SOCK_DGRAM)
    {
        if (cliAddr == NULL){
            return - 1;
        }

        socklen_t len = sizeof(*cliAddr);
        bytesSend = sendto(sockFD, buffer->data(), bufDataSize, MSG_CONFIRM, cliAddr, len);
    }

    return bytesSend;
}

int Server::handleTcp(int sockFD)
{
    std::array<char, 1024> buffer = {};

    int nRead = readFromSocket(sockFD, &buffer, sizeof(buffer), SOCK_STREAM);
    if(nRead <= 0)
    {
        --m_connectedClients;

        closeConnection(sockFD);

        return -1;
    }

    std::string msg = buffer.data();
    if(msg.at(0) == '/')
    {
        buffer.fill(0);
        std::stringstream strStream;

        if (msg.find("/shutdown") != std::string::npos)
        {
            --m_connectedClients;

            closeConnection(sockFD);

            return sockFD;
        }
        else if (msg.find("/stats") != std::string::npos){
            strStream << "Connected: " << to_string(m_connectedClients) << ", Total for all time: " << to_string(m_allClients) << std::endl;
        }
        else if(msg.find("/time") != std::string::npos){
            strStream << getCurrentTimeAndDate() << std::endl;
        }
        else{
            strStream << "Command not found." << std::endl;
        }

        std::string outMsg = strStream.str();
        copy(outMsg.begin(), outMsg.end(), buffer.data());
    }

    int lenMsg = strlen(buffer.data());
    int nWrite = writeToSocket(sockFD, &buffer, lenMsg, SOCK_STREAM);
    if(nWrite <= 0)
    {
        --m_connectedClients;

        closeConnection(sockFD);

        return -2;
    }

    return sockFD;
}

int Server::handleUdp(int sockFD)
{
    sockaddr cliAddr = {};

    std::array<char, 1024> buffer = {};

    int nRead = readFromSocket(sockFD, &buffer, sizeof(buffer), SOCK_DGRAM, &cliAddr);
    if(nRead <= 0){
        return -1;
    }

    std::string msg = buffer.data();
    if(msg.at(0) == '/')
    {
        buffer.fill(0);
        std::stringstream strStream;

        if (msg.find("/stats") != std::string::npos){
            strStream << "Udp connection. Command not supported" << std::endl;
        }
        else if(msg.find("/time") != std::string::npos){
            strStream << getCurrentTimeAndDate() << std::endl;
        }
        else if (msg.find("/shutdown") != std::string::npos){
            strStream << "Udp connection. Command not supported" << std::endl;
        }
        else{
            strStream << "Command not found." << std::endl;
        }

        std::string outMsg = strStream.str();
        copy(outMsg.begin(), outMsg.end(), buffer.data());
    }

    int lenMsg = strlen(buffer.data());
    int nWrite = writeToSocket(sockFD, &buffer, lenMsg, SOCK_DGRAM, &cliAddr);
    if(nWrite <= 0){
        return -2;
    }

    return sockFD;
}

int Server::closeAppSignalsInit()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        return -1;
    }

    int clAppFD = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if(clAppFD == -1){
        return -2;
    }

    return clAppFD;
}

int Server::createListenSocket(__socket_type sockType, int port)
{
    sockaddr_in srvAddr = {};
    setSockAddr(&srvAddr, port);

    int listenSockFD = socket(AF_INET, sockType, 0);
    if (listenSockFD == -1) {
        return -1;
    }

    if (bind(listenSockFD, (sockaddr*)&srvAddr, sizeof(srvAddr)) == -1){
        return -2;
    }

    if (setNonBlocking(listenSockFD) == -1){
        return -3;
    }

    if (sockType == SOCK_STREAM)
    {
        if (listen(listenSockFD, 5) == -1){
            return -4;
        }
    }

    m_sockType = sockType;

    return listenSockFD;
}

int Server::epollCtlAdd(int epFD, int FD, uint32_t events)
{
    epoll_event ev;
    ev.events = events;
    ev.data.fd = FD;
    if (epoll_ctl(epFD, EPOLL_CTL_ADD, FD, &ev) == -1) {
        return -1;
    }

    return 1;
}

int Server::epollCtlDel(int epFD, int FD)
{
    if (epoll_ctl(epFD, EPOLL_CTL_DEL, FD, NULL) == -1){
        return -1;
    }

    return 1;
}

int Server::epollCreate()
{
    int epollFD = epoll_create(1);
    if(epollFD == -1){
        return - 1;
    }

    return epollFD;
}
