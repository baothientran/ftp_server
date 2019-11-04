#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <exception>
#include <vector>
#include <bitset>
#include <algorithm>
#include <iomanip>
#include "Socket.h"


/************************************************************
 * SocketException class definition
 ************************************************************/
const char *SocketException::what() const noexcept { return strerror(errno); }


/************************************************************
 * Socket class definition
 ************************************************************/
struct Socket::Impl {
    int sockfd;
    NetProtocol protocol;
};


Socket::Socket() {
    _impl = std::make_unique<Impl>();
    _impl->sockfd = -1;
    _impl->protocol = UNSPECIFIED;
}


Socket::Socket(Socket &&other) noexcept {
    _impl = std::move(other._impl);
}


Socket &Socket::operator=(Socket &&other) noexcept {
    if (&other != this)
        std::swap(_impl, other._impl);

    return *this;
}


Socket::~Socket() noexcept {
    if (!_impl || _impl->sockfd == -1)
        return;

    close(_impl->sockfd);
}


int Socket::pollForRead(int timeout) {
    struct pollfd fds;
    fds.fd = _impl->sockfd;
    fds.events = POLL_IN;
    return ::poll(&fds, 1, timeout);
}


bool Socket::isValid() const {
    return _impl->sockfd != -1;
}


NetProtocol Socket::netProtocol() const {
    return _impl->protocol;
}


std::string Socket::IPAddr() const {
    // retrieve local ip address will be used for PORT and EPRT cmd
    char localIp[INET6_ADDRSTRLEN];
    if (_impl->protocol == IPv4) {
        sockaddr_in addr;
        socklen_t len = sizeof(sockaddr_in);
        getsockname(_impl->sockfd, reinterpret_cast<sockaddr*>(&addr), &len);
        inet_ntop(AF_INET, &(addr.sin_addr), localIp, sizeof(localIp));
    }
    else {
        sockaddr_in6 addr;
        socklen_t len = sizeof(sockaddr_in6);
        getsockname(_impl->sockfd, reinterpret_cast<sockaddr*>(&addr), &len);
        if (IN6_IS_ADDR_V4MAPPED(&addr.sin6_addr)) {
            inet_ntop(AF_INET, reinterpret_cast<in_addr *>(addr.sin6_addr.s6_addr + 12), localIp, sizeof(localIp));
        }
        else
            inet_ntop(AF_INET6, &(addr.sin6_addr), localIp, sizeof(localIp));
    }

    return std::string(localIp);
}


std::size_t Socket::write(const Byte *buf, std::size_t size) {
    size_t writeSofar = 0;
    while (writeSofar < size) {
        auto wn = send(_impl->sockfd, buf + writeSofar, size - writeSofar, MSG_NOSIGNAL);
        if (wn < 0)
            throw SocketException();

        writeSofar += static_cast<size_t>(wn);
    }

    return writeSofar;
}


std::size_t Socket::read(Byte *buf, std::size_t size) {
    std::size_t readSoFar = 0;
    while (readSoFar < size) {
        auto rn = ::read(_impl->sockfd, buf + readSoFar, static_cast<unsigned long>(size - readSoFar));
        if (rn == -1)
            throw SocketException();

        if (rn == 0)
            break;

        readSoFar += rn;
    }

    return readSoFar;
}


std::size_t Socket::readline(char *buf, std::size_t size) {
    char ch;
    std::size_t readSoFar = 0;
    do {
        ssize_t rn = ::read(_impl->sockfd, &ch, 1);
        if (rn == -1)
            throw SocketException();

        if (rn == 0)
            break;

        buf[readSoFar] = ch;
        readSoFar += static_cast<std::size_t>(rn);
    } while (ch != '\n' && readSoFar < size);

    return readSoFar;
}


Socket Socket::accept(const Socket &listenSock) {
    int sockfd;
    sockaddr_storage peerAddr;
    socklen_t len = sizeof(peerAddr);
    sockfd = ::accept(listenSock._impl->sockfd, reinterpret_cast<sockaddr *>(&peerAddr), &len);
    if (sockfd == -1)
        throw SocketException();

    Socket socket;
    socket._impl->sockfd = sockfd;
    socket._impl->protocol = peerAddr.ss_family == AF_INET ? IPv4 : IPv6 ;
    return socket;
}


Socket Socket::connect(const std::string &host, uint16_t port) {
    int sockfd = -1;
    NetProtocol netProtocol = UNSPECIFIED;
    std::string portStr = std::to_string(port);

    // get ip address
    int stat;
    addrinfo hint, *ipAddrHdr = nullptr;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    if ((stat = getaddrinfo(host.c_str(), portStr.c_str(), &hint, &ipAddrHdr)) == -1)
        throw SocketException();

    // loop through all possible ip address to open socket
    addrinfo *ipAddr;
    for (ipAddr = ipAddrHdr; ipAddr; ipAddr = ipAddr->ai_next) {
        int fd = socket(ipAddr->ai_family, ipAddr->ai_socktype, ipAddr->ai_protocol);
        if (fd == -1)
            continue;

        if (::connect(fd, ipAddr->ai_addr, ipAddr->ai_addrlen) == -1) {
            close(fd);
            continue;
        }

        sockfd = fd;
        netProtocol = ipAddr->ai_family == AF_INET ? IPv4 : IPv6;

        break;
    }

    if (ipAddr == nullptr) {
        freeaddrinfo(ipAddrHdr);
        throw SocketException();
    }

    freeaddrinfo(ipAddrHdr);

    Socket socket;
    socket._impl->sockfd = sockfd;
    socket._impl->protocol = netProtocol;
    return socket;
}


Socket Socket::listen(uint16_t port, int queueMax, NetProtocol netProtocol) {
     int sockfd = -1;
     std::string portStr = std::to_string(port);

    // get ip address
    int stat;
    addrinfo hint, *ipAddrHdr = nullptr;
    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;
    if (netProtocol == UNSPECIFIED)
        hint.ai_family = AF_UNSPEC;
    else if (netProtocol == IPv4)
        hint.ai_family = AF_INET;
    else
        hint.ai_family = AF_INET6;

    if ((stat = getaddrinfo(nullptr, portStr.c_str(), &hint, &ipAddrHdr)) == -1)
        throw SocketException();

    // loop through all possible ip address to open socket
    addrinfo *ipAddr;
    for (ipAddr = ipAddrHdr; ipAddr; ipAddr = ipAddr->ai_next) {
        sockfd = socket(ipAddr->ai_family, ipAddr->ai_socktype, ipAddr->ai_protocol);
        if (sockfd == -1)
            continue;

        int reuse = 1;
        bool socketUnusable = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) == -1 ||
                              bind(sockfd, ipAddr->ai_addr, ipAddr->ai_addrlen)                 == -1 ||
                              ::listen(sockfd, queueMax)                                        == -1;

        if (socketUnusable) {
            close(sockfd);
            continue;
        }

        break;
    }

    if (ipAddr == nullptr) {
        freeaddrinfo(ipAddrHdr);
        throw SocketException();
    }


    Socket socket;
    socket._impl->sockfd = sockfd;
    socket._impl->protocol = ipAddr->ai_family == AF_INET ? IPv4 : IPv6;

    freeaddrinfo(ipAddrHdr);
    return socket;
}
