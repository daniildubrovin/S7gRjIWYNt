#include "server.h"

Server::Server(int port) : m_port(port)
{
    m_socket.create();
    m_socket.bind(port);
    m_socket.listen();
    std::cout << "start server" << std::endl;
}

void Server::run()
{
    while (true)
    {
        try {
            Socket newSocket = m_socket.accept();
            std::thread t([&](Socket newSocket) { handlingHttp(newSocket, responses); }, newSocket);
            t.detach();
        }
        catch (std::exception& e) {std::cout << e.what() << std::endl;}
    }
}

void Server::handlingHttp(Socket socket, const std::vector<HttpMapping> &httpMappings) {
    try {
        Request request = parseHttp(readDataFromSocket(socket));
        Response response;

        HttpMapping httpMapping = findHttpMapping(request, httpMappings);
        Log::i("mapping: " + httpMapping.url);

        std::string data = httpMapping.response(request, response);
        if(!data.empty()) {
            if(data.size() < 300) {
                std::ifstream file("www/" + data);
                if(file.is_open()) data = std::string((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
            }

            response.insertHeader("Content-Length", std::to_string(data.size()));
        }

        response.body = data;
        clearTmpDir();
        socket << response.toString();
    }
    catch (NotFoundException& e) {
        Log::e(e.msg);
        socket << Response::notFound(e.msg).toString();
    }
    catch (std::exception& e) {
        Log::e(e.what());
        socket << Response::error(e.what()).toString();
    }

    socket.close();
}

Server::Request Server::parseHttp(const std::string &s)
{
    Request request;
    std::istringstream iss(s);
    std::string method, url;

    iss >> method; method == "GET" ? request.method = GET : request.method = POST;
    iss >> url;

    if(url.find('?') != std::string::npos)
    {
        request.url = url.substr(0, url.find_first_of('?'));
        parseParams(url.substr(url.find_first_of('?') + 1), request);
    }
    else request.url = url;

    iss >> request.version;

    std::string line;
    // for /r/n
    std::getline(iss, line);

    std::getline(iss, line, '\r'); iss.get();
    while(!line.empty())
    {
        request.headers.insert(std::make_pair(line.substr(0, line.find_first_of(':')),line.substr(line.find_first_of(':') + 2)));
        std::getline(iss, line, '\r'); iss.get();
    }

    if(request.headers.count("Cookie")) {
        parseCookie(request.headers["Cookie"], request.cookies);
    }

    if(request.headers.count("Content-Type")) {
        if(contains(request.headers["Content-Type"], "multipart/form-data")) {
            request.boundary = request.headers["Content-Type"].substr(request.headers["Content-Type"].find('=') + 1);
            parseMultiPartBody(iss, request);
        }
        else if(contains(request.headers["Content-Type"], "application/x-www-form-urlencoded")){
            while(!iss.eof())
            {
                std::getline(iss, line);
                request.body += line;
            }
            parseParams(request.body, request);
        }
        else if(contains(request.headers["Content-Type"], "text/plain")){
            while(!iss.eof())
            {
                std::getline(iss, line);
                request.body += line;
            }
            parseTextPlain(request.body);
        }
    }

    return request;
}

void Server::parseMultiPartBody(std::istringstream &iss, Server::Request &request) {
    std::string line;
    std::getline(iss, line, '\r'); iss.get(); //first boundary

    while(!iss.eof()){
        string contentDisposition, contentType, fileType, fileName;
        std::getline(iss, line, '\r'); iss.get(); // content-Disposition
        if(contains(line, "Content-Disposition")) {
            contentDisposition = line.substr(line.find_first_of(':') + 2);
            if(contains(contentDisposition, "filename")) {
                fileName = contentDisposition.substr(contentDisposition.find("filename=") + 9);
                fileName = fileName.substr(1, fileName.size() - 2);
                if(fileName.find_last_of('.') != string::npos) {
                    fileType = fileName.substr(fileName.find_last_of('.'));
                    fileName = fileName.substr(0, fileName.find_last_of('.'));
                }
            }
            else {
                fileName = generateSession();
                fileType = ".txt";
            }
        }

        std::getline(iss, line, '\r'); iss.get(); //Content-Type
        if(!line.empty()) {
            if(contains(line, "Content-Type")) {
                contentType = line.substr(line.find_first_of(':') + 2);
                cout << contentType << endl;
            }
            std::getline(iss, line, '\r'); iss.get();
        }

        string tmpName = generateSession();
        std::filesystem::create_directory("tmp");
        std::ofstream file("tmp/" + tmpName, std::ios::binary);
        if(!file.is_open()) throw std::runtime_error("the file: " + string("tmp/") + tmpName + " cannot be created");

        std::ostringstream ts;
        while(!iss.eof()) {
            std::getline(iss, line, '\r'); iss.get();

            if(contains(line, request.boundary)) {
                if (contains(line, request.boundary + "--")) std::getline(iss, line);
                break;
            }

            file << ts.str();
            file << line;

            iss.unget(); iss.unget();
            ts = std::ostringstream();
            ts << (char)iss.get() << (char)iss.get();
        }

        file.close();

        std::fstream file2("tmp/" + tmpName, std::ios::binary | std::ios::ate);
        File f {fileName + fileType, contentType, tmpName, (int64_t)file2.tellg()};
        files.insert(std::make_pair(fileName + fileType, f));
    }
}

void Server::parseParams(const std::string &str, Request &request)
{
    char c;
    int i = -1;
    std::string param, value;
    enum class StateParam {param, value, error};
    StateParam state = StateParam::param;

    while (true) {
        if((c = str[++i]) == '\0') {
            replaceEncodedCharacters(value);
            request.params.insert(std::pair<std::string, std::string>(param, value));
            break;
        }
        else if (state == StateParam::param) {
            if (c == '=') state = StateParam::value;
            else param += c;
        }
        else if (state == StateParam::value) {
            if (c == '&') {
                replaceEncodedCharacters(value);
                request.params.insert(std::pair<std::string, std::string>(param, value));
                param = ""; value = "";
                state = StateParam::param;
            }
            else value += c;
        }
    }
}

int Server::stringToHexInt(std::string str)
{
    int n = 0, p = str.length() - 1;
    char hexSymbols[6] = {'A', 'B', 'C', 'D', 'E', 'F'};
    for (size_t j = 0; j < str.length(); j++)
    {
        int n2 = str[j] - '0';
        for (size_t i = 0; i < 6; i++)
        {
            if(str[j] == hexSymbols[i]) n2 = 10 + i;
        }
        n += n2 * std::pow(16,p);
        p--;
    }
    return n;
}

void Server::replaceEncodedCharacters(std::string& str)
{
    std::string type = "n", number;
    for (size_t i = 0; i < str.length(); i++)
    {
        if(str[i] == '+') str.replace(i,1," ");
        if(str[i] == '%') type = "%";
        else if(type == "%"){
            if(isdigit(str[i]) || str[i] >= 65 && str[i] <= 70) number += str[i];
            else throw std::runtime_error("notFound parsing http body parameters: %" + number);

            if(number.length() == 2) {
                type = "n";
                std::string rs; rs += (char)stringToHexInt(number);
                str.replace(i - 2, 3, rs);
                i-=2;
                number = "";
            }
        }
    }
}

std::string Server::generateSession() {
    const std::string CHARS = "0123456789ABCDEFGHIJKLMNOPORSTUVWXYZabcdefghigklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0,CHARS.length()-1);
    std::string UUID;
    for (int i = 0; i < 32; ++i) UUID += CHARS[dist(gen)];
    return UUID;
}

void Server::parseCookie(const std::string& cookieStr, std::map<std::string, std::string> &cookies) {
    std::istringstream sstream(cookieStr);
    std::string token;
    while(getline(sstream, token, ';')){
        cookies.insert(std::make_pair(token.substr(0, token.find_first_of('=')), token.substr(token.find_first_of('=')+1)));
        if(sstream.get() != ' ') sstream.unget();
    }
}

std::string Server::Response::toString() {
    std::string res = version + " " + std::to_string(code.code) + " " + code.msg + "\r\n";
    for(const auto& header: headers) res += header.first + ": " + header.second + "\r\n";
    for(Cookie& cookie: cookies) res += "Set-Cookie: " + cookie.toString() + "\r\n";
    res += "\r\n" + body;
    return res;
}

void Server::Response::insertHeader(const std::string& name, const std::string& value) {
    headers.insert(std::make_pair(name, value));
}

Server::Response Server::Response::notFound(const std::string& errorMsg) {
    Server::Response response;
    response.code = {404, "Not Found"};
    response.body = "<!DOCTYPE html> <p>" +  errorMsg + "</p>";
    return response;
}

Server::Response Server::Response::error(const std::string &errorMsg) {
    Server::Response response;
    response.code = {500, "Internal Server Error"};
    response.body = "<!DOCTYPE html> <p>" +  errorMsg + "</p>";
    return response;
}

std::string Server::Cookie::toString() const {
    std::string res = key + "=" + value + "; ";
    if(!expires.empty()) res += "Expires=" + expires + "; ";
    if(!max_age.empty()) res += "Max-Age=" + max_age + "; ";
    if(httpOnly) res += "HttpOnly; ";
    if(!domain.empty()) res += "domain=" + domain + "; ";
    if(!path.empty()) res += "path=" + path + "; ";
    return res;
}

void Server::addMapping(const std::string url, std::function<std::string(Request& request, Response& response)> callback, Method method) {
    responses.push_back({url, std::move(callback), method});
}

bool Server::contains(string &str, const string& subStr) {
    return str.find(subStr) != std::string::npos;
}

bool Server::moveFile(const Server::File &file, const string &dir) {
    std::ifstream tmp("tmp/" + file.tmpName);
    if(!tmp.is_open()) return false;
    std::ofstream f(dir + "/" + file.fileName);
    f << tmp.rdbuf();
    return true;
}

void Server::clearTmpDir() {
    for (const auto & entry : std::filesystem::directory_iterator("tmp/")) {
        string file = entry.path().string();
        remove(file.c_str());
    }
    files.clear();
}

string Server::readDataFromSocket(Socket socket) {
    std::string all_data, local_data;
    int64_t content_len = -1;
    int64_t count = 0;
    bool startCount = false;
    string s = "Content-Length: ";

    Log::i("start receive data from socket: ");
    while(true){
        socket >> local_data; all_data += local_data;
        if(startCount) count += local_data.size();

        if(content_len == -1) {
            size_t ls = all_data.find(s);
            if(ls != string::npos) {
                size_t le = all_data.find('\r', ls + s.size());
                if(le != string::npos) {
                    content_len = stoi(all_data.substr(ls + s.size(), le - (ls + s.size())));
                    Log::i("size of data in body is: " + std::to_string(content_len));
                }
            }
        }

        if(count == 0) {
            int64_t c = all_data.find("\r\n\r\n");
            if(c != string::npos) {
                if(content_len != -1) {
                    count = all_data.size() - c - 4;
                    startCount = true;
                }
                else count = -1;
            }
        }
        if(count == content_len || count == -1) break;
        if(count > content_len) throw std::runtime_error("number bytes of data is bigger Content-Length");
    }

    Log::i("receive total: " + std::to_string(all_data.size()));

    return all_data;
}

void Server::parseTextPlain(string &str) {
    size_t i;
    while((i = str.find(' ')) != string::npos) {
        str.replace(i,1,"+");
    }
}

Server::HttpMapping Server::findHttpMapping(Server::Request &request, const std::vector<HttpMapping> &httpMapping) {
    auto it = std::find_if(httpMapping.begin(), httpMapping.end(), [&](const HttpMapping& mapping) {
        return mapping.method == request.method && mapping.url == request.url;
    });

    if(it != httpMapping.end()) return *it;
    throw NotFoundException("[" + request.url + "]" + " dont mapping");
}
