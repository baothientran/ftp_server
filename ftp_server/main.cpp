#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <iostream>
#include "FtpSession.h"
#include "Utility.h"


void displayUsage() {
    std::cout << "Usage: ftp_client_exe [IP addr or hostname] [log file] [port number]\n";
    std::cout << "[IP addr or hostname]: REQUIRED. The IP address or hostname of ftp server to connect to\n";
    std::cout << "[log file           ]: REQUIRED. The log file to log the client actions\n";
    std::cout << "[port number        ]: OPTIONAL. The port number used to connect to ftp server. Default is port 21\n";
}


int main(int argc, const char **argv) {
    // parsing command line
    std::string logFile, portStr;
    uint16_t port;
    if (argc == 3) {
        logFile  = argv[1];
        portStr = argv[2] ;
        int res = toUnsignedInt<uint16_t>(portStr, port);
        if (res == -1) {
            std::cout << "Port not a number.\n";
            exit(0);
        }
        else if (res == 1) {
            std::cout << "Port number overflow.\n";
            exit(0);
        }
    }
    else {
        displayUsage();
        exit(0);
    }

    // open log file
    std::ofstream logger(logFile, std::ios::app);
    if (!logger) {
        std::cout << "Cannot open file " << logFile << "\n";
        exit(0);
    }

    // spin server
    runFtpServer(port, "accounts", IPv6);

    exit(0);
}
