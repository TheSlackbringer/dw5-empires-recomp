#pragma once
// Self-contained SHA-1 implementation (RFC 3174) with full 64-bit length support for multi-gigabyte ISOs.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace dw5e::setup {

class Sha1
{
public:
    Sha1() { reset(); }

    void reset()
    {
        m_state[0] = 0x67452301u;
        m_state[1] = 0xEFCDAB89u;
        m_state[2] = 0x98BADCFEu;
        m_state[3] = 0x10325476u;
        m_state[4] = 0xC3D2E1F0u;
        m_count = 0;
        m_bufferLen = 0;
    }

    void update(const void *data, size_t len)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        m_count += len;

        if (m_bufferLen > 0)
        {
            size_t take = 64 - m_bufferLen;
            if (take > len)
                take = len;
            std::memcpy(m_buffer.data() + m_bufferLen, bytes, take);
            m_bufferLen += take;
            bytes += take;
            len -= take;

            if (m_bufferLen == 64)
            {
                transform(m_buffer.data());
                m_bufferLen = 0;
            }
        }

        while (len >= 64)
        {
            transform(bytes);
            bytes += 64;
            len -= 64;
        }

        if (len > 0)
        {
            std::memcpy(m_buffer.data(), bytes, len);
            m_bufferLen = len;
        }
    }

    std::string hex()
    {
        uint64_t totalBits = m_count * 8ULL;

        // Append 0x80
        uint8_t pad = 0x80;
        update(&pad, 1);

        // Pad with zeros until length is 56 mod 64
        uint8_t zero = 0;
        while ((m_count % 64) != 56)
            update(&zero, 1);

        // Append 64-bit length in big-endian
        std::array<uint8_t, 8> lengthBytes{};
        for (int i = 7; i >= 0; --i)
        {
            lengthBytes[7 - i] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFF);
        }
        update(lengthBytes.data(), 8);

        // Output hex
        std::string out;
        out.reserve(40);
        char buf[9];
        for (uint32_t val : m_state)
        {
            std::snprintf(buf, sizeof(buf), "%08x", val);
            out += buf;
        }
        return out;
    }

private:
    static uint32_t rotl(uint32_t val, int bits)
    {
        return (val << bits) | (val >> (32 - bits));
    }

    void transform(const uint8_t *block)
    {
        std::array<uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i)
        {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f = 0, k = 0;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }

            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
    }

    std::array<uint32_t, 5> m_state{};
    uint64_t m_count = 0;
    size_t m_bufferLen = 0;
    std::array<uint8_t, 64> m_buffer{};
};

inline std::string sha1Hex(const void *data, size_t len)
{
    Sha1 sha;
    sha.update(data, len);
    return sha.hex();
}

} // namespace dw5e::setup
