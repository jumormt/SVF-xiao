//===- svf-harness.cpp -- LLM-friendly analysis harness daemon/CLI -------===//
#include "HarnessServer.h"
#include "JsonUtil.h"
#include "QueryEngine.h"
#include "Util/CommandLine.h"
#include "Util/Options.h"
#include "nlohmann/json.hpp"
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <glob.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;
using harness::dumpJson;

static const char* kUsage =
    "svf-harness — LLM-friendly SVF query daemon/CLI\n"
    "  svf-harness serve <bitcode...> [--socket PATH] [--analysis-config JSON]   start daemon\n"
    "  svf-harness <method> [args] [--socket PATH]      query the daemon\n"
    "  svf-harness --oneshot <method> [--params JSON] [--analysis-config JSON] <bitcode...>  build state + run one query\n"
    "  svf-harness shutdown [--socket PATH]             stop the daemon\n"
    "Methods: schema summary functions callers callees cfg defuse pts aliases cfl_pts cfl_aliases dda_pts dda_aliases saber_leaks saber_double_frees saber_file_leaks mta_summary mta_mhp vfpath reachable graphs graph_nodes graph_edges node neighbors analysis_config\n";

/// Oneshot error contract: print {"error":{code,message}} on stdout, return 1.
static int jsonError(const std::string& message, int code = -32000)
{
    json err = {{"error", {{"code", code}, {"message", message}}}};
    std::puts(dumpJson(err).c_str());
    return 1;
}

/// Client-mode error contract: print the bare JSON-RPC error object
/// {code,message} on stdout (same shape as a daemon "error" member), return 1.
static int clientError(const std::string& message, int code = -32000)
{
    json err = {{"code", code}, {"message", message}};
    std::puts(dumpJson(err).c_str());
    return 1;
}

/// Parse the value of a `--params <json>` flag. Returns an error message
/// (empty string on success) so each caller can report it under its own
/// output contract; *out is only written on success.
static std::string parseParamsArg(int argc, char** argv, int i, json* out)
{
    if (i + 1 >= argc)
        return "--params requires a JSON argument";
    json p = json::parse(argv[i + 1], /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (p.is_discarded())
        return std::string("invalid --params JSON: ") + argv[i + 1];
    *out = std::move(p);
    return "";
}

static std::string parseAnalysisConfigArg(int argc, char** argv, int i,
                                          QueryEngine::HarnessConfig* out)
{
    if (i + 1 >= argc)
        return "--analysis-config requires a JSON argument";
    json p = json::parse(argv[i + 1], /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (p.is_discarded())
        return std::string("invalid --analysis-config JSON: ") + argv[i + 1];
    try
    {
        *out = QueryEngine::HarnessConfig::fromJson(p);
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
    return "";
}

/// Daemon socket path resolution, in priority order:
///   1. explicit --socket PATH
///   2. env SVF_HARNESS_SOCKET
///   3. /tmp/svf-harness-<id>.sock, <id> = first 12 hex chars of the FNV-1a
///      hash of the absolute bitcode paths joined by ':' (stable per module set)
static std::string resolveSocketPath(const std::string& explicitPath,
                                     const std::vector<std::string>& moduleNames)
{
    if (!explicitPath.empty())
        return explicitPath;
    if (const char* env = std::getenv("SVF_HARNESS_SOCKET"))
        if (*env)
            return env;
    uint64_t h = 1469598103934665603ULL; // FNV-1a 64-bit offset basis
    auto feed = [&h](const char* s)
    {
        for (; *s; ++s)
        {
            h ^= static_cast<unsigned char>(*s);
            h *= 1099511628211ULL; // FNV-1a 64-bit prime
        }
    };
    bool first = true;
    for (const std::string& name : moduleNames)
    {
        if (!first)
            feed(":");
        first = false;
        char buf[PATH_MAX];
        feed(realpath(name.c_str(), buf) ? buf : name.c_str());
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string("/tmp/svf-harness-") + std::string(hex, 12) + ".sock";
}

/// `svf-harness serve <bitcode...> [--socket PATH]`: build analysis state,
/// then serve JSON-RPC on a Unix socket until "shutdown" or SIGINT/SIGTERM.
/// stdout stays clean; the ready line goes to stderr.
static int runServe(int argc, char** argv)
{
    std::string socketArg;
    QueryEngine::HarnessConfig config;
    // Shifted argv for OptionBase: program name + args minus --socket PATH.
    // Stats forced off: daemon stdout must stay clean.
    static char statOff[] = "-stat=false";
    std::vector<char*> shifted;
    shifted.push_back(argv[0]);
    shifted.push_back(statOff);
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--socket") == 0)
        {
            if (i + 1 >= argc)
            {
                std::fputs("svf-harness: --socket requires a PATH argument\n",
                           stderr);
                return 1;
            }
            socketArg = argv[i + 1];
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--analysis-config") == 0)
        {
            std::string err = parseAnalysisConfigArg(argc, argv, i, &config);
            if (!err.empty())
            {
                std::fprintf(stderr, "svf-harness: %s\n", err.c_str());
                return 1;
            }
            ++i;
            continue;
        }
        shifted.push_back(argv[i]);
    }
    std::vector<std::string> moduleNameVec = OptionBase::parseOptions(
        static_cast<int>(shifted.size()), shifted.data(),
        "svf-harness serve", "[options] <input-bitcode...>");
    try
    {
        QueryEngine engine(moduleNameVec, config);
        std::string path = resolveSocketPath(socketArg, moduleNameVec);
        HarnessServer server(engine, path);
        std::fprintf(stderr, "svf-harness: listening on %s\n", path.c_str());
        server.run();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "svf-harness: %s\n", e.what());
        return 1;
    }
}

/// Client socket resolution: --socket > SVF_HARNESS_SOCKET > the single
/// /tmp/svf-harness-*.sock if exactly one exists. Returns "" after printing
/// a JSON error when no unambiguous socket can be found.
static std::string resolveClientSocket(const std::string& explicitPath)
{
    if (!explicitPath.empty())
        return explicitPath;
    if (const char* env = std::getenv("SVF_HARNESS_SOCKET"))
        if (*env)
            return env;
    std::vector<std::string> candidates;
    glob_t g;
    if (glob("/tmp/svf-harness-*.sock", 0, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; ++i)
            candidates.push_back(g.gl_pathv[i]);
    globfree(&g);
    if (candidates.size() == 1)
    {
        std::fprintf(stderr, "svf-harness: using auto-discovered socket %s\n",
                     candidates[0].c_str());
        return candidates[0];
    }
    std::string msg = candidates.empty()
                          ? "no daemon socket found"
                          : "multiple daemon sockets found:";
    for (const std::string& c : candidates)
        msg += " " + c;
    clientError(msg + " (pass --socket or set SVF_HARNESS_SOCKET)");
    return "";
}

/// `svf-harness <method> [--params JSON] [--socket PATH]`: send one JSON-RPC
/// request to a running daemon. Prints the `result` JSON (exit 0) or the
/// `error` object (exit 1) on stdout.
static int runClient(int argc, char** argv)
{
    std::string method = argv[1];
    std::string socketArg;
    json params = json::object();
    QueryEngine::HarnessConfig config;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--params") == 0)
        {
            std::string err = parseParamsArg(argc, argv, i, &params);
            if (!err.empty())
                return clientError(err);
            ++i;
        }
        else if (std::strcmp(argv[i], "--socket") == 0)
        {
            if (i + 1 >= argc)
                return clientError("--socket requires a PATH argument");
            socketArg = argv[i + 1];
            ++i;
        }
        else
        {
            return clientError(std::string("unexpected argument: ") + argv[i] +
                               " (client mode takes --params JSON and --socket PATH;"
                               " bitcode inputs belong to serve/--oneshot)");
        }
    }
    std::string path = resolveClientSocket(socketArg);
    if (path.empty())
        return 1; // resolveClientSocket already printed the error

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
        return clientError("socket path too long: " + path);
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return clientError(std::string("socket: ") + std::strerror(errno));
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close(fd);
        return clientError("cannot connect to " + path + ": " +
                           std::strerror(errno) +
                           " (is the daemon running? start with: svf-harness"
                           " serve <bitcode...> --socket " + path + ")");
    }

    json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method},
                {"params", params}};
    std::string out = dumpJson(req) + "\n";
    size_t sent = 0;
    while (sent < out.size())
    {
        ssize_t n = send(fd, out.data() + sent, out.size() - sent, 0);
        if (n <= 0)
        {
            close(fd);
            return clientError("send failed: " +
                               std::string(std::strerror(errno)));
        }
        sent += static_cast<size_t>(n);
    }

    std::string line;
    char buf[65536];
    while (line.find('\n') == std::string::npos)
    {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        line.append(buf, static_cast<size_t>(n));
    }
    close(fd);

    json resp = json::parse(line, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (resp.is_discarded() || !resp.is_object())
        return clientError("malformed response from daemon");
    if (resp.contains("error"))
    {
        std::puts(dumpJson(resp["error"]).c_str());
        return 1;
    }
    std::puts(dumpJson(resp.value("result", json())).c_str());
    return 0;
}

/// `svf-harness --oneshot <method> [options] <bitcode...>`: build analysis
/// state, answer a single query on stdout, exit.
///
/// CLI output contract:
///   exit 0  => stdout is the JSON result
///   exit != 0 => stdout is a JSON error object or empty
///               (option-parser diagnostics go to stderr)
static int runOneshot(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fputs(kUsage, stdout);
        return 1;
    }
    std::string method = argv[2];
    // Extract `--params <json>` (not an SVF option; OptionBase would reject
    // it). Defaults to an empty object; invalid JSON => standard JSON error.
    json params = json::object();
    QueryEngine::HarnessConfig config;
    // Shifted argv: program name + everything after the method name.
    // Stats are forced off: oneshot stdout must be pure JSON.
    static char statOff[] = "-stat=false";
    std::vector<char*> shifted;
    shifted.push_back(argv[0]);
    shifted.push_back(statOff);
    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--params") == 0)
        {
            std::string err = parseParamsArg(argc, argv, i, &params);
            if (!err.empty())
                return jsonError(err);
            ++i; // skip the JSON value
            continue;
        }
        if (std::strcmp(argv[i], "--analysis-config") == 0)
        {
            std::string err = parseAnalysisConfigArg(argc, argv, i, &config);
            if (!err.empty())
                return jsonError(err);
            ++i;
            continue;
        }
        shifted.push_back(argv[i]);
    }
    std::vector<std::string> moduleNameVec = OptionBase::parseOptions(
        static_cast<int>(shifted.size()), shifted.data(),
        "svf-harness oneshot", "[options] <input-bitcode...>");
    try
    {
        QueryEngine engine(moduleNameVec, config);
        std::puts(dumpJson(engine.dispatch(method, params)).c_str());
        return 0;
    }
    catch (const std::exception& e)
    {
        return jsonError(e.what());
    }
}

int main(int argc, char** argv)
{
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0)
    {
        std::fputs(kUsage, stdout);
        return argc < 2 ? 1 : 0;
    }
    if (std::strcmp(argv[1], "--oneshot") == 0)
        return runOneshot(argc, argv);
    if (std::strcmp(argv[1], "serve") == 0)
        return runServe(argc, argv);
    return runClient(argc, argv);
}
