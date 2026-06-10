//===- HarnessServer.cpp -- Unix-socket JSON-RPC 2.0 daemon loop ---------===//
#include "HarnessServer.h"
#include "JsonUtil.h"
#include "QueryEngine.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using json = nlohmann::json;

namespace
{
/// Set by SIGINT/SIGTERM. Handlers are installed without SA_RESTART so a
/// blocking accept() returns EINTR and the loop can observe the flag.
volatile sig_atomic_t gStop = 0;

void onSignal(int)
{
    gStop = 1;
}

/// Max request size; longer requests get a -32600 invalid-request error.
constexpr size_t kMaxRequestBytes = 1 << 20; // 1 MiB

/// JSON-RPC error response. `data` is attached only when non-null.
json rpcError(const json& id, int code, const std::string& message,
              const json& data = json())
{
    json err = {{"code", code}, {"message", message}};
    if (!data.is_null())
        err["data"] = data;
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", err}};
}

/// "known methods: summary, functions, shutdown (see schema)"
std::string knownMethodsHint()
{
    std::string hint = "known methods: ";
    for (const std::string& name : QueryEngine::methodNames())
        hint += name + ", ";
    hint += "shutdown (see schema)";
    return hint;
}

/// True if connecting to `path` succeeds, i.e. a live daemon is bound there.
bool socketIsLive(const std::string& path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    bool live = connect(fd, reinterpret_cast<sockaddr*>(&addr),
                        sizeof(addr)) == 0;
    close(fd);
    return live;
}
}

HarnessServer::HarnessServer(QueryEngine& eng, std::string path)
    : engine(eng), socketPath(std::move(path))
{
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(addr.sun_path))
        throw std::runtime_error("socket path too long: " + socketPath);
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    // A leftover file at the path makes bind() fail with EADDRINUSE. Probe it
    // first: if something accepts connections, a daemon is already running;
    // otherwise it is a stale file from a crashed daemon and safe to unlink.
    if (access(socketPath.c_str(), F_OK) == 0)
    {
        if (socketIsLive(socketPath))
            throw std::runtime_error("daemon already running on " + socketPath);
        unlink(socketPath.c_str());
    }

    listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0)
        throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
    if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        std::string msg = std::string("bind ") + socketPath + ": " +
                          std::strerror(errno);
        close(listenFd);
        listenFd = -1;
        throw std::runtime_error(msg);
    }
    if (listen(listenFd, 8) != 0)
    {
        std::string msg = std::string("listen ") + socketPath + ": " +
                          std::strerror(errno);
        close(listenFd);
        listenFd = -1;
        unlink(socketPath.c_str());
        throw std::runtime_error(msg);
    }

    // Clients may disconnect before reading the reply; a write must not kill
    // the daemon with SIGPIPE (send() will return EPIPE instead).
    signal(SIGPIPE, SIG_IGN);
    // No SA_RESTART: accept() must return EINTR so run() can check gStop.
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

HarnessServer::~HarnessServer()
{
    if (listenFd >= 0)
        close(listenFd);
    unlink(socketPath.c_str());
}

void HarnessServer::run()
{
    while (!gStop)
    {
        int fd = accept(listenFd, nullptr, nullptr);
        if (fd < 0)
        {
            if (errno == EINTR)
                continue; // signal: loop condition re-checks gStop
            break; // unexpected accept failure: shut down cleanly
        }
        bool stop = handleConnection(fd);
        close(fd);
        if (stop)
            break;
    }
}

bool HarnessServer::handleConnection(int fd)
{
    // Set 30-second recv/send timeouts so an idle or non-reading client cannot
    // wedge the single-threaded daemon forever.
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    // Deliberately ignore return values: if the socket option cannot be set the
    // daemon continues without the timeout (preferable to refusing connections).
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Read one newline-terminated request (or until EOF / size cap / timeout).
    std::string line;
    bool oversize = false;
    bool timedOut = false;
    char buf[65536];
    while (line.find('\n') == std::string::npos)
    {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue; // signal: retry the recv
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                timedOut = true; // SO_RCVTIMEO expired
                break;
            }
            break; // other error: treat what we have as the request
        }
        if (n == 0)
            break; // EOF: client disconnected
        line.append(buf, static_cast<size_t>(n));
        if (line.size() > kMaxRequestBytes)
        {
            oversize = true;
            break;
        }
    }

    if (timedOut)
    {
        // Send a -32000 "request timed out" error and close; the caller closes fd.
        json resp = rpcError(nullptr, -32000, "request timed out");
        std::string out = harness::dumpJson(resp) + "\n";
        size_t sent = 0;
        while (sent < out.size())
        {
            ssize_t n = send(fd, out.data() + sent, out.size() - sent, 0);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue; // signal: retry the send
                break;        // EAGAIN/EWOULDBLOCK or other error: give up silently
            }
            if (n == 0)
                break;
            sent += static_cast<size_t>(n);
        }
        return false;
    }

    bool shutdown = false;
    json resp =
        oversize ? rpcError(nullptr, -32600, "invalid request: larger than 1 MiB")
                 : handleRequest(line, shutdown);

    std::string out = harness::dumpJson(resp) + "\n";
    size_t sent = 0;
    while (sent < out.size())
    {
        ssize_t n = send(fd, out.data() + sent, out.size() - sent, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;  // signal: retry the send
            break;         // EAGAIN/EWOULDBLOCK or other: give up silently (close)
        }
        if (n == 0)
            break; // client went away
        sent += static_cast<size_t>(n);
    }
    return shutdown;
}

json HarnessServer::handleRequest(const std::string& line, bool& shutdown)
{
    json req = json::parse(line, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (req.is_discarded())
        return rpcError(nullptr, -32700, "parse error");

    json id = req.is_object() ? req.value("id", json()) : json();
    if (!req.is_object() || !req.contains("method") ||
        !req["method"].is_string())
        return rpcError(id, -32600, "invalid request: missing string 'method'");

    std::string method = req["method"].get<std::string>();
    if (method == "shutdown")
    {
        shutdown = true;
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"ok", true}}}};
    }

    json params = req.value("params", json::object());
    try
    {
        json result = engine.dispatch(method, params);
        return json{{"jsonrpc", "2.0"}, {"id", id},
                    {"result", std::move(result)}};
    }
    catch (const std::exception& e)
    {
        std::string what = e.what();
        if (what.rfind("unknown method", 0) == 0)
            return rpcError(id, -32601, what, {{"hint", knownMethodsHint()}});
        return rpcError(id, -32000, what);
    }
}
