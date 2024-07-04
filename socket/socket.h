// Definition of the Socket class

#ifndef Socket_class
#define Socket_class

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <sys/poll.h>
#include <iterator>
#include <sstream>
#include <algorithm>
#include "../Logger.h"
#include "../Exceptions.h"

using std::cout;
using std::endl;
using std::string;

class Socket {
private:
    int fd;
    sockaddr_in m_addr;
    const int MAXCONNECTIONS = SOMAXCONN;
    const int MAXRECV = 65535;
public:
    Socket();
    void create();
    void bind(const int port);
    void listen() const;
    Socket accept() const;
    void close() const;
    void send(const std::string &) const;
    void recv(std::string &) const;
    const Socket &operator<<(const std::string &s) const;
    const Socket &operator>>(std::string &s) const;
};

#endif