#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <zstd.h>

#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include <xxhash.h>

#include "BytecodeUtils.h"
#include "BytecodeBuilder.h"
#include "Compiler.h"

extern "C" {
#include "blake3.h"
}

namespace physics_compiler {

class BytecodeEncoder : public Luau::BytecodeEncoder {
public:
    void encode(uint32_t* data, size_t count) override
    {
        for (size_t i = 0; i < count;) {
            auto& opcode = *reinterpret_cast<uint8_t*>(data + i);
            i += Luau::getOpLength(LuauOpcode(opcode));
            opcode *= 227;
        }
    }
};

struct CompileResult {
    bool ok = false;
    std::vector<char> bytecode;
    std::string error;
};

namespace detail {

// https://v3rm.net/threads/release-bytecode-signing.22993/
static constexpr uint32_t MAGIC_A = 0x4C464F52;
static constexpr uint32_t MAGIC_B = 0x946AC432;
static constexpr uint8_t KEY_BYTES[4] = {0x52, 0x4F, 0x46, 0x4C};

inline uint8_t rotl8(uint8_t value, int shift)
{
    shift &= 7;
    return static_cast<uint8_t>((value << shift) | (value >> (8 - shift)));
}

inline std::vector<char> compress(const std::string& bytecode)
{
    const size_t data_size = bytecode.size();
    const size_t max_size = ZSTD_compressBound(data_size);

    std::vector<char> buffer(max_size + 8);
    buffer[0] = 'R';
    buffer[1] = 'S';
    buffer[2] = 'B';
    buffer[3] = '1';
    std::memcpy(buffer.data() + 4, &data_size, sizeof(data_size));

    const size_t compressed_size =
        ZSTD_compress(buffer.data() + 8, max_size, bytecode.data(), data_size, ZSTD_maxCLevel());
    if (ZSTD_isError(compressed_size)) {
        return {};
    }

    const size_t total_size = compressed_size + 8;
    const uint32_t key = XXH32(buffer.data(), total_size, 42u);
    const auto* key_bytes = reinterpret_cast<const uint8_t*>(&key);

    for (size_t i = 0; i < total_size; ++i) {
        buffer[i] ^= static_cast<char>(key_bytes[i % 4] + i * 41u);
    }

    buffer.resize(total_size);
    return buffer;
}

inline std::vector<char> sign_and_compress(const std::string& bytecode)
{
    if (bytecode.empty()) {
        return {};
    }

    constexpr uint32_t FOOTER_SIZE = 40u;
    std::vector<uint8_t> blake3_hash(32);
    {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, bytecode.data(), bytecode.size());
        blake3_hasher_finalize(&hasher, blake3_hash.data(), blake3_hash.size());
    }

    std::vector<uint8_t> transformed_hash(32);
    for (int i = 0; i < 32; ++i) {
        const uint8_t byte = KEY_BYTES[i & 3];
        const uint8_t hash_byte = blake3_hash[static_cast<size_t>(i)];
        const uint8_t combined = static_cast<uint8_t>(byte + i);

        uint8_t result = 0;
        switch (i & 3) {
        case 0:
            result = rotl8(static_cast<uint8_t>(hash_byte ^ ~byte), (combined & 3) + 1);
            break;
        case 1:
            result = rotl8(static_cast<uint8_t>(byte ^ ~hash_byte), (combined & 3) + 2);
            break;
        case 2:
            result = rotl8(static_cast<uint8_t>(hash_byte ^ ~byte), (combined & 3) + 3);
            break;
        default:
            result = rotl8(static_cast<uint8_t>(byte ^ ~hash_byte), (combined & 3) + 4);
            break;
        }

        transformed_hash[static_cast<size_t>(i)] = result;
    }

    std::vector<uint8_t> footer(FOOTER_SIZE, 0);
    const uint32_t first_hash_dword = *reinterpret_cast<const uint32_t*>(transformed_hash.data());
    const uint32_t footer_prefix = first_hash_dword ^ MAGIC_B;
    const uint32_t xored = first_hash_dword ^ MAGIC_A;

    std::memcpy(footer.data(), &footer_prefix, sizeof(footer_prefix));
    std::memcpy(footer.data() + 4, &xored, sizeof(xored));
    std::memcpy(footer.data() + 8, transformed_hash.data(), transformed_hash.size());

    std::string signed_bytecode = bytecode;
    signed_bytecode.append(reinterpret_cast<const char*>(footer.data()), footer.size());
    return compress(signed_bytecode);
}

}

inline CompileResult compile_and_sign(const std::string& source)
{
    static BytecodeEncoder encoder;

    const std::string bytecode = Luau::compile(source, {}, {}, &encoder);
    if (bytecode.empty()) {
        return {false, {}, "Luau compiler returned empty output"};
    }

    if (bytecode.front() == '\0') {
        std::string message = bytecode.substr(1);
        message.erase(std::remove(message.begin(), message.end(), '\0'), message.end());
        if (message.empty()) {
            message = "Luau compile error";
        }
        return {false, {}, message};
    }

    auto signed_payload = detail::sign_and_compress(bytecode);
    if (signed_payload.empty()) {
        return {false, {}, "Failed to sign/compress bytecode"};
    }

    return {true, std::move(signed_payload), {}};
}

}
