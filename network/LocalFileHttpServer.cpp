#include "LocalFileHttpServer.h"
#include "Config.h"
#include "Debug.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <File.h>
#include <Url.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>

LocalFileHttpServer::LocalFileHttpServer()
    : fServerThread(-1), fRunning(false), fServerSocket(-1), fPort(0)
{
    /// Try to get a non-localhost IP address to construct public URLs
    fMyIpAddress = "127.0.0.1";
    
    uint32 cookie = 0;
    BNetworkInterface interface;
    while (BNetworkRoster::Default().GetNextInterface(&cookie, interface) == B_OK) {
        if ((interface.Flags() & IFF_LOOPBACK) != 0) continue;
        if ((interface.Flags() & IFF_UP) == 0) continue;
        
        int32 addrIdx = interface.FindFirstAddress(AF_INET);
        if (addrIdx >= 0) {
            BNetworkInterfaceAddress addr;
            if (interface.GetAddressAt(addrIdx, addr) == B_OK) {
                BString ip = addr.Address().ToString(false);
                if (!ip.StartsWith("169.254")) {
                    fMyIpAddress = ip;
                    break;
                }
            }
        }
    }
}

LocalFileHttpServer::~LocalFileHttpServer()
{
    Stop();
}

status_t LocalFileHttpServer::Start()
{
    if (fRunning) return B_OK;

    fServerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (fServerSocket < 0) {
        DEBUG_PRINT("Failed to create socket\n");
        return B_ERROR;
    }

    int opt = 1;
    setsockopt(fServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = 0; ///< Bind to any free port

    if (bind(fServerSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        DEBUG_PRINT("Bind failed\n");
        close(fServerSocket);
        fServerSocket = -1;
        return B_ERROR;
    }

    socklen_t len = sizeof(serverAddr);
    if (getsockname(fServerSocket, (struct sockaddr *)&serverAddr, &len) == 0) {
        fPort = ntohs(serverAddr.sin_port);
    }

    if (listen(fServerSocket, 5) < 0) {
        DEBUG_PRINT("Listen failed\n");
        close(fServerSocket);
        fServerSocket = -1;
        return B_ERROR;
    }

    DEBUG_PRINT("Listening on %s:%d\n", fMyIpAddress.String(), fPort);

    fRunning = true;
    fServerThread = spawn_thread(_ServerThreadEntry, "LocalFileHttpServer", B_NORMAL_PRIORITY, this);
    if (fServerThread >= 0) {
        resume_thread(fServerThread);
    } else {
        fRunning = false;
        close(fServerSocket);
        fServerSocket = -1;
        return B_ERROR;
    }

    return B_OK;
}

void LocalFileHttpServer::Stop()
{
    if (!fRunning) return;
    fRunning = false;

    if (fServerSocket >= 0) {
        close(fServerSocket);
        fServerSocket = -1;
    }

    if (fServerThread >= 0) {
        status_t exitValue;
        wait_for_thread(fServerThread, &exitValue);
        fServerThread = -1;
    }
}

status_t LocalFileHttpServer::ServeFile(const BString& absolutePath, BString& outUrl)
{
    fCurrentFile = absolutePath;
    
    /// Create the URL
    /// DLNA renderers are picky, so URL encode the filename
    BString filename = absolutePath;
    int32 lastSlash = filename.FindLast('/');
    if (lastSlash >= 0) {
        filename.Remove(0, lastSlash + 1);
    }
    if (filename.IsEmpty()) filename = "stream.mp3";
    
    outUrl.SetToFormat("http://%s:%d/%s", fMyIpAddress.String(), fPort, _UrlEncode(filename).String());
    DEBUG_PRINT("Serving file: %s as %s\n", absolutePath.String(), outUrl.String());
    
    return B_OK;
}

int32 LocalFileHttpServer::_ServerThreadEntry(void* data)
{
    LocalFileHttpServer* server = static_cast<LocalFileHttpServer*>(data);
    server->_ServerLoop();
    return 0;
}

void LocalFileHttpServer::_ServerLoop()
{
    while (fRunning) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(fServerSocket, (struct sockaddr *)&clientAddr, &clientLen);

        if (!fRunning) {
            if (clientSocket >= 0) close(clientSocket);
            break;
        }

        if (clientSocket < 0) {
            continue;
        }

        ClientData* cd = new ClientData();
        cd->server = this;
        cd->socket = clientSocket;

        thread_id cThread = spawn_thread(_ClientThreadEntry, "LocalHttpClient", B_NORMAL_PRIORITY, cd);
        if (cThread >= 0) {
            resume_thread(cThread);
        } else {
            close(clientSocket);
            delete cd;
        }
    }
}

int32 LocalFileHttpServer::_ClientThreadEntry(void* data)
{
    ClientData* cd = static_cast<ClientData*>(data);
    cd->server->_HandleClient(cd->socket);
    delete cd;
    return 0;
}

void LocalFileHttpServer::_HandleClient(int clientSocket)
{
    char buffer[4096];
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesRead <= 0) {
        close(clientSocket);
        return;
    }
    buffer[bytesRead] = '\0';
    BString request(buffer);

    DEBUG_PRINT("Received request: %s\n", request.String());

    if (!request.StartsWith("GET ")) {
        const char* badMethod = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
        send(clientSocket, badMethod, strlen(badMethod), 0);
        close(clientSocket);
        return;
    }

    if (fCurrentFile.IsEmpty()) {
        const char* notFound = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(clientSocket, notFound, strlen(notFound), 0);
        close(clientSocket);
        return;
    }

    BFile file(fCurrentFile.String(), B_READ_ONLY);
    if (file.InitCheck() != B_OK) {
        const char* notFound = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(clientSocket, notFound, strlen(notFound), 0);
        close(clientSocket);
        return;
    }

    off_t fileSize = 0;
    file.GetSize(&fileSize);

    /// Check for Range header
    off_t startPos = 0;
    off_t endPos = fileSize - 1;
    bool isPartial = false;

    int32 rangeIdx = request.IFindFirst("Range: bytes=");
    if (rangeIdx >= 0) {
        int32 startNumIdx = rangeIdx + 13;
        int32 endNumIdx = request.FindFirst('-', startNumIdx);
        if (endNumIdx > startNumIdx) {
            BString startStr;
            request.CopyInto(startStr, startNumIdx, endNumIdx - startNumIdx);
            startPos = atoll(startStr.String());
            
            int32 lineEndR = request.FindFirst('\r', endNumIdx);
            int32 lineEndN = request.FindFirst('\n', endNumIdx);
            int32 lineEnd = lineEndR;
            if (lineEnd < 0) lineEnd = lineEndN;
            else if (lineEndN >= 0 && lineEndN < lineEnd) lineEnd = lineEndN;

            if (lineEnd > endNumIdx + 1) {
                BString endStr;
                request.CopyInto(endStr, endNumIdx + 1, lineEnd - (endNumIdx + 1));
                endPos = atoll(endStr.String());
            }
            isPartial = true;
        }
    }

    if (endPos >= fileSize) {
        endPos = fileSize - 1;
    }

    if (startPos > endPos || startPos >= fileSize) {
        const char* badRange = "HTTP/1.1 416 Range Not Satisfiable\r\n\r\n";
        send(clientSocket, badRange, strlen(badRange), 0);
        close(clientSocket);
        return;
    }

    off_t contentLength = endPos - startPos + 1;

    BString responseHeader;
    if (isPartial) {
        responseHeader.SetToFormat("HTTP/1.1 206 Partial Content\r\n"
                                   "Content-Range: bytes %lld-%lld/%lld\r\n",
                                   (long long)startPos, (long long)endPos, (long long)fileSize);
    } else {
        responseHeader = "HTTP/1.1 200 OK\r\n";
    }

    BString mimeType = "application/octet-stream";
    BString lowerFile = fCurrentFile;
    lowerFile.ToLower();
    if (lowerFile.EndsWith(".mp3")) mimeType = "audio/mpeg";
    else if (lowerFile.EndsWith(".flac")) mimeType = "audio/flac";
    else if (lowerFile.EndsWith(".ogg")) mimeType = "audio/ogg";
    else if (lowerFile.EndsWith(".m4a")) mimeType = "audio/mp4";
    else if (lowerFile.EndsWith(".wav")) mimeType = "audio/wav";
#if ENABLE_MIDI_PLAYBACK
    else if (lowerFile.EndsWith(".mid") || lowerFile.EndsWith(".midi")) {
        mimeType = "audio/midi";
    }
#endif

    responseHeader << "Content-Type: " << mimeType << "\r\n"
                   << "Content-Length: " << contentLength << "\r\n"
                   << "Accept-Ranges: bytes\r\n"
                   << "Connection: close\r\n\r\n";

    send(clientSocket, responseHeader.String(), responseHeader.Length(), 0);

    file.Seek(startPos, SEEK_SET);

    char fileBuf[32768];
    off_t remaining = contentLength;
    while (remaining > 0 && fRunning) {
        size_t toRead = std::min((off_t)sizeof(fileBuf), remaining);
        ssize_t readBytes = file.Read(fileBuf, toRead);
        if (readBytes <= 0) break;

        ssize_t sent = 0;
        while (sent < readBytes && fRunning) {
            ssize_t s = send(clientSocket, fileBuf + sent, readBytes - sent, 0);
            if (s < 0) break;
            sent += s;
        }
        if (sent < readBytes) break; ///< Network error
        
        remaining -= readBytes;
    }

    close(clientSocket);
}

BString LocalFileHttpServer::_UrlEncode(const BString& str)
{
    BString out;
    for (int32 i = 0; i < str.Length(); i++) {
        char c = str.ByteAt(i);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
            out << hex;
        }
    }
    return out;
}

BString LocalFileHttpServer::_UrlDecode(const BString& str)
{
    BString out;
    for (int32 i = 0; i < str.Length(); i++) {
        char c = str.ByteAt(i);
        if (c == '%') {
            if (i + 2 < str.Length()) {
                BString hex;
                str.CopyInto(hex, i + 1, 2);
                out << (char)strtol(hex.String(), nullptr, 16);
                i += 2;
            }
        } else if (c == '+') {
            out << ' ';
        } else {
            out << c;
        }
    }
    return out;
}
