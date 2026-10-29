// keygen.cpp - genera el par RSA una sola vez.
// La clave publica se incrusta en Cifrar.exe.
// La clave privada se guarda cifrada con la contrasena en Descifrar.exe.
//
// Uso:  keygen.exe [contrasena]
//       (si se omite, la lee de la entrada estandar)
//
// IMPORTANTE: ejecutar una sola vez. Si se regeneran las claves, todo lo
// cifrado con las claves anteriores deja de poder descifrarse.

#include "../src/crypto.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

static const uint64_t kIterations = 1000000;
static const unsigned kRsaBits = 3072;

static bool WriteArray(std::ostream& out, const char* name,
                       const std::vector<uint8_t>& data) {
    out << "static const unsigned char " << name << "[] = {\n";
    for (size_t i = 0; i < data.size(); ++i) {
        if (i % 12 == 0) out << "    ";
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "0x%02X", data[i]);
        out << buffer;
        if (i + 1 != data.size()) out << ", ";
        if (i % 12 == 11 || i + 1 == data.size()) out << "\n";
    }
    out << "};\n";
    out << "static const unsigned int " << name << "Len = "
        << data.size() << "u;\n\n";
    return out.good();
}

static std::string ReadPassword(int argc, wchar_t** argv) {
    if (argc > 1) {
        std::wstring wide(argv[1]);
        int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(needed > 0 ? needed - 1 : 0, '\0');
        if (needed > 1)
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], needed, nullptr, nullptr);
        return utf8;
    }

    std::string line;
    std::getline(std::cin, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return line;
}

int wmain(int argc, wchar_t** argv) {
    std::string password = ReadPassword(argc, argv);
    if (password.size() < 8) {
        std::wcerr << L"La contrasena debe tener al menos 8 caracteres.\n";
        return 1;
    }

    {
        std::ifstream existing("..\\src\\keys.h");
        if (!existing.good()) existing.open("src\\keys.h");
        if (existing.good()) {
            std::wcerr << L"Ya existe src\\keys.h. No se regeneran las claves para no perder acceso.\n"
                          L"Si de verdad quieres regenerarlas, borra src\\keys.h y keys\\ a mano.\n";
            return 2;
        }
    }

    // 1. Generar par RSA.
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        std::wcerr << L"No se pudo abrir el proveedor RSA.\n";
        return 3;
    }
    status = BCryptGenerateKeyPair(algorithm, &key, kRsaBits, 0);
    if (BCRYPT_SUCCESS(status)) status = BCryptFinalizeKeyPair(key, 0);
    if (!BCRYPT_SUCCESS(status)) {
        std::wcerr << L"No se pudo generar el par RSA.\n";
        return 3;
    }

    ULONG publicLength = 0;
    BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &publicLength, 0);
    std::vector<uint8_t> publicBlob(publicLength);
    status = BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB,
                             publicBlob.data(), publicLength, &publicLength, 0);
    if (!BCRYPT_SUCCESS(status)) {
        std::wcerr << L"No se pudo exportar la clave publica.\n";
        return 3;
    }
    publicBlob.resize(publicLength);

    ULONG privateLength = 0;
    BCryptExportKey(key, nullptr, BCRYPT_RSAPRIVATE_BLOB, nullptr, 0, &privateLength, 0);
    std::vector<uint8_t> privateBlob(privateLength);
    status = BCryptExportKey(key, nullptr, BCRYPT_RSAPRIVATE_BLOB,
                             privateBlob.data(), privateLength, &privateLength, 0);
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!BCRYPT_SUCCESS(status)) {
        std::wcerr << L"No se pudo exportar la clave privada.\n";
        return 3;
    }
    privateBlob.resize(privateLength);

    // 2. Cifrar la clave privada con PBKDF2 + AES-256-GCM.
    uint8_t salt[16];
    uint8_t nonce[12];
    uint8_t tag[16];
    uint8_t kek[32];
    if (!CryptoRandom(salt, sizeof(salt)) ||
        !CryptoRandom(nonce, sizeof(nonce)) ||
        !Pbkdf2Sha256(password.data(), password.size(), salt, sizeof(salt),
                      kIterations, kek, sizeof(kek))) {
        std::wcerr << L"Fallo al derivar la clave.\n";
        return 4;
    }

    AesGcmKey aes;
    if (!aes.Init(kek, sizeof(kek))) {
        std::wcerr << L"Fallo al inicializar AES.\n";
        return 4;
    }

    std::vector<uint8_t> cipher(privateBlob.size());
    if (!aes.Encrypt(nonce, sizeof(nonce), nullptr, 0,
                     privateBlob.data(), privateBlob.size(),
                     cipher.data(), tag, sizeof(tag))) {
        std::wcerr << L"Fallo al cifrar la clave privada.\n";
        return 4;
    }
    SecureZero(kek, sizeof(kek));

    // Formato del blob: iteraciones(4) | salt(16) | nonce(12) | tag(16) | cifrado.
    std::vector<uint8_t> wrapped;
    for (int i = 0; i < 4; ++i) wrapped.push_back(static_cast<uint8_t>((kIterations >> (8 * i)) & 0xFF));
    wrapped.insert(wrapped.end(), salt, salt + sizeof(salt));
    wrapped.insert(wrapped.end(), nonce, nonce + sizeof(nonce));
    wrapped.insert(wrapped.end(), tag, tag + sizeof(tag));
    wrapped.insert(wrapped.end(), cipher.begin(), cipher.end());

    // 3. Escribir src/keys.h
    std::ofstream header("src/keys.h", std::ios::binary);
    if (!header.good()) header.open("..\\src\\keys.h", std::ios::binary);
    if (!header.good()) {
        std::wcerr << L"No se pudo crear src\\keys.h\n";
        return 5;
    }
    header << "// GENERADO AUTOMATICAMENTE POR build\\keygen.exe - NO EDITAR A MANO.\n"
              "// Solo contiene la clave PUBLICA. La clave privada cifrada vive en\n"
              "// keys\\private_wrapped.bin y se incrusta como recurso solo en Descifrar.exe.\n"
              "#pragma once\n\n";
    WriteArray(header, "kRsaPublicBlob", publicBlob);
    header.close();

    // 4. Copia de respaldo de las claves.
    CreateDirectoryW(L"keys", nullptr);
    {
        std::ofstream pub("keys/public.bin", std::ios::binary);
        pub.write(reinterpret_cast<const char*>(publicBlob.data()), publicBlob.size());
        std::ofstream priv("keys/private_wrapped.bin", std::ios::binary);
        priv.write(reinterpret_cast<const char*>(wrapped.data()), wrapped.size());

        std::ofstream note("keys/LEEME.txt", std::ios::binary);
        note << "COPIA DE SEGURIDAD DE LA LLAVE DEL PROGRAMA\r\n"
                "=========================================\r\n\r\n"
                "public.bin          -> clave publica (no es secreta)\r\n"
                "private_wrapped.bin -> clave privada cifrada con tu contrasena\r\n\r\n"
                "Guarda estos dos archivos junto con Descifrar.exe en un lugar seguro\r\n"
                "(USB, nube personal). Sin ellos o sin la contrasena no hay forma de\r\n"
                "recuperar las carpetas cifradas.\r\n";
    }

    std::wcout << L"Listo.\n"
               << L"  RSA: " << kRsaBits << L" bits\n"
               << L"  Clave publica: " << publicBlob.size() << L" bytes\n"
               << L"  Clave privada cifrada: " << wrapped.size() << L" bytes\n"
               << L"  PBKDF2: " << kIterations << L" iteraciones\n"
               << L"  src/keys.h generado.\n";
    return 0;
}
