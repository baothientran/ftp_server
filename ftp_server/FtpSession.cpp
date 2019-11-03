#include <map>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <dirent.h>
#include <algorithm>
#include <sys/stat.h>
#include <cstring>
#include "FtpSession.h"
#include "Socket.h"
#include "Utility.h"

static const int QUEUE_MAX = 100;
static const int BUF_MAX   = 2048;


/************************************************************
 * runServer definition
 ************************************************************/
static void removeFtpUSerSession(std::vector<std::thread> &threadlist,
                                 std::mutex &threadlistMutex,
                                 std::thread::id threadId)
{
    std::lock_guard<std::mutex> guard(threadlistMutex);
    auto thread = std::find_if(threadlist.begin(), threadlist.end(), [&](const std::thread &t){ return t.get_id() == threadId; });
    if (thread != threadlist.end()) {
        thread->join();
        threadlist.erase(thread);
    }
}


static void runFtpUserSession(std::vector<std::thread> &threadlist,
                              std::mutex &threadlistMutex,
                              Socket socket, std::string accountsFile)
{
    FtpServerPI ftpPI(std::move(socket), accountsFile);
    ftpPI.run();
    std::thread removeThread(removeFtpUSerSession,
                             std::ref(threadlist),
                             std::ref(threadlistMutex),
                             std::this_thread::get_id());
    removeThread.detach();
}


void runFtpServer(uint16_t port, const std::string &accountsFile, NetProtocol protocol) {
    Socket listenSock;
    try {
        listenSock = Socket::listen(port, QUEUE_MAX, protocol);
    } catch (const SocketException &e) {
        std::cout << e.what() << "\n";
        return;
    }

    std::vector<std::thread> threadlist;
    std::mutex threadlistMutex;
    while (true) {
        try {
            Socket connectSock = Socket::accept(listenSock);
            std::thread thread(runFtpUserSession,
                               std::ref(threadlist),
                               std::ref(threadlistMutex),
                               std::move(connectSock),
                               accountsFile);

            // guard threadlist before modified
            std::lock_guard<std::mutex> guard(threadlistMutex);
            threadlist.push_back(std::move(thread));

        } catch (const SocketException &e) {
            std::cout << e.what() << "\n";
        }
    }
}


/************************************************************
 * FtpUserSession class definition
 ************************************************************/
struct FtpServerPI::Impl {
    std::vector<std::string> parseCommandLine(const std::string &input) {
        std::vector<std::string> args;
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (input[i] == ' ') {
                auto cmd  = input.substr(0, i);
                auto rest = input.substr(i+1, input.size()-i - 3);
                args.push_back(cmd);
                if (!rest.empty())
                    args.push_back(rest);

                return args;
            }
        }

        return { input.substr(0, input.size()-2) };
    }

    Socket      ctrlSock;
    std::string accountsFile;
    FtpServerDTP ftpDTP;
    std::map<std::string, std::unique_ptr<FtpCommand>> loginCommands;
    std::map<std::string, std::unique_ptr<FtpCommand>> commands;
};


FtpServerPI::FtpServerPI(Socket ctrlSock, std::string accountsFile) {
    _impl = std::make_unique<Impl>();
    _impl->ctrlSock        = std::move(ctrlSock);
    _impl->accountsFile    = std::move(accountsFile);

    // shared state variables
    username          = "";
    userNativeHomeDir = "";
    userWorkingDir    = "";
    EPSVexclusive     = false;
    loggedIn          = false;
    quit              = false;

    // initiate login commands
    _impl->loginCommands.insert({USERCommand::PROG, std::make_unique<USERCommand>(this)});
    _impl->loginCommands.insert({PASSCommand::PROG, std::make_unique<PASSCommand>(this)});
    _impl->loginCommands.insert({QUITCommand::PROG, std::make_unique<QUITCommand>(this)});

    // initiate normal commands
    _impl->commands.insert({TYPECommand::PROG, std::make_unique<TYPECommand>(this)});
    _impl->commands.insert({ PWDCommand::PROG, std::make_unique<PWDCommand>(this)});
    _impl->commands.insert({ CWDCommand::PROG, std::make_unique<CWDCommand>(this)});
    _impl->commands.insert({CDUPCommand::PROG, std::make_unique<CDUPCommand>(this)});
    _impl->commands.insert({PORTCommand::PROG, std::make_unique<PORTCommand>(this)});
    _impl->commands.insert({EPRTCommand::PROG, std::make_unique<EPRTCommand>(this)});
    _impl->commands.insert({PASVCommand::PROG, std::make_unique<PASVCommand>(this)});
    _impl->commands.insert({EPSVCommand::PROG, std::make_unique<EPSVCommand>(this)});
    _impl->commands.insert({LISTCommand::PROG, std::make_unique<LISTCommand>(this)});
    _impl->commands.insert({RETRCommand::PROG, std::make_unique<RETRCommand>(this)});
    _impl->commands.insert({STORCommand::PROG, std::make_unique<STORCommand>(this)});
}


FtpServerPI::~FtpServerPI()
{}


void FtpServerPI::run() {
    writeCtrl(SERVICE_READY, "Service ready");

    while (true) {
        // poll for any activity
        int res = _impl->ctrlSock.pollForRead(TIME_OUT);
        if (res == 0 || res == -1) {
            writeCtrl(SERVICE_UNAVAILABLE, "Time out");
            break;
        }

        // read command from client
        char input[BUF_MAX];
        auto rn = _impl->ctrlSock.readline(input, BUF_MAX);
        if (rn >= BUF_MAX) {
            writeCtrl(COMMAND_NOT_RECOGNIZED, "Command too long");
            continue;
        }
        input[rn] = '\0';

        // begin executing command if it is valid
        std::vector<std::string> args = _impl->parseCommandLine(input);
        if (args.empty()) {
            writeCtrl(COMMAND_NOT_RECOGNIZED, "Command empty");
            continue;
        }

        // command is a login command
        std::transform(args[0].begin(), args[0].end(), args[0].begin(), ::toupper);
        auto loginCmd = _impl->loginCommands.find(args[0]);
        if (loginCmd != _impl->loginCommands.end()) {
            loginCmd->second->execute(args);
            continue;
        }

        // command is a normal command required login
        auto cmd = _impl->commands.find(args[0]);
        if (cmd == _impl->commands.end())
            writeCtrl(COMMAND_NOT_RECOGNIZED, "Unrecognized command");
        else if (loggedIn)
            cmd->second->execute(args);
        else
            writeCtrl(USER_NOT_LOGGED_IN, "Not logged in");

        // check if session is quit
        if (quit)
            break;
    }
}


void FtpServerPI::writeCtrl(FtpCode code, const std::string &msg) {
    std::string reply = std::to_string(code) + " " + msg + "\r\n";
    _impl->ctrlSock.write(reinterpret_cast<const Byte *>(reply.data()), reply.size());
}


const std::string &FtpServerPI::accountsFile() const {
    return _impl->accountsFile;
}


std::string FtpServerPI::serverIPAddr() const {
    return _impl->ctrlSock.IPAddr();
}


FtpServerDTP &FtpServerPI::DTP() {
    return _impl->ftpDTP;
}


/************************************************************
 * FtpServerDTP class definition
 ************************************************************/
struct FtpServerDTP::Impl {
    void writeBinaryMode(std::istream &data) {
        Byte buf[BUF_MAX];
        std::streamsize size = BUF_MAX;
        while (data) {
            data.read(reinterpret_cast<char *>(buf), size);
            dataSock.write(buf, static_cast<std::size_t>(data.gcount()));
        }
    }


    void writeAsciiMode(std::istream &data) {
        while (data) {
            std::string line;
            std::getline(data, line);
            if (!line.empty()) {
                line.back() = '\r';
                line += "\n";
            }

            dataSock.write(reinterpret_cast<const Byte *>(line.c_str()), line.size());
        }
    }


    Socket passiveSock;
    Socket dataSock;
    std::string receiverIP;
    NetProtocol netProtocol;
    TransferMode transferMode;
    uint16_t port;
    bool activeMode;
    bool connectSetup;
};


FtpServerDTP::FtpServerDTP() {
    _impl = std::make_unique<Impl>();
    _impl->passiveSock  = Socket();
    _impl->dataSock     = Socket();
    _impl->receiverIP   = "";
    _impl->netProtocol  = UNSPECIFIED;
    _impl->transferMode = ASCII;
    _impl->port         = USABLE_PORT_MIN;
    _impl->activeMode   = true;
    _impl->connectSetup = false;
}


FtpServerDTP::~FtpServerDTP()
{}


void FtpServerDTP::setTransferMode(TransferMode mode) {
    _impl->transferMode = mode;
}


TransferMode FtpServerDTP::transferMode() const {
    return _impl->transferMode;
}


bool FtpServerDTP::doesDataConnectSetup() const {
    return _impl->connectSetup;
}


void FtpServerDTP::closeDataConnect() {
    _impl->passiveSock  = Socket();
    _impl->dataSock     = Socket();
    _impl->receiverIP   = "";
    _impl->netProtocol  = UNSPECIFIED;
    _impl->port         = USABLE_PORT_MIN;
    _impl->activeMode   = true;
    _impl->connectSetup = false;
}


void FtpServerDTP::openData() {
    if (_impl->activeMode)
        _impl->dataSock = Socket::connect(_impl->receiverIP, _impl->port);
    else
        _impl->dataSock = Socket::accept(_impl->passiveSock);
}


void FtpServerDTP::setupActiveMode(const std::string &receiverIP,
                                   uint16_t port,
                                   NetProtocol protocol)
{
    _impl->receiverIP   = receiverIP;
    _impl->netProtocol  = protocol;
    _impl->port         = port;
    _impl->activeMode   = true;
    _impl->connectSetup = true;
}


void FtpServerDTP::setupPassiveMode(uint16_t port, NetProtocol protocol) {
    _impl->passiveSock = Socket::listen(port, QUEUE_MAX, protocol);
    _impl->netProtocol  = protocol;
    _impl->port         = port;
    _impl->activeMode   = false;
    _impl->connectSetup = true;
}


void FtpServerDTP::writeData(std::istream &data) {
    if (_impl->transferMode == BINARY)
        _impl->writeBinaryMode(data);
    else if (_impl->transferMode == ASCII)
        _impl->writeAsciiMode(data);
}


void FtpServerDTP::readData(std::ostream &data) {
    Byte buf[BUF_MAX];
    std::size_t rn;
    while ((rn = _impl->dataSock.read(buf, BUF_MAX)) != 0) {
        data.write(reinterpret_cast<const char *>(buf), rn);
    }
}


/************************************************************
 * FtpCommand class definition
 ************************************************************/
struct FtpCommand::Impl {
    FtpServerPI *session;
};


FtpCommand::FtpCommand(FtpServerPI *session) {
    _impl = std::make_unique<Impl>();
    _impl->session = session;
}


FtpCommand::~FtpCommand()
{}


FtpServerPI *FtpCommand::PI() {
    return _impl->session;
}


std::string FtpCommand::convertToNativePath(const std::string &userPath) {
    auto ftpPI = PI();

    std::string nativePath;
    if (!userPath.empty()) {
        bool absolutePath = userPath[0] == '/';
        nativePath = absolutePath ?
                                 normalizePath(userPath) :
                                 normalizePath(ftpPI->userWorkingDir + "/" + userPath);

        nativePath = "/" + ftpPI->userNativeHomeDir + "/" + nativePath;
    }
    else
        nativePath = "/" + ftpPI->userNativeHomeDir + "/" + ftpPI->userWorkingDir;

    return nativePath;
}


/************************************************************
 * TYPECommand class definition
 ************************************************************/
const std::string TYPECommand::PROG = "TYPE";


void TYPECommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    auto &ftpDTP = ftpPI->DTP();

    if (args.size() != 2) {
        ftpPI->writeCtrl(COMMAND_ARGS_NOT_RECOGNIZED, "Cannot recognize code type");
        return;
    }

    if (args[1] == "a" || args[1] == "A") {
        ftpPI->writeCtrl(COMMAND_OK , "Switch to ASCII mode");
        ftpDTP.setTransferMode(ASCII);
    }
    else if (args[1] == "i" || args[1] == "I") {
        ftpPI->writeCtrl(COMMAND_OK, "Switch to BINARY mode");
        ftpDTP.setTransferMode(BINARY);
    }
    else
        ftpPI->writeCtrl(COMMAND_NOT_IMPLEMENTED_FOR_ARGS , "Type " + args[1] + " not implemented");
}


/************************************************************
 * USERCommand class definition
 ************************************************************/
const std::string USERCommand::PROG = "USER";


void USERCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    std::string username;
    if (args.size() == 2)
        username = std::move(args[1]);

    if (!ftpPI->loggedIn) {
        ftpPI->username = username;
        ftpPI->writeCtrl(USER_OK_PASSWORD_NEEDED, "Please specify the password");
    }
    else if (ftpPI->username == username)
        ftpPI->writeCtrl(USER_OK_PASSWORD_NEEDED, "Any password will do");
    else
        ftpPI->writeCtrl(USER_NOT_LOGGED_IN, "Can't change to another user");
}


/************************************************************
 * PASSCommand class definition
 ************************************************************/
const std::string PASSCommand::PROG = "PASS";


void PASSCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    if (ftpPI->loggedIn) {
        ftpPI->writeCtrl(USER_LOGGED_IN_PROCCEED, "Already logged in");
        return;
    }
    else if (ftpPI->username.empty()) {
        ftpPI->writeCtrl(BAD_SEQUENCE_COMMAND, "Login with USER first");
        return;
    }

    std::string pass;
    if (args.size() == 2)
        pass = std::move(args[1]);

    std::ifstream accounts(ftpPI->accountsFile());
    if (!accounts) {
        ftpPI->writeCtrl(USER_NOT_LOGGED_IN, "Accounts file not found");
        return;
    }

    std::string actualUser, actualPass, actualHomeDir;
    while (accounts >> actualUser &&
           accounts >> actualPass &&
           accounts >> actualHomeDir)
    {
        if (actualUser == ftpPI->username &&
            actualPass == pass)
        {
            ftpPI->loggedIn = true;
            ftpPI->userNativeHomeDir = normalizePath(actualHomeDir);
            ftpPI->userWorkingDir = "";
            ftpPI->writeCtrl(USER_LOGGED_IN_PROCCEED, "User logged in, proceed");
            return;
        }
    }

    ftpPI->loggedIn = false;
    ftpPI->username = "";
    ftpPI->writeCtrl(USER_NOT_LOGGED_IN, "Login incorrect");
}


/************************************************************
 * PWDCommand class definition
 ************************************************************/
const std::string PWDCommand::PROG = "PWD";


void PWDCommand::execute(const std::vector<std::string> &) {
    auto ftpPI = PI();
    std::string workingDir = "/" + ftpPI->userWorkingDir;
    ftpPI->writeCtrl(PATHNAME_CREATED, "\"" + workingDir + "\" is the current directory");
}


/************************************************************
 * CWDCommand class definition
 ************************************************************/
const std::string CWDCommand::PROG = "CWD";


void CWDCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();

    std::string userWorkingDir;
    if (args.size() == 2) {
        bool absolutePath = args[1][0] == '/';
        userWorkingDir = absolutePath ?
                                      normalizePath(args[1]) :
                                      normalizePath(ftpPI->userWorkingDir + "/" + args[1]);
    }

    if (isDiretory("/" + ftpPI->userNativeHomeDir + "/" + userWorkingDir)) {
        ftpPI->userWorkingDir = userWorkingDir;
        ftpPI->writeCtrl(REQUESTED_FILE_ACTION_COMPLETED , "Directory change okay");
    }
    else
        ftpPI->writeCtrl(REQUESTED_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE, "Failed to change directory");
}


/************************************************************
 * CDUPCommand class definition
 ************************************************************/
const std::string CDUPCommand::PROG = "CDUP";


void CDUPCommand::execute(const std::vector<std::string> &) {
    auto ftpPI = PI();

    std::string userWorkingDir = normalizePath(ftpPI->userWorkingDir + "/..");
    if (isDiretory("/" + ftpPI->userNativeHomeDir + "/" + userWorkingDir)) {
        ftpPI->userWorkingDir = userWorkingDir;
        ftpPI->writeCtrl(REQUESTED_FILE_ACTION_COMPLETED , "Directory change okay");
    }
    else
        ftpPI->writeCtrl(REQUESTED_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE, "Failed to change directory");
}


/************************************************************
 * QUITCommand class definition
 ************************************************************/
const std::string QUITCommand::PROG = "QUIT";


void QUITCommand::execute(const std::vector<std::string> &) {
    auto ftpPI = PI();
    ftpPI->quit = true;
    ftpPI->writeCtrl(SERVICE_CLOSE_CTRL_CONNECTION, "Goodbye");
}


/************************************************************
 * PORTCommand class definition
 ************************************************************/
const std::string PORTCommand::PROG = "PORT";


void PORTCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    auto &ftpDTP = ftpPI->DTP();

    if (ftpPI->EPSVexclusive) {
        ftpPI->writeCtrl(REQUESTED_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE, "Can only accept EPSV");
        return;
    }

    if (args.size() != 2) {
        ftpPI->writeCtrl(COMMAND_ARGS_NOT_RECOGNIZED, "Cannot recognize IP address and port number");
        return;
    }

    std::vector<std::string> PORTArgs = splitString(args[1], ",");
    if (PORTArgs.size() != 6) {
        ftpPI->writeCtrl(COMMAND_ARGS_NOT_RECOGNIZED, "Cannot recognize IP address and port number");
        return;
    }

    uint16_t leftPort  = 0;
    uint16_t rightPort = 0;
    for (std::size_t i = 0; i < PORTArgs.size(); ++i) {
        uint8_t num;
        if (toUnsignedInt<uint8_t>(PORTArgs[i], num) != 0) {
            ftpPI->writeCtrl(COMMAND_ARGS_NOT_RECOGNIZED, "Cannot recognize IP address and port number");
            return;
        }

        if (i == PORTArgs.size()-2)
            leftPort = num;
        else if (i == PORTArgs.size()-1)
            rightPort = num;
    }

    std::string receiverIPAddress = joinString(PORTArgs.begin(), PORTArgs.begin()+4, ".");
    uint16_t port = (leftPort << 8 & 0xFFFF) | rightPort;

    ftpDTP.setupActiveMode(receiverIPAddress, port, IPv4);
    ftpPI->writeCtrl(COMMAND_OK, "PORT Command successful. Consider using PASV");
}


/************************************************************
 * EPRTCommand class definition
 ************************************************************/
const std::string EPRTCommand::PROG = "EPRT";


void EPRTCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    auto &ftpDTP = ftpPI->DTP();

    if (ftpPI->EPSVexclusive) {
        ftpPI->writeCtrl(REQUESTED_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE, "Can only accept EPSV");
        return;
    }

    if (args.size() != 2 || args[1].back() != '|' || args[1].front() != '|') {
        ftpPI->writeCtrl(COMMAND_ARGS_NOT_RECOGNIZED, "EPRT command args not recognized");
        return;
    }

    std::size_t size = args[1].size();
    std::vector<std::string> EPRTargs = splitString(args[1].substr(1, size-2), "|");
    if (EPRTargs.size() != 3) {
        ftpPI->writeCtrl(COMMAND_ARGS_NOT_RECOGNIZED, "EPRT command args not recognized");
        return;
    }

    NetProtocol protocol = UNSPECIFIED;
    if (EPRTargs[0] == "1")
        protocol = IPv4;
    else if (EPRTargs[0] == "2")
        protocol = IPv6;
    else {
        ftpPI->writeCtrl(NETWORK_PROTOCOL_NOT_SUPPORTED, "Protocol not supported. use (1,2)");
        return;
    }

    std::string receiverIP = EPRTargs[1];
    uint16_t port;
    if (toUnsignedInt(EPRTargs[2], port) != 0)
        return;

    ftpDTP.setupActiveMode(receiverIP, port, protocol);
    ftpPI->writeCtrl(COMMAND_OK, "EPRT Command successful. Consider using EPSV");
}


/************************************************************
 * PASVCommand class definition
 ************************************************************/
const std::string PASVCommand::PROG = "PASV";


void PASVCommand::execute(const std::vector<std::string> &) {
    auto ftpPI = PI();
    auto &ftpDTP = ftpPI->DTP();

    if (ftpPI->EPSVexclusive) {
        ftpPI->writeCtrl(REQUESTED_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE, "Can only accept EPSV");
        return;
    }

    for (uint16_t port = FtpServerDTP::USABLE_PORT_MAX; port >= FtpServerDTP::USABLE_PORT_MIN; --port) {
        try {
            ftpDTP.setupPassiveMode(port, IPv4);
        } catch (const SocketException &e) {
            continue;
        }

        // add ip address to reply
        std::string reply = "Entering passive mode (";
        auto ipn = splitString(ftpPI->serverIPAddr(), ".");
        for (const auto &n : ipn)
            reply += n + ",";

        // add first 8 bits and second 8 bits of port number to cmd
        std::string p1 = std::to_string(port >> 8);
        std::string p2 = std::to_string(port & 0x00FF);
        reply += p1 + "," + p2 + ")";

        ftpPI->writeCtrl(ENTERING_PASSIVE_MODE, reply);
        break;
    }
}


/************************************************************
 * EPSVCommand class definition
 ************************************************************/
const std::string EPSVCommand::PROG = "EPSV";


void EPSVCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    auto &ftpDTP = ftpPI->DTP();

    if (args.size() != 2) {
        ftpPI->writeCtrl(COMMAND_ARGS_NOT_RECOGNIZED, "EPSV command args not recognized");
        return;
    }

    NetProtocol protocol;
    if (args[1] == "1")
        protocol = IPv4;
    else if (args[1] == "2")
        protocol = IPv6;
    else if (args[1] == "ALL") {
        ftpPI->EPSVexclusive = true;
        ftpPI->writeCtrl(ENTERING_EXTENDED_PASSIVE_MODE, "EPSV ALL ok");
        return;
    }
    else {
        ftpPI->writeCtrl(NETWORK_PROTOCOL_NOT_SUPPORTED, "Protocol not supported. use (1,2)");
        return;
    }

    for (uint16_t port = FtpServerDTP::USABLE_PORT_MAX; port >= FtpServerDTP::USABLE_PORT_MIN; --port) {
        try {
            ftpDTP.setupPassiveMode(port, protocol);
        } catch (const SocketException &e) {
            continue;
        }

        // add ip address to reply
        std::string reply = "Entering extended passive mode (|||" + std::to_string(port) + "|)";
        ftpPI->writeCtrl(ENTERING_EXTENDED_PASSIVE_MODE, reply);
        break;
    }
}


/************************************************************
 * LISTCommand class definition
 ************************************************************/
const std::string LISTCommand::PROG = "LIST";


void LISTCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    auto &ftpDTP = ftpPI->DTP();

    // get the path to list info
    std::string nativePath;
    if (args.size() == 2)
        nativePath = convertToNativePath(args[1]);
    else
        nativePath = convertToNativePath("");

    // get directory list
    std::stringstream directoryList;
    DIR *dir;
    struct stat fstat;
    if ((dir = opendir(nativePath.c_str())) != nullptr) {
        dirent *dirent;
        while ((dirent = readdir(dir)) != nullptr) {
            if (std::strcmp(dirent->d_name, ".") == 0 || std::strcmp(dirent->d_name, "..") == 0)
                continue;

            std::string path = nativePath + "/" + dirent->d_name;
            if (stat(path.c_str(), &fstat) == 0) {
                printFileStat(directoryList, fstat);

                directoryList << "\t";
                directoryList << dirent->d_name << "\r\n";
            }
        }
    }
    else if (stat(nativePath.c_str(), &fstat) == 0) {
        printFileStat(directoryList, fstat);

        directoryList << "\t";
        auto pos = nativePath.find_last_of("/");
        directoryList << nativePath.substr(pos+1) << "\r\n";
    }

    // check if data connection setup
    if (!ftpDTP.doesDataConnectSetup()) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CANNOT_OPEN_DATA_CONNECTION, "Failed open data connection");
        return;
    }

    // open data connection
    try {
        ftpDTP.openData();
    } catch (const SocketException &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CANNOT_OPEN_DATA_CONNECTION, "Failed open data connection");
        return;
    }

    // write to data connection
    ftpPI->writeCtrl(FILE_STATUS_OK_OPEN_DATA_CONNECTION, "Here come the directory listing");

    try {
        ftpDTP.writeData(directoryList);
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CLOSE_DATA_CONNECTION_REQUEST_FILE_ACTION_SUCCESS, "Directory listing sent OK");

    } catch (const SocketException &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CONNECTION_CLOSE_TRANSFER_ABORT, "Data connection close transfer abort");
    } catch (const std::exception &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(REQUESTED_ACTION_ABORTED_LOCAL_ERROR_PROCESSING, "Data connection close local error");
    }
}


/************************************************************
 * RETRCommand class definition
 ************************************************************/
const std::string RETRCommand::PROG = "RETR";


void RETRCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    auto &ftpDTP = ftpPI->DTP();

    // open file
    std::string nativePath;
    if (args.size() == 2)
        nativePath = convertToNativePath(args[1]);
    else
        nativePath = convertToNativePath("");

    std::ifstream file(nativePath, std::ios::in | std::ios::binary);
    if (!isRegularFile(nativePath)) {
        ftpPI->writeCtrl(REQUESTED_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE, "Failed to open file");
        return;
    }

    // check if data connection setup
    if (!ftpDTP.doesDataConnectSetup()) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CANNOT_OPEN_DATA_CONNECTION, "Failed open data connection");
        return;
    }

    // open data connection
    try {
        ftpDTP.openData();
    } catch (const SocketException &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CANNOT_OPEN_DATA_CONNECTION, "Failed open data connection");
        return;
    }

    // write to data connection
    ftpPI->writeCtrl(FILE_STATUS_OK_OPEN_DATA_CONNECTION, "Open data connection for file transfer");

    try {
        ftpDTP.writeData(file);
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CLOSE_DATA_CONNECTION_REQUEST_FILE_ACTION_SUCCESS, "Data connection close file sent OK");

    } catch (const SocketException &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CONNECTION_CLOSE_TRANSFER_ABORT, "Data connection close transfer abort");
    } catch (const std::exception &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(REQUESTED_ACTION_ABORTED_LOCAL_ERROR_PROCESSING, "Data connection close local error");
    }

}


/************************************************************
 * STORCommand class definition
 ************************************************************/
const std::string STORCommand::PROG = "STOR";


void STORCommand::execute(const std::vector<std::string> &args) {
    auto ftpPI = PI();
    auto &ftpDTP = ftpPI->DTP();

    // open file
    std::string nativePath;
    if (args.size() == 2)
        nativePath = convertToNativePath(args[1]);
    else
        nativePath = convertToNativePath("");

    std::ofstream file(nativePath);
    if (!file) {
        ftpPI->writeCtrl(REQUESTED_ACTION_ABORTED_LOCAL_ERROR_PROCESSING, "Failed to create file");
        return;
    }

    // check if data connection setup
    if (!ftpDTP.doesDataConnectSetup()) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CANNOT_OPEN_DATA_CONNECTION, "Failed open data connection");
        return;
    }

    // open data connection
    try {
        ftpDTP.openData();
    } catch (const SocketException &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CANNOT_OPEN_DATA_CONNECTION, "Failed open data connection");
        return;
    }

    // write to data connection
    ftpPI->writeCtrl(FILE_STATUS_OK_OPEN_DATA_CONNECTION, "Open data connection for file transfer");

    try {
        ftpDTP.readData(file);
        file.flush();
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CLOSE_DATA_CONNECTION_REQUEST_FILE_ACTION_SUCCESS, "Data connection close file sent OK");

    } catch (const SocketException &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(CONNECTION_CLOSE_TRANSFER_ABORT, "Data connection close transfer abort");
    } catch (const std::exception &) {
        ftpDTP.closeDataConnect();
        ftpPI->writeCtrl(REQUESTED_ACTION_ABORTED_LOCAL_ERROR_PROCESSING, "Data connection close local error");
    }

}

