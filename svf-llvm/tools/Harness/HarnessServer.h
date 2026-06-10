//===- HarnessServer.h -- Unix-socket JSON-RPC 2.0 daemon loop -----------===//
#pragma once
#include "nlohmann/json.hpp"
#include <string>
class QueryEngine;

/// Single-threaded Unix-socket JSON-RPC 2.0 server (newline-delimited).
/// One request per connection. "shutdown" stops the loop. Owns the socket
/// file: unlinks on clean exit and on SIGINT/SIGTERM.
///
/// The constructor binds + listens (throws std::runtime_error on failure,
/// e.g. when another daemon is already bound to the path); the socket file
/// therefore exists as soon as construction returns.
///
/// Known limitation: a signal landing between the gStop check and accept()
/// entry is not detected until the next connection (self-pipe/ppoll would
/// close this race; deferred — see docs/FUTURE.md).
///
/// Intentional simplifications:
///   - The `jsonrpc` version field in incoming requests is not validated.
///   - Requests without an `id` field get responses with `id: null`
///     (JSON-RPC notifications are not treated specially).
class HarnessServer
{
public:
    HarnessServer(QueryEngine& engine, std::string socketPath);
    ~HarnessServer();
    /// Blocks in the accept loop. Returns when "shutdown" is received.
    void run();

    HarnessServer(const HarnessServer&) = delete;
    HarnessServer& operator=(const HarnessServer&) = delete;

private:
    /// Serve one connection. Returns true when the request was "shutdown".
    bool handleConnection(int fd);
    /// Build the JSON-RPC response object for one raw request line.
    /// Sets `shutdown` to true for the "shutdown" method. Never throws.
    nlohmann::json handleRequest(const std::string& line, bool& shutdown);

    QueryEngine& engine;
    std::string socketPath;
    int listenFd = -1;
};
