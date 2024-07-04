#include "server.h"
#include <cpp_redis/cpp_redis>

using Request = Server::Request;
using Response = Server::Response;
using std::string;

void writeSessionToRedis(const string& session,const string& login, int seconds);
bool isSessionInRedis(const string& cookieSession);
string getLoginFromRedis(const string& cookieSession);
string auth(Request& request, Response& response, int seconds);
string regist(Request& request, Response& response);
string getAccountHtml(const string& login, const string& files);

std::map<string,string> readUsers();
bool authUser(const string& login, const string& password);
bool isUserLogin(const string& login);
void addUser(const string& login, const string& password);
void deleteUser(const string& login);

string getAllFiles(const string& login);
string errorMsg(const string& msg, Response& response);
void deleteFileInfo(const string& file, const string& login);
void addFileInfo(const Server::File& file, const string& login);

bool isAdmin(string login);
void deleteAdmin(string login);
void setAdmin(string login);
string getAdminHtml(const string& login);

bool checkSession(string login);

std::mutex mut_users, mut_admins, mut_files;

int main() 
{
    Server server(8079);

    server.addMapping(
       "/authorization",
       [](Request& request, Response& response) { return auth(request, response, 60);},
       Server::Method::POST);

    server.addMapping(
        "/registration",
        [](Request& request, Response& response) { return regist(request, response);},
        Server::Method::POST);

    server.addMapping(
            "/account.html",
            [](Request& q, Response& s) {
               if(q.cookies.count("session") && isSessionInRedis(q.cookies["session"])) {
                   return getAccountHtml(getLoginFromRedis(q.cookies["session"]), getAllFiles(getLoginFromRedis(q.cookies["session"])));
               }
               return errorMsg("not auth", s);
    });

    server.addMapping(
            "/allfiles",
            [](Request& q, Response& s) {
                s.code = {302, "Found"};
                s.insertHeader("Location", "/account.html");
                string login = getLoginFromRedis(q.cookies["session"]);
                return string();
    });

    server.addMapping(
            "/loadfile",
            [](Request& q, Response& s) {
                string login = getLoginFromRedis(q.cookies["session"]);

                for(const auto& file: q.files) {
                    {
                        std::scoped_lock lock(mut_files);
                        Server::moveFile(file.second, "files/" + login);
                    }

                    addFileInfo(file.second, login);
                }
                return getAccountHtml(login, getAllFiles(login));
            },Server::Method::POST);

    server.addMapping(
            "/delete",
            [](Request& q, Response& s) {
                    string login = getLoginFromRedis(q.cookies["session"]);
                    string param = q.params.begin()->first;
                    Server::replaceEncodedCharacters(param);
                    string m = "files/" + login + "/" + param;
                    {
                        std::scoped_lock lock(mut_files);
                        if(remove((m).c_str()) != 0) throw std::runtime_error(strerror(errno));
                    }
                    deleteFileInfo(param, login);
                    return getAccountHtml(login, getAllFiles(login));
            }, Server::Method::POST);

    server.addMapping(
            "/download",
            [](Request& q, Response& s) {
                string login = getLoginFromRedis(q.cookies["session"]);
                string fileName = q.params.begin()->first;
                Server::replaceEncodedCharacters(fileName);
                string ContentType, fileSize;

                std::scoped_lock lock(mut_files);
                std::ifstream info("files/" + login + ".txt");
                while(!info.eof()) {
                    string line; getline(info, line);
                    if(Server::contains(line, fileName)) {
                        fileSize = line.substr(fileName.size() + 1);
                        fileSize = fileSize.substr(0, fileSize.find(' '));
                        ContentType = line.substr(fileName.size() + fileSize.size() + 2);
                        break;
                    }
                }

                s.insertHeader("Content-Type", ContentType);
                s.insertHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");

                std::ifstream file("files/" + login + "/" + fileName, std::ios::binary);
                uint64_t sz = std::filesystem::file_size("files/" + login + "/" + fileName);
                std::string result(sz, '\0');
                file.read(result.data(), sz);

                return result;
            }, Server::Method::POST);

    server.addMapping(
        "/admin",
        [](Request& q, Response& s) {
            if(!isSessionInRedis(q.cookies["session"])) {
                s.cookies.emplace_back("session", "");
                throw std::runtime_error("not auth");
            }
            string login = getLoginFromRedis(q.cookies["session"]);
            if(!isAdmin(login)) throw std::runtime_error(login + " is not admin");
            return getAdminHtml(login);
        },
        Server::Method::GET
    );

    server.addMapping(
            "/delete_user",
            [](Request& q, Response& s) {
                s.code = {302, "Found"};
                s.insertHeader("Location", "/admin");
                string login = getLoginFromRedis(q.cookies["session"]);
                string user = q.params.begin()->first;
                Server::replaceEncodedCharacters(user);
                deleteUser(user);
                deleteAdmin(user);
                std::scoped_lock lock(mut_files);
                std::filesystem::remove("files/" + user + ".txt");
                std::filesystem::remove_all("files/" + user);
                return "";
            },
            Server::Method::POST
    );

    server.addMapping(
            "/set_admin",
            [](Request& q, Response& s) {
                s.code = {302, "Found"};
                s.insertHeader("Location", "/admin");
                string login = getLoginFromRedis(q.cookies["session"]);
                string user = q.params.begin()->first;
                Server::replaceEncodedCharacters(user);
                setAdmin(user);
                return "";
            },
            Server::Method::POST
    );

    server.addMapping(
          "/registration.html",
          [](Request& q, Response& s) { return "registration.html"; });

    server.addMapping(
          "/",
          [](Request& q, Response& s) { return "index.html"; });

    server.addMapping(
          "/index.html",
          [](Request& q, Response& s) { return "index.html"; });

    server.run();
    return 0;
}

void writeSessionToRedis(const string& session, const string& login, int seconds){
    cpp_redis::client client;
    client.connect("localhost", 6379, [](const std::string& host, std::size_t port, cpp_redis::client::connect_state status) {
        if (status == cpp_redis::client::connect_state::dropped) throw std::runtime_error("client disconnected from " + host + ":" + std::to_string(port));
    });

    client.setex(session, seconds, login);
    client.sync_commit();
}
bool isSessionInRedis(const string& cookieSession){
    cpp_redis::client client;
    client.connect("localhost", 6379, [](const std::string& host, std::size_t port, cpp_redis::client::connect_state status) {
        if (status == cpp_redis::client::connect_state::dropped) throw std::runtime_error("client disconnected from " + host + ":" + std::to_string(port));
    });

    auto session = client.get(cookieSession);
    client.sync_commit();
    return !session.get().is_null();
}

string getLoginFromRedis(const string& cookieSession){
    cpp_redis::client client;
    client.connect("localhost", 6379, [](const std::string& host, std::size_t port, cpp_redis::client::connect_state status) {
        if (status == cpp_redis::client::connect_state::dropped) throw std::runtime_error("client disconnected from " + host + ":" + std::to_string(port));
    });

    auto session = client.get(cookieSession);
    client.sync_commit();

    return session.get().as_string();
}

string auth(Request &request, Response &response, int seconds) {
    //if(request.headers["Content-Type"] != "application/x-www-form-urlencoded") throw std::runtime_error("mime type must be application/x-www-form-urlencoded");

    Log::i("файл: " + request.files.begin()->first);

    response.code = {302, "Found"};

    if(authUser(request.params["login"], request.params["password"])) {
        response.insertHeader("Location", "/account.html");

        string session = Server::generateSession() + request.params["login"];
        writeSessionToRedis(session,request.params["login"], seconds);

        response.cookies.emplace_back("session", session, std::to_string(seconds));
        response.cookies.emplace_back("authorization", "", "-1");
    }
    else {
        response.insertHeader("Location", "/");
        response.cookies.emplace_back("authorization", "no", "60");
    }

    return "";
}

string regist(Request& request, Response& response){
    if(request.headers["Content-Type"] != "application/x-www-form-urlencoded") throw std::runtime_error("mime type must be application/x-www-form-urlencoded");
    response.code = {302, "Found"};

    try {
        if(request.params["login"].find(' ') != std::string::npos || request.params["password"].find(' ') != std::string::npos) {
            throw std::runtime_error("login or password contains space(' ')");
        }

        addUser(request.params["login"], request.params["password"]);

        response.cookies.emplace_back("registration", "", "-1");
        response.insertHeader("Location", "/");
    }
    catch (std::exception& e) {
        response.cookies.emplace_back("registration", e.what(), "60");
        response.insertHeader("Location", "/registration.html");
    }

    return "";
}

std::map<string,string> readUsers(){
    std::scoped_lock lock(mut_admins);
    std::fstream file("users.txt");
    std::map<string,string> users;
    string line, login, password;
    while(getline(file, line)) {
        std::istringstream s(line); s >> login; s >> password;
        users.insert(std::make_pair(login, password));
    }
    return users;
}

bool authUser(const string& login, const string& password){
    std::map<string,string> users = readUsers();
    for(const auto& user: users) {
        if(user.first == login && user.second == password) return true;
    }
    return false;
}

bool isUserLogin(const string& login){
    std::map<string,string> users = readUsers();
    for(const auto& user: users) if(user.first == login) return true;
    return false;
}

void addUser(const string& login, const string& password) {
    if(isUserLogin(login)) throw std::runtime_error("user: " + login + " exists");

    std::scoped_lock lock(mut_users);
    std::fstream file("users.txt", std::ios::app);
    file << login << " " << password << "\n";

    std::filesystem::create_directories("files/" + login);
}

void deleteUser(const string& login) {
    std::map<string,string> users = readUsers();
    std::scoped_lock lock(mut_users);
    for(const auto& user: users) {
        if(user.first == login) {  users.erase(user.first); break; }
    }

    std::ofstream file("users.txt");
    for(const auto& user: users) file << user.first << " " << user.second << "\n";
}

string getAllFiles(const string& login){
    std::scoped_lock lock(mut_files);
    string path = "files/" + login;
    string res;

    if (!std::filesystem::is_directory(path)) std::filesystem::create_directory(path);

    for (const auto & entry : std::filesystem::directory_iterator(path)){
        string file = entry.path().string();
        file = file.substr(file.find_last_of('/') + 1);
        res +=
            "<div>\n"
            "<p class=\"ib\">" + file + "</p>\n"
            "<form class=\"ib\" action=\"/delete\" method=\"post\"> <input type=\"submit\" name=\"" + file + "\" value=\"delete\"> </form>\n"
            "<form class=\"ib\" action=\"/download\" method=\"post\"> <input type=\"submit\" name=\"" + file + "\" value=\"download\"> </form>"
            "</div>";
    }
    return res;
}

string errorMsg(const string& msg, Response& response){
    response.code = {500, "Internal Server Error"};
    return "error: " + msg;
}

void deleteFileInfo(const string& file, const string& login) {
    std::scoped_lock lock(mut_files);
    std::ifstream info("files/" + login + ".txt");
    std::vector<string> info_files;
    string line, name;
    while(getline(info, line)) {
        std::istringstream s(line); s >> name;
        if(name != file) info_files.push_back(line);
    }
    std::ofstream info2("files/" + login + ".txt");
    for(string& info_file: info_files) info2 << info_file << "\n";
}

void addFileInfo(const Server::File& file, const string& login){
    deleteFileInfo(file.fileName, login);
    std::scoped_lock lock(mut_files);
    std::ofstream info("files/" + login  + ".txt", std::ios::app);
    info << file.fileName << " " << file.size << " " << file.type << std::endl;
}

bool isAdmin(string login){
    std::scoped_lock lock(mut_admins);
    std::ifstream info("admins.txt");
    string line;
    while(getline(info, line)) {
        if(login == line) return true;
    }
    return false;
}

void deleteAdmin(string login){
    std::scoped_lock lock(mut_admins);
    std::ifstream info("admins.txt");
    string line, l;
    std::vector<string> lines;
    while(getline(info, line)) {
        if(line != login) lines.push_back(line);
    }
    std::ofstream info2("admins.txt");
    for(string& l2: lines) info2 << l2 << "\n";
}

void setAdmin(string login){
    std::scoped_lock lock(mut_admins);
    std::ofstream info2("admins.txt", std::ios::app);
    info2 << login << "\n";
}

string getAdminHtml(const string& login){
    std::map<string,string> users = readUsers();
    string users_str;
    for(const auto& user: users){
        users_str +=
                "<div>\n"
                "            <p class=\"ib\">" + user.first +"</p>\n"
                "            <form class=\"ib delete\" action=\"/delete_user\" method=\"post\"> <input type=\"submit\" name=\"" + user.first +"\" value=\"delete\">  </form>\n" +
                "</div>\n";
    }

    string html =
            "<!DOCTYPE html>\n"
            "<html lang=\"en\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <title>admins</title>\n"
            "    <style>\n"
            "        .ib {display: inline-block}\n"
            "        .login {margin-left: 1%; color: red}\n"
            "        input { font-size: 15pt; }\n"
            "        .delete {margin-left: 20pt;}\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div>\n"
            "        <h1 class=\"ib\"> admin: </h1>\n"
            "        <h1 class=\"ib login\">" + login + "</h1>\n"
            "    </div>\n"
            "    <div style=\"float: left; margin-left: 40%; margin-top: 20pt\">\n"
            "            <h1>List of users:</h1>\n"
            "            <form action=\"/admin\" method=\"get\"> <input style=\"margin-bottom: 35pt\" type=\"submit\" value=\"SHOW ALL USERS\"> </form>\n"
                                                    + users_str +
            "    </div>\n"
            "</body>\n"
            "</html>";

    return html;
}

string getAccountHtml(const string& login, const string &files){
    string html =
            "<!DOCTYPE html>\n"
            "<html lang=\"en\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <title>DubrovinBox</title>\n"
            "    <style>\n"
            "        .ib {display: inline-block}\n"
            "        .login {margin-left: 1%; color: green}\n"
            "        input { font-size: 15pt; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div>\n"
            "        <h1 class=\"ib\"> user: </h1>\n"
            "        <h1 class=\"ib login\">" + login + "</h1>\n"
            + (isAdmin(login) ? "<form class=\"ib\" style=\"margin-left: 20pt\" action=\"/admin\" method=\"get\"> <input type=\"submit\" value=\"admin panel\"> </form>" : "") +
            "    </div>\n"
            "    <div>\n"
            "        <div style=\"float: left; margin-left: 5%; margin-top: 20pt\">\n"
            "            <h1>List of files:</h1>\n"
            "            <form action=\"/allfiles\" method=\"get\"> <input type=\"submit\" value=\"SHOW ALL FILES\"> </form>\n"
                                   + files +
            "       </div>\n"
            " <div style=\"float: left; margin-left: 20%; margin-top: 20pt\">\n"
            "            <h1>Load files:</h1>\n"
            "            <form  action=\"loadfile\" method=\"post\" enctype=\"multipart/form-data\" >\n"
            "                <input type=\"file\" name=\"file\" multiple >\n"
            "                <input type=\"submit\" value=\"LOAD\">\n"
            "            </form>\n"
            "        </div>\n"

            "    </div>\n"
            "</body>\n"
            "</html>";

    return html;
}