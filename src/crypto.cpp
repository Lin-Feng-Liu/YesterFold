#include "crypto.h"
#include <cstring>
#include <stdexcept>

namespace crypto {

bool init() {
    if (sodium_init() < 0) return false;
    return true;
}

bool deriveKey(const char* password, size_t passLen,
               const unsigned char* salt, unsigned char* keyOut) {
    int ret = crypto_pwhash(
        keyOut, KEY_LEN,
        password, passLen,
        salt,
        ARGON_OPS, ARGON_MEM,
        crypto_pwhash_ALG_ARGON2ID13
    );
    return ret == 0;
}

std::vector<unsigned char> encryptFile(const std::string& plaintext,
                                       const char* password) {
    unsigned char salt[SALT_LEN];
    unsigned char nonce[NONCE_LEN];
    randombytes_buf(salt, SALT_LEN);
    randombytes_buf(nonce, NONCE_LEN);

    unsigned char key[KEY_LEN];
    if (!deriveKey(password, std::strlen(password), salt, key)) {
        throw std::runtime_error("密钥派生失败");
    }

    size_t cipherLen = plaintext.size() + TAG_LEN;
    std::vector<unsigned char> ciphertext(cipherLen);
    unsigned long long actualLen = 0;
    int ret = crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext.data(), &actualLen,
        reinterpret_cast<const unsigned char*>(plaintext.data()),
        plaintext.size(),
        nullptr, 0,
        nullptr,
        nonce,
        key
    );
    sodium_memzero(key, KEY_LEN);
    if (ret != 0) throw std::runtime_error("加密失败");

    std::vector<unsigned char> result(HEADER_SIZE + actualLen);
    unsigned char* p = result.data();

    p[0] = static_cast<unsigned char>(MAGIC & 0xFF);
    p[1] = static_cast<unsigned char>((MAGIC >> 8) & 0xFF);
    p[2] = static_cast<unsigned char>((MAGIC >> 16) & 0xFF);
    p[3] = static_cast<unsigned char>((MAGIC >> 24) & 0xFF);
    p += 4;

    p[0] = static_cast<unsigned char>(VERSION & 0xFF);
    p[1] = static_cast<unsigned char>((VERSION >> 8) & 0xFF);
    p[2] = static_cast<unsigned char>((VERSION >> 16) & 0xFF);
    p[3] = static_cast<unsigned char>((VERSION >> 24) & 0xFF);
    p += 4;

    std::memcpy(p, salt, SALT_LEN); p += SALT_LEN;
    std::memcpy(p, nonce, NONCE_LEN); p += NONCE_LEN;
    std::memcpy(p, ciphertext.data(), actualLen);

    return result;
}

std::optional<std::string> decryptFile(const unsigned char* data, size_t size,
                                       const char* password) {
    if (size < HEADER_SIZE + TAG_LEN) return std::nullopt;

    const unsigned char* p = data;

    uint32_t magic = static_cast<uint32_t>(p[0])
                   | (static_cast<uint32_t>(p[1]) << 8)
                   | (static_cast<uint32_t>(p[2]) << 16)
                   | (static_cast<uint32_t>(p[3]) << 24);
    if (magic != MAGIC) return std::nullopt;
    p += 4;

    uint32_t version = static_cast<uint32_t>(p[0])
                     | (static_cast<uint32_t>(p[1]) << 8)
                     | (static_cast<uint32_t>(p[2]) << 16)
                     | (static_cast<uint32_t>(p[3]) << 24);
    if (version != VERSION) return std::nullopt;
    p += 4;

    const unsigned char* salt = p;
    p += SALT_LEN;
    const unsigned char* nonce = p;
    p += NONCE_LEN;

    const unsigned char* cipherWithTag = p;
    size_t cipherWithTagLen = size - HEADER_SIZE;

    unsigned char key[KEY_LEN];
    if (!deriveKey(password, std::strlen(password), salt, key)) return std::nullopt;

    size_t plainLen = cipherWithTagLen - TAG_LEN;
    std::string plaintext(plainLen, '\0');
    unsigned long long actualLen = 0;
    int ret = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char*>(plaintext.data()), &actualLen,
        nullptr,
        cipherWithTag, cipherWithTagLen,
        nullptr, 0,
        nonce,
        key
    );
    sodium_memzero(key, KEY_LEN);

    if (ret != 0) return std::nullopt;
    plaintext.resize(actualLen);
    return plaintext;
}

} // namespace crypto
