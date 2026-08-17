#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mriv::term
{

/// Encode a byte buffer as standard base64.
std::string base64Encode(const uint8_t* data, std::size_t size);

/// Decode a base64 string into bytes. Returns empty on illegal input.
std::vector<uint8_t> base64Decode(std::string_view input);

} // namespace mriv::term
