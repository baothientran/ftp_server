#ifndef UTILITY_H
#define UTILITY_H


#include <string>
#include <vector>
#include <limits>
#include <iostream>


bool isRegularFile(const std::string &file);


bool isDiretory(const std::string &directory);


std::string normalizePath(const std::string &path);


void printFileStat(std::ostream &stream, const struct stat &fstat);


std::ostream &logDateTime(std::ostream &stream);


std::vector<std::string> splitString(const std::string &str, const std::string &token);


template<typename Iter>
std::string joinString(Iter begin, Iter end, const std::string &token) {
    std::string res;
    std::string sep = "";
    for (auto it = begin; it != end; ++it) {
        res += sep + *it;
        sep = token;
    }

    return res;
}


template<typename UnsigedInt>
int toUnsignedInt(const std::string &str, UnsigedInt &res) {
    uint64_t num = 0;
    for (auto c = str.begin(); c != str.end(); ++c) {

        if (*c >= '0' && *c <= '9') {
            UnsigedInt digit = *c - '0';
            if (num <= std::numeric_limits<UnsigedInt>::max() - digit)
                num = num * 10 + digit;
            else
                return 1;
        }
        else
            return -1;
    }

    res = num;
    return 0;
}

#endif // UTILITY_H
