#include "crypto.h"

#include <cstring>

bool CryptoRandom(void* buffer, size_t length) {
    if (length == 0) return true;
    NTSTATUS status = BCryptGenRandom(nullptr, static_cast<PUCHAR>(buffer),
                                      static_cast<ULONG>(length),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return BCRYPT_SUCCESS(status);
}

bool Pbkdf2Sha256(const void* password, size_t passwordLength,
                  const uint8_t* salt, size_t saltLength,
                  uint64_t iterations,
                  uint8_t* output, size_t outputLength) {
    BCRYPT_ALG_HANDLE prf = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&prf, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                  BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) return false;

    status = BCryptDeriveKeyPBKDF2(prf,
                                   static_cast<PUCHAR>(const_cast<void*>(password)),
                                   static_cast<ULONG>(passwordLength),
                                   const_cast<PUCHAR>(salt),
                                   static_cast<ULONG>(saltLength),
                                   iterations,
                                   output,
                                   static_cast<ULONG>(outputLength),
                                   0);
    BCryptCloseAlgorithmProvider(prf, 0);
    return BCRYPT_SUCCESS(status);
}

AesGcmKey::~AesGcmKey() {
    if (key_) BCryptDestroyKey(key_);
    if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
}

bool AesGcmKey::Init(const uint8_t* key, size_t keyLength) {
    if (key_ || algorithm_) return false;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return false;

    status = BCryptSetProperty(algorithm_, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (!BCRYPT_SUCCESS(status)) return false;

    status = BCryptGenerateSymmetricKey(algorithm_, &key_, nullptr, 0,
                                        const_cast<PUCHAR>(key),
                                        static_cast<ULONG>(keyLength), 0);
    return BCRYPT_SUCCESS(status);
}

bool AesGcmKey::Encrypt(const uint8_t* nonce, size_t nonceLength,
                        const uint8_t* aad, size_t aadLength,
                        const uint8_t* input, size_t inputLength,
                        uint8_t* output, uint8_t* tag, size_t tagLength) {
    if (!key_ || !tag || tagLength != 16) return false;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = static_cast<ULONG>(nonceLength);
    info.pbAuthData = const_cast<PUCHAR>(aad);
    info.cbAuthData = static_cast<ULONG>(aadLength);
    info.pbTag = tag;
    info.cbTag = static_cast<ULONG>(tagLength);

    ULONG written = 0;
    NTSTATUS status = BCryptEncrypt(key_,
                                    const_cast<PUCHAR>(input),
                                    static_cast<ULONG>(inputLength),
                                    &info, nullptr, 0,
                                    output,
                                    static_cast<ULONG>(inputLength),
                                    &written, 0);
    return BCRYPT_SUCCESS(status) && written == inputLength;
}

bool AesGcmKey::Decrypt(const uint8_t* nonce, size_t nonceLength,
                        const uint8_t* aad, size_t aadLength,
                        const uint8_t* input, size_t inputLength,
                        const uint8_t* tag, size_t tagLength,
                        uint8_t* output) {
    if (!key_ || !tag || tagLength != 16) return false;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = static_cast<ULONG>(nonceLength);
    info.pbAuthData = const_cast<PUCHAR>(aad);
    info.cbAuthData = static_cast<ULONG>(aadLength);
    info.pbTag = const_cast<PUCHAR>(tag);
    info.cbTag = static_cast<ULONG>(tagLength);

    ULONG written = 0;
    NTSTATUS status = BCryptDecrypt(key_,
                                    const_cast<PUCHAR>(input),
                                    static_cast<ULONG>(inputLength),
                                    &info, nullptr, 0,
                                    output,
                                    static_cast<ULONG>(inputLength),
                                    &written, 0);
    return BCRYPT_SUCCESS(status) && written == inputLength;
}

static bool RsaImport(const wchar_t* blobType,
                      const uint8_t* blob, size_t blobLength,
                      BCRYPT_KEY_HANDLE* key) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return false;

    status = BCryptImportKeyPair(algorithm, nullptr, blobType, key,
                                 const_cast<PUCHAR>(blob),
                                 static_cast<ULONG>(blobLength), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return BCRYPT_SUCCESS(status);
}

bool RsaWrap(const uint8_t* publicBlob, size_t publicBlobLength,
             const uint8_t* data, size_t dataLength,
             std::vector<uint8_t>& output) {
    BCRYPT_KEY_HANDLE key = nullptr;
    if (!RsaImport(BCRYPT_RSAPUBLIC_BLOB, publicBlob, publicBlobLength, &key)) return false;

    BCRYPT_OAEP_PADDING_INFO padding{};
    padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    ULONG needed = 0;
    NTSTATUS status = BCryptEncrypt(key, const_cast<PUCHAR>(data),
                                    static_cast<ULONG>(dataLength),
                                    &padding, nullptr, 0, nullptr, 0, &needed,
                                    BCRYPT_PAD_OAEP);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(key);
        return false;
    }

    output.assign(needed, 0);
    ULONG written = 0;
    status = BCryptEncrypt(key, const_cast<PUCHAR>(data),
                           static_cast<ULONG>(dataLength),
                           &padding, nullptr, 0, output.data(), needed, &written,
                           BCRYPT_PAD_OAEP);
    BCryptDestroyKey(key);

    if (!BCRYPT_SUCCESS(status)) {
        output.clear();
        return false;
    }
    output.resize(written);
    return true;
}

bool RsaUnwrap(const uint8_t* privateBlob, size_t privateBlobLength,
               const uint8_t* data, size_t dataLength,
               std::vector<uint8_t>& output) {
    BCRYPT_KEY_HANDLE key = nullptr;
    if (!RsaImport(BCRYPT_RSAPRIVATE_BLOB, privateBlob, privateBlobLength, &key)) return false;

    BCRYPT_OAEP_PADDING_INFO padding{};
    padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    ULONG needed = 0;
    NTSTATUS status = BCryptDecrypt(key, const_cast<PUCHAR>(data),
                                    static_cast<ULONG>(dataLength),
                                    &padding, nullptr, 0, nullptr, 0, &needed,
                                    BCRYPT_PAD_OAEP);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(key);
        return false;
    }

    output.assign(needed, 0);
    ULONG written = 0;
    status = BCryptDecrypt(key, const_cast<PUCHAR>(data),
                           static_cast<ULONG>(dataLength),
                           &padding, nullptr, 0, output.data(), needed, &written,
                           BCRYPT_PAD_OAEP);
    BCryptDestroyKey(key);

    if (!BCRYPT_SUCCESS(status)) {
        output.clear();
        return false;
    }
    output.resize(written);
    return true;
}

void SecureZero(void* data, size_t length) {
    SecureZeroMemory(data, length);
}
