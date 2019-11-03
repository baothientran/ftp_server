#ifndef FTPSESSION_H
#define FTPSESSION_H

#include <memory>
#include <vector>
#include <string>
#include <limits>
#include <functional>
#include "Socket.h"


class FtpServerDTP;


enum TransferMode {
    ASCII,
    BINARY
};


enum FtpCode {
    // RFC 959 reply code
    COMMAND_OK = 200,
    COMMAND_NOT_RECOGNIZED = 500,
    COMMAND_ARGS_NOT_RECOGNIZED = 501,
    COMMAND_NOT_IMPLEMENTED_SUPERFLOUS = 202,
    COMMAND_NOT_IMPLEMENTED = 502,
    BAD_SEQUENCE_COMMAND = 503,
    COMMAND_NOT_IMPLEMENTED_FOR_ARGS = 504,
    RESTARTER_MARKER_REPLY = 110,
    SYSTEM_STATUS = 211,
    DIRECTORY_STATUS = 212,
    FILE_STATUS = 213,
    HELP_MESSAGE = 214,
    NAME_SYSTEM_TYPE = 215,

    SERVICE_DELAY = 120,
    SERVICE_READY = 220,
    SERVICE_CLOSE_CTRL_CONNECTION = 221,
    SERVICE_UNAVAILABLE = 421,
    DATA_CONNECTION_OPEN_TRANSFER_STARTING = 125,
    DATA_CONNECTION_OPEN_NO_TRANSFER_IN_PROGRESS = 225,
    CANNOT_OPEN_DATA_CONNECTION = 425,
    CLOSE_DATA_CONNECTION_REQUEST_FILE_ACTION_SUCCESS = 226,
    CONNECTION_CLOSE_TRANSFER_ABORT = 426,
    ENTERING_PASSIVE_MODE = 227,

    USER_LOGGED_IN_PROCCEED = 230,
    USER_NOT_LOGGED_IN = 530,
    USER_OK_PASSWORD_NEEDED = 331,
    ACCT_NEEDED_FOR_LOGGED_IN = 332,
    ACCT_NEEDED_FOR_STORING_FILE = 532,
    FILE_STATUS_OK_OPEN_DATA_CONNECTION = 150,
    REQUESTED_FILE_ACTION_COMPLETED = 250,
    PATHNAME_CREATED = 257,
    REQUESTED_FILE_ACTION_PENDING_FOR_FURTHER_INFO = 350,
    REQUESTED_FILE_ACTION_NOT_TAKEN_FILE_TEMP_UNAVAILABLE = 450,
    REQUESTED_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE = 550,
    REQUESTED_ACTION_ABORTED_LOCAL_ERROR_PROCESSING = 451,
    REQUESTED_ACTION_ABORTED_PAGE_TYPE_UNKNOWN = 551,
    REQUESTED_ACTION_NOT_TAKEN_INSUFFICIENT_STORAGE = 452,
    REQUESTED_ACTION_ABORTED_EXCEEDED_STORAGE_ALLOCATION = 552,
    REQUESTED_ACTION_NOT_TAKEN_FILENAME_NOT_ALLOWED = 553,

    // RFC 2428 reply code
    NETWORK_PROTOCOL_NOT_SUPPORTED = 522,
    ENTERING_EXTENDED_PASSIVE_MODE = 229,
};


void runFtpServer(uint16_t port, const std::string &accountsFile, NetProtocol protocol);


class FtpServerPI
{
public:
    FtpServerPI(Socket ctrlSock, std::string accountsFile);

    FtpServerPI(const FtpServerPI &) = delete;

    FtpServerPI &operator=(const FtpServerPI &) = delete;

    ~FtpServerPI();

    void run();

    void writeCtrl(FtpCode code, const std::string &reply);

    void closeCtrl();

    const std::string &accountsFile() const;

    std::string serverIPAddr() const;

    FtpServerDTP &DTP();

    std::string username;
    std::string userNativeHomeDir;
    std::string userWorkingDir;
    bool        EPSVexclusive;
    bool        loggedIn;
    bool        quit;

    // 5 minutes timeout for each user session
    static const int TIME_OUT = 5 * 60 * 1000;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};


class FtpServerDTP {
public:
    FtpServerDTP();

    ~FtpServerDTP();

    void setTransferMode(TransferMode mode);

    TransferMode transferMode() const;

    bool doesDataConnectSetup() const;

    void setupActiveMode(const std::string &receiverIP,
                         uint16_t port,
                         NetProtocol protocol);

    void setupPassiveMode(uint16_t port, NetProtocol protocol);

    void closeDataConnect();

    void openData();

    void writeData(std::istream &data);

    void readData(std::ostream &data);

    static const uint16_t USABLE_PORT_MIN  = 1024;

    static const uint16_t USABLE_PORT_MAX  = std::numeric_limits<uint16_t>::max();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};


class FtpCommand {
public:
    FtpCommand(FtpServerPI *PI);

    virtual ~FtpCommand();

    virtual void execute(const std::vector<std::string> &args) = 0;

    FtpServerPI *PI();

    std::string convertToNativePath(const std::string &userPath);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};


class TYPECommand : public FtpCommand {
public:
    TYPECommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};



class USERCommand : public FtpCommand {
public:
    USERCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class PASSCommand : public FtpCommand {
public:
    PASSCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class PWDCommand : public FtpCommand {
public:
    PWDCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class CWDCommand : public FtpCommand {
public:
    CWDCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class CDUPCommand : public FtpCommand {
public:
    CDUPCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class QUITCommand : public FtpCommand {
public:
    QUITCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class PORTCommand : public FtpCommand {
public:
    PORTCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class EPRTCommand : public FtpCommand {
public:
    EPRTCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class PASVCommand : public FtpCommand {
public:
    PASVCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class EPSVCommand : public FtpCommand {
public:
    EPSVCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class LISTCommand : public FtpCommand {
public:
    LISTCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class RETRCommand : public FtpCommand {
public:
    RETRCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};


class STORCommand : public FtpCommand {
public:
    STORCommand(FtpServerPI *session)
        : FtpCommand{session}
    {}

    void execute(const std::vector<std::string> &args) override;

    static const std::string PROG;
};

#endif // FTPSESSION_H
