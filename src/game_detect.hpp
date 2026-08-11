#pragma once

#include <string>

namespace vkBasalt {

    // Compute a unique, human-readable game identifier.
    // Uses Steam AppID + game name when available, falls back to exe path + MD5.
    std::string computeGameId();

    // MD5 (RFC 1321) — used to fingerprint non-Steam executables.
    std::string md5Hash(const std::string& input);

} // namespace vkBasalt
