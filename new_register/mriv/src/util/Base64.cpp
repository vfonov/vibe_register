#include "util/Base64.hpp"

#include <array>

namespace mriv::term
{

namespace
{

constexpr char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

const std::array<int, 256> kDecodeTable = []
{
    std::array<int, 256> t{};
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i] = -1;
    for (std::size_t i = 0; i < 64; ++i)
        t[static_cast<unsigned char>(kEncodeTable[i])] = static_cast<int>(i);
    t[static_cast<unsigned char>('=')] = -2;
    return t;
}();

} // namespace

std::string base64Encode(const uint8_t* data, std::size_t size)
{
    if (!data || size == 0)
        return {};

    std::string out;
    out.reserve(((size + 2) / 3) * 4);

    for (std::size_t i = 0; i < size; i += 3)
    {
        uint32_t b = (static_cast<uint32_t>(data[i]) << 16);
        if (i + 1 < size)
            b |= (static_cast<uint32_t>(data[i + 1]) << 8);
        if (i + 2 < size)
            b |= static_cast<uint32_t>(data[i + 2]);

        out.push_back(kEncodeTable[(b >> 18) & 0x3F]);
        out.push_back(kEncodeTable[(b >> 12) & 0x3F]);
        out.push_back(i + 1 < size ? kEncodeTable[(b >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < size ? kEncodeTable[b & 0x3F] : '=');
    }

    return out;
}

std::vector<uint8_t> base64Decode(std::string_view input)
{
    if (input.empty())
        return {};

    // Base64 length must be a multiple of 4 (ignoring trailing whitespace is
    // unnecessary for our own encoder's output, so we keep it strict).
    if (input.size() % 4 != 0)
        return {};

    std::vector<uint8_t> out;
    out.reserve((input.size() / 4) * 3);

    std::size_t i = 0;
    while (i < input.size())
    {
        std::array<int, 4> vals{};
        for (int j = 0; j < 4; ++j, ++i)
        {
            int v = kDecodeTable[static_cast<unsigned char>(input[i])];
            if (v == -1)
                return {}; // illegal character
            vals[j] = v;
        }

        uint32_t b = (static_cast<uint32_t>(vals[0] & 0x3F) << 18) |
                     (static_cast<uint32_t>(vals[1] & 0x3F) << 12);
        out.push_back(static_cast<uint8_t>((b >> 16) & 0xFF));

        if (input[i - 2] != '=')
        {
            b |= (static_cast<uint32_t>(vals[2] & 0x3F) << 6);
            out.push_back(static_cast<uint8_t>((b >> 8) & 0xFF));
        }

        if (input[i - 1] != '=')
        {
            b |= (static_cast<uint32_t>(vals[3] & 0x3F));
            out.push_back(static_cast<uint8_t>(b & 0xFF));
        }
    }

    return out;
}

} // namespace mriv::term
