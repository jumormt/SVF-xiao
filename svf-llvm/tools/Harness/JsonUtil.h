//===- JsonUtil.h -- shared JSON serialization helpers -------------------===//
#pragma once
#include "nlohmann/json.hpp"
#include <string>

namespace harness
{
/// Serialize a JSON value to a string using the replace error handler so that
/// arbitrary bytes from LLVM symbols (e.g. non-UTF-8 identifiers) are replaced
/// with U+FFFD instead of throwing nlohmann::json::type_error.316.
/// Use this for ALL output (stdout and socket writes alike).
inline std::string dumpJson(const nlohmann::json& j)
{
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}
}
