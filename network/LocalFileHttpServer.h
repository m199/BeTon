#ifndef BETON_LOCAL_FILE_HTTP_SERVER_H
#define BETON_LOCAL_FILE_HTTP_SERVER_H

#include <String.h>
#include <OS.h>
#include <atomic>
#include <vector>

/**
 * @class LocalFileHttpServer
 * @brief A minimal, embedded HTTP server for streaming local files to DLNA renderers.
 */
class LocalFileHttpServer {
public:
    /**
     * @brief Constructs the local HTTP file server.
     */
    LocalFileHttpServer();

    /**
     * @brief Stops server and releases socket/thread resources.
     */
    ~LocalFileHttpServer();

    /**
     * @brief Starts the HTTP server on a random free port.
     * @return B_OK on success.
     */
    status_t Start();

    /**
     * @brief Stops the HTTP server.
     */
    void Stop();

    /**
     * @brief Serves a specific local file.
     * @param absolutePath The path to the file to serve.
     * @param outUrl Populated with the HTTP URL that points to this file.
     * @return B_OK on success.
     */
    status_t ServeFile(const BString& absolutePath, BString& outUrl);

private:
    /**
     * @brief Per-client handoff data for spawned client handler thread.
     */
    struct ClientData {
        LocalFileHttpServer* server;
        int socket;
    };

    /**
     * @brief Static thread entry for the accept loop.
     */
    static int32 _ServerThreadEntry(void* data);

    /**
     * @brief Static thread entry for one client connection.
     */
    static int32 _ClientThreadEntry(void* data);

    /**
     * @brief Accept loop that spawns client workers.
     */
    void _ServerLoop();

    /**
     * @brief Handles a single HTTP client request/response.
     */
    void _HandleClient(int clientSocket);
    
    /**
     * @brief URL-decodes a request path fragment.
     */
    BString _UrlDecode(const BString& str);

    /**
     * @brief URL-encodes a path fragment for renderer-safe URLs.
     */
    BString _UrlEncode(const BString& str);

    /** @brief Accept-loop thread id. */
    thread_id fServerThread;
    /** @brief Indicates whether server loop should keep running. */
    std::atomic<bool> fRunning;
    /** @brief Listening socket file descriptor. */
    int fServerSocket;
    /** @brief Bound TCP port. */
    int fPort;
    
    /** @brief Absolute path currently exposed via HTTP endpoint. */
    BString fCurrentFile;
    /** @brief Non-loopback IPv4 used to build renderer-facing URLs. */
    BString fMyIpAddress;
};

#endif // BETON_LOCAL_FILE_HTTP_SERVER_H
