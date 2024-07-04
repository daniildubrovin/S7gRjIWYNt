#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include "socket/socket.h"
#include <thread>
#include <sstream>
#include <iostream>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <random>
#include <filesystem>
#include "Logger.h"
#include <utility>
#include "Exceptions.h"
#include <mutex>

class Server {
public:
    enum Method {
        GET, POST
    };

    struct HttpCode {
        int code;
        std::string msg;
    };

    struct Cookie {
        std::string key;
        std::string value;
        std::string expires;
        std::string max_age;
        bool httpOnly = false;
        std::string domain;
        std::string path;

        Cookie(std::string key, std::string value): key(key), value(value) {};
        Cookie(std::string key, std::string value, std::string max_age): key(key), value(value), max_age(max_age) {};
        Cookie() = default;
        std::string toString() const;
    };

    struct File {
        std::string fileName;
        std::string type;
        std::string tmpName;
        int64_t size;
    };

    struct Request {
        Method method;
        std::string url;
        std::string version;
        std::string body;
        std::string boundary;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> cookies;
        std::map<std::string, std::string> params;
        std::map<std::string, File> files;
    };

    struct Response {
        std::string version = "HTTP/1.1";
        HttpCode code = {200, "OK"};
        std::map<std::string, std::string> headers;
        std::vector<Cookie> cookies;
        std::string body;

        Response(const std::string& body): body(body) { insertHeader("Content-Length", std::to_string(body.size()));}
        Response() = default;
        void insertHeader(const std::string& name, const std::string& value);
        static Response notFound(const std::string& errorMsg);
        static Response error(const std::string& errorMsg);
        std::string toString();
    };

    struct HttpMapping {
        std::string url;
        std::function<std::string(Request& request, Response& response)> response;
        Method method = Method::GET;
    };

    Server(int port = 8080);
    Server(const Server &s) = delete;
    void run();
    void addMapping(std::string url, std::function<std::string(Request&, Response&)> callback, Method method = Method::GET);
    static std::string generateSession();
    static bool moveFile(const File& file, const string& dir);
    static bool contains(std::string& str, const std::string& subStr);
    static int stringToHexInt(std::string str);
    static void replaceEncodedCharacters(std::string &str);


private:
    int m_port;
    Socket m_socket;
    std::vector<HttpMapping> responses;

    void handlingHttp(Socket socket, const std::vector<HttpMapping>& httpMapping);
    Request parseHttp(const std::string &s);
    void parseParams(const std::string &str, Request &request);
    void parseCookie(const std::string& cookieStr, std::map<std::string, std::string>& cookies);
    void parseMultiPartBody(std::istringstream& iss, Request &request);
    void parseTextPlain(std::string &str);
    void clearTmpDir(Request& request);
    string readDataFromSocket(Socket socket);
    HttpMapping findHttpMapping(Server::Request &request, const std::vector<HttpMapping> &httpMappings);
};