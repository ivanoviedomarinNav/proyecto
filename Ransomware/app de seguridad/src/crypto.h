// crypto.h - envoltorio minimo sobre CNG (bcrypt) de Windows.
// AES-256-GCM, PBKDF2-HMAC-SHA256 y RSA-OAEP-SHA256.
#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

bool CryptoRandom(void* buffer, size_t length);

bool Pbkdf2Sha256(const void* password, size_t passwordLength,
                  const uint8_t* salt, size_t saltLength,
                  uint64_t iterations,
                  uint8_t* output, size_t outputLength);

class AesGcmKey {
public:
    AesGcmKey() = default;
    ~AesGcmKey();
    AesGcmKey(const AesGcmKey&) = delete;
    AesGcmKey& operator=(const AesGcmKey&) = delete;

    bool Init(const uint8_t* key, size_t keyLength);

    bool Encrypt(const uint8_t* nonce, size_t nonceLength,
                 const uint8_t* aad, size_t aadLength,
                 const uint8_t* input, size_t inputLength,
                 uint8_t* output, uint8_t* tag, size_t tagLength = 16);

    bool Decrypt(const uint8_t* nonce, size_t nonceLength,
                 const uint8_t* aad, size_t aadLength,
                 const uint8_t* input, size_t inputLength,
                 const uint8_t* tag, size_t tagLength,
                 uint8_t* output);

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
};

// Cifra "data" con la clave publica RSA y relleno OAEP-SHA256.
bool RsaWrap(const uint8_t* publicBlob, size_t publicBlobLength,
             const uint8_t* data, size_t dataLength,
             std::vector<uint8_t>& output);

// Descifra con la clave privada RSA (blob BCRYPT_RSAPRIVATE_BLOB).
bool RsaUnwrap(const uint8_t* privateBlob, size_t privateBlobLength,
               const uint8_t* data, size_t dataLength,
               std::vector<uint8_t>& output);

// Borra memoria de forma que el compilador no la optimice.
void SecureZero(void* data, size_t length);
