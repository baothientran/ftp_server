#include <sys/types.h>
#include <sys/stat.h>
#include <iomanip>
#include <stack>
#include "Utility.h"


std::vector<std::string> splitString(const std::string &str, const std::string &token) {
    std::vector<std::string> res;
    std::string::size_type prev = 0;
    std::string::size_type pos = str.find(token, prev);
    while (pos != std::string::npos) {
        res.push_back(str.substr(prev, pos - prev));
        prev = pos+1;
        pos  = str.find(token, prev);
    }
    res.push_back(str.substr(prev));

    return res;
}


bool isRegularFile(const std::string &file) {
    struct stat fstat;
    return stat(file.c_str(), &fstat) == 0 && S_ISREG(fstat.st_mode);
}


std::ostream &logDateTime(std::ostream &stream) {
    std::time_t now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    return stream << std::put_time(&tm, "%c %Z") << ": ";
}


bool isDiretory(const std::string &directory) {
    struct stat fstat;
    return stat(directory.c_str(), &fstat) == 0 && S_ISDIR(fstat.st_mode);
}


std::string normalizePath(const std::string &path) {
    std::stack<std::string> curr;

    std::size_t begin = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (path[i] == '/' || i == path.size()) {
            auto p = path.substr(begin, i-begin);
            if (p == ".." && !curr.empty())
                curr.pop();
            else if (p != "." && p != ".." && !p.empty())
                curr.push(p);

            begin = i+1;
        }
    }

    std::string res = "";
    std::string sep = "";
    while (!curr.empty()) {
        res = curr.top() + sep + res;
        sep = "/";
        curr.pop();
    }

    return res;
}


void printFileStat(std::ostream &stream, const struct stat &fstat) {
    static const int MAX_BUF = 200;
    stream << (S_ISDIR(fstat.st_mode)  ? "d" : "-");
    stream << (fstat.st_mode & S_IRUSR ? "r" : "-");
    stream << (fstat.st_mode & S_IWUSR ? "w" : "-");
    stream << (fstat.st_mode & S_IXUSR ? "x" : "-");
    stream << (fstat.st_mode & S_IRGRP ? "r" : "-");
    stream << (fstat.st_mode & S_IWGRP ? "w" : "-");
    stream << (fstat.st_mode & S_IXGRP ? "x" : "-");
    stream << (fstat.st_mode & S_IROTH ? "r" : "-");
    stream << (fstat.st_mode & S_IWOTH ? "w" : "-");
    stream << (fstat.st_mode & S_IXOTH ? "x" : "-");

    stream << "\t";
    stream << fstat.st_nlink;

    stream << "\t";
    stream << fstat.st_size;

    stream << "\t";
    char date[MAX_BUF];
    strftime(date, 20, "%b %d %H:%M", localtime(&(fstat.st_ctime)));
    stream << date;
}
