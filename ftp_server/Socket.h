#ifndef SOCKET_H
#define SOCKET_H

#include <string>
#include <memory>


using Byte = unsigned char;


enum NetProtocol {
    UNSPECIFIED = 0,
    IPv4 = 1,
    IPv6 = 2,
};


class SocketException : public std::exception {
public:
    const char *what() const noexcept override;
};


class Socket {
public:
    Socket();

    Socket(const Socket &) = delete;

    Socket(Socket &&other) noexcept;

    Socket &operator=(const Socket &) = delete;

    Socket &operator=(Socket &&) noexcept;

    ~Socket() noexcept;

    int pollForRead(int timeout);

    bool isValid() const;

    NetProtocol netProtocol() const;

    std::string IPAddr() const;

    std::size_t write(const Byte *buf, std::size_t size);

    std::size_t read(Byte *buf, std::size_t size);

    std::size_t readline(char *buf, std::size_t size);

    static Socket accept(const Socket &listenSock);

    static Socket connect(const std::string &host, uint16_t port);

    static Socket listen(uint16_t port, int queueMax, NetProtocol netProtocol);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};


#endif // SOCKET_H
