//===- svf-harness.cpp -- LLM-friendly analysis harness daemon/CLI -------===//
#include "nlohmann/json.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using json = nlohmann::json;

static const char* kUsage =
    "svf-harness — LLM-friendly SVF query daemon/CLI\n"
    "  svf-harness serve <bitcode...> [--socket PATH]   start daemon\n"
    "  svf-harness <method> [args] [--socket PATH]      query the daemon\n"
    "  svf-harness shutdown [--socket PATH]             stop the daemon\n"
    "Methods: schema summary functions callers callees cfg defuse pts aliases vfpath reachable\n";

int main(int argc, char** argv)
{
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0)
    {
        std::fputs(kUsage, stdout);
        return argc < 2 ? 1 : 0;
    }
    json err = {{"error", {{"message", "not implemented"}, {"method", argv[1]}}}};
    std::puts(err.dump().c_str());
    return 2;
}
