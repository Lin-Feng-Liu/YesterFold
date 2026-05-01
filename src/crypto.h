#pragma once
#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <sodium.h>

namespace crypto {

constexpr uint32_t MAGIC   = 0x31524944;    // "DIR1" little-endian
constexpr uint32_t VERSION = 1;
constexpr size_t   HEADER_SIZE = 48;
constexpr size_t   SALT_LEN    = crypto_pwhash_SALTBYTES;
constexpr size_t   NONCE_LEN   = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr size_t   KEY_LEN     = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
constexpr size_t   TAG_LEN     = crypto_aead_xchacha20poly1305_ietf_ABYTES;
constexpr size_t   ARGON_OPS   = crypto_pwhash_OPSLIMIT_MODERATE;
constexpr size_t   ARGON_MEM   = crypto_pwhash_MEMLIMIT_MODERATE;

bool init();
bool deriveKey(const char* password, size_t passLen,
               const unsigned char* salt, unsigned char* keyOut);
std::vector<unsigned char> encryptFile(const std::string& plaintext,
                                       const char* password);
std::optional<std::string> decryptFile(const unsigned char* data, size_t size,
                                       const char* password);

} // namespace crypto
