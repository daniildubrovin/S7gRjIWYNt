#include "socket.h"

Socket::Socket() : fd(-1) { std::ofstream ifs("http.txt"); }

void Socket::create() {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) throw std::runtime_error("socket hadn't created");
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on));
}

void Socket::bind(const int port) {
    m_addr.sin_family = AF_INET;
    m_addr.sin_addr.s_addr = INADDR_ANY;
    m_addr.sin_port = htons(port);

    if(::bind(fd, (struct sockaddr *) &m_addr, sizeof(m_addr)) == -1) throw std::runtime_error("socket dont bind");
}

void Socket::listen() const {
    if(::listen(fd, MAXCONNECTIONS) == -1) throw std::runtime_error("socket dont listen");
}

Socket Socket::accept() const {
    int addr_length = sizeof(m_addr);
    Socket newSocket;
    newSocket.fd = ::accept(fd, (sockaddr *) &m_addr, (socklen_t *) &addr_length);
    Log::i("new connection: fd: " + std::to_string(newSocket.fd));
    if (newSocket.fd <= 0) throw std::runtime_error("dont create newSocket");
    return newSocket;
}

void Socket::send(const std::string& s) const {
    const char *buf = s.c_str();
    int total = 0;
    while(total < s.size()){
        int status = ::send(fd, buf + total, s.size() - total, 0);
        Log::i("sending: " + std::to_string(status));
        if (status == -1) throw SocketException("send notFound: " + std::string(strerror(errno)));
        total += status;
    }
}

void Socket::recv(std::string &s) const {
    s = "";
    char buf[MAXRECV + 1];
    memset(buf, 0, MAXRECV + 1);

    int status;

    pollfd fds[1] {{fd, POLLIN}};
    int ret = poll(fds, 1, 10000);
    fds[0].revents = 0;

    if(ret == -1) {
        Log::e("poll error: " + string(strerror(errno)));
        throw std::runtime_error("poll error: " + string(strerror(errno)));
    }
    else if(ret == 0) {
        Log::e("данных для чтения не обнаружено");
        throw std::runtime_error("poll error: waiting for data lasts more than 10 seconds");
    }

    status = ::recv(fd, buf, MAXRECV, 0);

    if(status == -1) throw std::runtime_error("recv error: " + string(strerror(errno)));
    if(status == 0) throw std::runtime_error("connection was closed");

    s.assign(buf, status);
    memset(buf, 0, MAXRECV + 1);

    std::ofstream file("http.txt", std::ios::app);  file << s;
}

const Socket &Socket::operator<<(const std::string &s) const {
    send(s);
    return *this;
}

const Socket &Socket::operator>>(std::string &s) const {
    recv(s);
    return *this;
}

void Socket::close() const {
    Log::i("close connection: fd: " + std::to_string(fd));
    ::close(fd);
}
