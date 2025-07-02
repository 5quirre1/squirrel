#include "squirrel.hpp"
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif
namespace Squirrel
{
    std::string trim(const std::string &str)
    {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (std::string::npos == first)
        {
            return str;
        }
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, (last - first + 1));
    }
    std::string urlDecode(const std::string &str)
    {
        std::string decoded;
        for (size_t i = 0; i < str.length(); ++i)
        {
            if (str[i] == '%')
            {
                if (i + 2 < str.length())
                {
                    std::string hex = str.substr(i + 1, 2);
                    char decodedChar = static_cast<char>(std::stoi(hex, nullptr, 16));
                    decoded += decodedChar;
                    i += 2;
                }
                else
                {
                    decoded += str[i];
                }
            }
            else if (str[i] == '+')
            {
                decoded += ' ';
            }
            else
            {
                decoded += str[i];
            }
        }
        return decoded;
    }
    Server::Server(int port) : m_port(port), m_serverSocket(-1), m_running(false)
    {
#ifdef _WIN32
        initWinsock();
#endif
    }
    Server::~Server()
    {
        stop();
#ifdef _WIN32
        cleanupWinsock();
#endif
    }
#ifdef _WIN32
    void Server::initWinsock()
    {
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0)
        {
            std::cerr << "WSAStartup failed: " << iResult << std::endl;
        }
    }
    void Server::cleanupWinsock()
    {
        WSACleanup();
    }
#endif
    void Server::start()
    {
        m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_serverSocket == -1)
        {
            std::cerr << "error creating socket" << std::endl;
            return;
        }
        int optval = 1;
        if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) < 0)
        {
            std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
        }
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(m_port);
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        if (bind(m_serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
        {
            std::cerr << "error binding socket to port " << m_port << std::endl;
#ifdef _WIN32
            closesocket(m_serverSocket);
#else
            close(m_serverSocket);
#endif
            return;
        }
        if (listen(m_serverSocket, 10) == -1)
        {
            std::cerr << "error listening on socket" << std::endl;
#ifdef _WIN32
            closesocket(m_serverSocket);
#else
            close(m_serverSocket);
#endif
            return;
        }
        m_running = true;
        std::cout << "squirrel server listening on port " << m_port << "..." << std::endl;
        m_acceptThread = std::thread(&Server::acceptConnections, this);
    }
    void Server::stop()
    {
        if (m_running)
        {
            m_running = false;
#ifdef _WIN32
            shutdown(m_serverSocket, SD_BOTH);
            closesocket(m_serverSocket);
#else
            shutdown(m_serverSocket, SHUT_RDWR);
            close(m_serverSocket);
#endif
            if (m_acceptThread.joinable())
            {
                m_acceptThread.join();
            }
            std::cout << "squirrel server stopped" << std::endl;
        }
    }
    void Server::get(const std::string &path, Handler handler)
    {
        std::lock_guard<std::mutex> lock(m_handlersMutex);
        m_getRoutes[path] = handler;
    }
    void Server::setStaticDir(const std::string &dir)
    {
        m_staticDir = dir;
        if (!m_staticDir.empty() && m_staticDir.back() != '/' && m_staticDir.back() != '\\')
        {
#ifdef _WIN32
            m_staticDir += '\\';
#else
            m_staticDir += '/';
#endif
        }
    }
    void Server::acceptConnections()
    {
        while (m_running)
        {
            sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            int clientSocket = accept(m_serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
            if (clientSocket == -1)
            {
                if (m_running)
                {
                    std::cerr << "error accepting connection" << std::endl;
                }
                continue;
            }
            char clientIp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIp, INET_ADDRSTRLEN);
            std::cout << "accepted connection from " << clientIp << ":" << ntohs(clientAddr.sin_port) << std::endl;
            std::thread clientHandler(&Server::handleClient, this, clientSocket);
            clientHandler.detach();
        }
    }
    void Server::handleClient(int clientSocket)
    {
        char buffer[4096];
        std::string requestString;
        ssize_t bytesRead;
        bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            requestString = buffer;
        }
        else if (bytesRead == 0)
        {
            std::cout << "client disconnected" << std::endl;
#ifdef _WIN32
            closesocket(clientSocket);
#else
            close(clientSocket);
#endif
            return;
        }
        else
        {
            std::cerr << "error reading from socket" << std::endl;
#ifdef _WIN32
            closesocket(clientSocket);
#else
            close(clientSocket);
#endif
            return;
        }
        HttpRequest request = parseRequest(requestString);
        HttpResponse response;
        processRequest(request, response);
        sendResponse(clientSocket, response);
#ifdef _WIN32
        closesocket(clientSocket);
#else
        close(clientSocket);
#endif
    }
    HttpRequest Server::parseRequest(const std::string &requestString)
    {
        HttpRequest request;
        std::istringstream iss(requestString);
        std::string line;
        std::getline(iss, line);
        std::istringstream lineStream(line);
        lineStream >> request.method >> request.path >> request.httpVersion;
        size_t queryPos = request.path.find('?');
        if (queryPos != std::string::npos)
        {
            std::string queryString = request.path.substr(queryPos + 1);
            request.path = request.path.substr(0, queryPos);
            std::istringstream queryStream(queryString);
            std::string param;
            while (std::getline(queryStream, param, '&'))
            {
                size_t eqPos = param.find('=');
                if (eqPos != std::string::npos)
                {
                    std::string key = urlDecode(param.substr(0, eqPos));
                    std::string value = urlDecode(param.substr(eqPos + 1));
                    request.queryParams[key] = value;
                }
            }
        }
        while (std::getline(iss, line) && line != "\r")
        {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos)
            {
                std::string key = trim(line.substr(0, colonPos));
                std::string value = trim(line.substr(colonPos + 1));
                request.headers[key] = value;
            }
        }
        std::string bodyContent;
        std::string remainingLine;
        while (std::getline(iss, remainingLine))
        {
            bodyContent += remainingLine + "\n";
        }
        request.body = trim(bodyContent);
        return request;
    }
    void Server::sendResponse(int clientSocket, const HttpResponse &response)
    {
        std::ostringstream oss;
        oss << "HTTP/1.1" << " " << response.statusCode << " " << response.statusMessage << "\r\n";
        for (const auto &header : response.headers)
        {
            oss << header.first << ": " << header.second << "\r\n";
        }
        oss << "\r\n";
        oss << response.body;
        std::string responseString = oss.str();
        send(clientSocket, responseString.c_str(), responseString.length(), 0);
    }
    void Server::processRequest(const HttpRequest &request, HttpResponse &response)
    {
        if (request.method == "GET")
        {
            std::lock_guard<std::mutex> lock(m_handlersMutex);
            auto it = m_getRoutes.find(request.path);
            if (it != m_getRoutes.end())
            {
                it->second(request, response);
                return;
            }
            if (!m_staticDir.empty())
            {
                std::string filePath = m_staticDir;
                if (request.path == "/")
                {
                    filePath += "index.html";
                }
                else
                {
                    filePath += request.path.substr(1);
                }
                std::ifstream file(filePath, std::ios::binary);
                if (file.is_open())
                {
                    response.sendFile(filePath);
                    return;
                }
            }
            response.setStatus(404, "not found");
            response.send("<h1>404 not found</h1><p>the requested URL " + request.path + " was not found on this server</p>");
        }
        else
        {
            response.setStatus(405, "method not allowed");
            response.setHeader("Allow", "GET");
            response.send("<h1>405 method not allowed</h1><p>only get requests are supported by this server</p>");
        }
    }
}
