//===- svf-harness.cpp -- LLM-friendly analysis harness daemon/CLI -------===//
#include "QueryEngine.h"
#include "Util/CommandLine.h"
#include "Util/Options.h"
#include "nlohmann/json.hpp"
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

using json = nlohmann::json;

static const char* kUsage =
    "svf-harness — LLM-friendly SVF query daemon/CLI\n"
    "  svf-harness serve <bitcode...> [--socket PATH]   start daemon\n"
    "  svf-harness <method> [args] [--socket PATH]      query the daemon\n"
    "  svf-harness --oneshot <method> <bitcode...>      build state + run one query\n"
    "  svf-harness shutdown [--socket PATH]             stop the daemon\n"
    "Methods: schema summary functions callers callees cfg defuse pts aliases vfpath reachable\n";

/// `svf-harness --oneshot <method> [options] <bitcode...>`: build analysis
/// state, answer a single query on stdout, exit.
static int runOneshot(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fputs(kUsage, stdout);
        return 1;
    }
    std::string method = argv[2];
    // Shifted argv: program name + everything after the method name.
    // Stats are forced off: oneshot stdout must be pure JSON.
    static char statOff[] = "-stat=false";
    std::vector<char*> shifted;
    shifted.push_back(argv[0]);
    shifted.push_back(statOff);
    for (int i = 3; i < argc; ++i)
        shifted.push_back(argv[i]);
    std::vector<std::string> moduleNameVec = OptionBase::parseOptions(
        static_cast<int>(shifted.size()), shifted.data(),
        "svf-harness oneshot", "[options] <input-bitcode...>");
    try
    {
        QueryEngine engine(moduleNameVec);
        std::puts(engine.dispatch(method, json::object()).dump().c_str());
        return 0;
    }
    catch (const std::exception& e)
    {
        json err = {{"error", {{"message", e.what()}}}};
        std::puts(err.dump().c_str());
        return 1;
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
    json err = {{"error", {{"message", "not implemented"}, {"method", argv[1]}}}};
    std::puts(err.dump().c_str());
    return 2;
}
