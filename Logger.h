#ifndef WEBSERVER_LOGGER_H
#define WEBSERVER_LOGGER_H

#include "iostream"
#include "thread"
using std::string;
using std::cout;

struct Log{
    static void i(const string& s) { cout << "\x1B[34m" << std::this_thread::get_id() << ": " << s << "\033[0m\n";}
    static void e(const string& s) { cout << "\x1B[91m" << std::this_thread::get_id() << ": " << s << "\033[0m\n";}
};

#endif //WEBSERVER_LOGGER_H
