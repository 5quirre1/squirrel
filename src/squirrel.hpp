#ifndef SQUIRREL_H
#define SQUIRREL_H
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <thread>
#include <mutex>
#include <iostream>
#include <sstream>
#include <fstream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif
namespace Squirrel
{
    struct HttpRequest
    {
        std::string method;
        std::string path;
        std::string httpVersion;
        std::map<std::string, std::string> headers;
        std::string body;
        std::map<std::string, std::string> queryParams;
        HttpRequest() : method(""), path(""), httpVersion(""), body("") {}
    };
    struct HttpResponse
    {
        int statusCode;
        std::string statusMessage;
        std::map<std::string, std::string> headers;
        std::string body;
        HttpResponse() : statusCode(200), statusMessage("OK"), body("")
        {
            headers["Content-Type"] = "text/html";
        }
        void setStatus(int code, const std::string &message)
        {
            statusCode = code;
            statusMessage = message;
        }
        void setHeader(const std::string &key, const std::string &value)
        {
            headers[key] = value;
        }
        void send(const std::string &content)
        {
            body = content;
            headers["Content-Length"] = std::to_string(body.length());
        }
        void sendFile(const std::string &filePath)
        {
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                setStatus(404, "not found");
                send("<h1>404 not found</h1>");
                return;
            }
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<char> buffer(size);
            if (file.read(buffer.data(), size))
            {
                body.assign(buffer.begin(), buffer.end());
                headers["Content-Length"] = std::to_string(body.length());
                std::string contentType = "application/octet-stream";
                size_t dotPos = filePath.rfind('.');
                if (dotPos != std::string::npos)
                {
                    std::string ext = filePath.substr(dotPos + 1);
                    if (ext == "html" || ext == "htm")
                        contentType = "text/html";
                    else if (ext == "css")
                        contentType = "text/css";
                    else if (ext == "js")
                        contentType = "application/javascript";
                    else if (ext == "json")
                        contentType = "application/json";
                    else if (ext == "jpg" || ext == "jpeg")
                        contentType = "image/jpeg";
                    else if (ext == "png")
                        contentType = "image/png";
                    else if (ext == "gif")
                        contentType = "image/gif";
                    else if (ext == "svg")
                        contentType = "image/svg+xml";
                    else if (ext == "ico")
                        contentType = "image/x-icon";
                    else if (ext == "pdf")
                        contentType = "application/pdf";
                }
                headers["Content-Type"] = contentType;
            }
            else
            {
                setStatus(500, "internal server error");
                send("<h1>500 internal server error</h1>");
            }
            file.close();
        }
    };
    using Handler = std::function<void(const HttpRequest &, HttpResponse &)>;
    class Server
    {
    public:
        Server(int port);
        ~Server();
        void start();
        void stop();
        void get(const std::string &path, Handler handler);
        void setStaticDir(const std::string &dir);

    private:
        int m_port;
        int m_serverSocket;
        bool m_running;
        std::thread m_acceptThread;
        std::map<std::string, Handler> m_getRoutes;
        std::string m_staticDir;
        std::mutex m_handlersMutex;
        void initWinsock();
        void cleanupWinsock();
        void acceptConnections();
        void handleClient(int clientSocket);
        HttpRequest parseRequest(const std::string &requestString);
        void sendResponse(int clientSocket, const HttpResponse &response);
        void processRequest(const HttpRequest &request, HttpResponse &response);
    };
}
#endif
