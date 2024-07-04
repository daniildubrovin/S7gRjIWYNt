//
// Created by user on 12.01.24.
//

#ifndef WEBSERVER_EXCEPTIONS_H
#define WEBSERVER_EXCEPTIONS_H

#include "exception"
#include "string"

class NotFoundException{
public:
    std::string msg;
    NotFoundException(std::string msg) : msg(msg) {}
};

class SocketException{
public:
    std::string msg;
    SocketException(std::string msg) : msg(msg) {}
};


#endif //WEBSERVER_EXCEPTIONS_H
