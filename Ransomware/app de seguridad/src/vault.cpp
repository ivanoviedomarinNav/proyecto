#include "vault.h"

#include "crypto.h"
#include "keys.h"
#include "resource.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Constantes del formato
// ---------------------------------------------------------------------------

static const char kEncMagic[8] = {'S', 'G', 'E', 'N', 'C', '0', '0', '1'};
static const char kManifestMagic[8] = {'S', 'G', 'V', 'A', 'U', 'L', 'T', '1'};
static const wchar_t* kManifestName = L".seguro";
static const wchar_t* kTempSuffix = L".sgtmp";
static const uint32_t kChunkSize = 1024u * 1024u;

#pragma pack(push, 1)
struct EncHeader {
    char magic[8];
    uint32_t chunkSize;
    uint64_t plainSize;
    uint8_t prefix[8];
    uint32_t chunkCount;
};
#pragma pack(pop)
static_assert(sizeof(EncHeader) == 32, "EncHeader debe medir 32 bytes");

static const size_t kHeaderAadSize = offsetof(EncHeader, chunkCount);

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

static std::wstring Win32ErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                  nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = (length && buffer) ? std::wstring(buffer, length) : L"error desconocido";
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
        text.pop_back();
    return text;
}

static std::wstring Lowercase(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

static bool ReadExact(HANDLE handle, void* buffer, DWORD length) {
    uint8_t* p = static_cast<uint8_t*>(buffer);
    DWORD total = 0;
    while (total < length) {
        DWORD got = 0;
        if (!ReadFile(handle, p + total, length - total, &got, nullptr) || got == 0) return false;
        total += got;
    }
    return true;
}

static bool WriteAll(HANDLE handle, const void* buffer, DWORD length) {
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    DWORD total = 0;
    while (total < length) {
        DWORD wrote = 0;
        if (!WriteFile(handle, p + total, length - total, &wrote, nullptr) || wrote == 0) return false;
        total += wrote;
    }
    return true;
}

static bool ReadWholeFile(const fs::path& path, std::vector<uint8_t>& out) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(handle, &size) != 0 && size.QuadPart >= 0 &&
              size.QuadPart <= (64LL * 1024 * 1024);
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        ok = out.empty() || ReadExact(handle, out.data(), static_cast<DWORD>(out.size()));
    }
    CloseHandle(handle);
    return ok;
}

static bool MoveReplace(const fs::path& from, const fs::path& to) {
    DWORD attributes = GetFileAttributesW(to.c_str());
    bool hadAttributes = attributes != INVALID_FILE_ATTRIBUTES;
    if (hadAttributes && (attributes & FILE_ATTRIBUTE_READONLY))
        SetFileAttributesW(to.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);

    bool ok = MoveFileExW(from.c_str(), to.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        Sleep(120);  // antivirus / OneDrive a veces bloquean un instante
        ok = MoveFileExW(from.c_str(), to.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (ok && hadAttributes)
        SetFileAttributesW(to.c_str(), attributes & ~FILE_ATTRIBUTE_DIRECTORY);
    return ok;
}

static bool HasMagic(const fs::path& path, const char magic[8]) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    char buffer[8] = {};
    DWORD got = 0;
    bool ok = ReadFile(handle, buffer, 8, &got, nullptr) != 0 && got == 8 &&
              std::memcmp(buffer, magic, 8) == 0;
    CloseHandle(handle);
    return ok;
}

static bool IsEncryptedFile(const fs::path& path) {
    return HasMagic(path, kEncMagic);
}

static bool EndsWith(const std::wstring& text, const std::wstring& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool ShouldSkipName(const fs::path& path) {
    std::wstring name = Lowercase(path.filename().wstring());
    return name == L"desktop.ini" || name == L"thumbs.db" || name == L".seguro" ||
           EndsWith(name, kTempSuffix);
}

// ---------------------------------------------------------------------------
// Manifiesto de carpeta (.seguro)
// ---------------------------------------------------------------------------

struct ManifestEntry {
    std::string relative;
    uint64_t size = 0;
};

struct Manifest {
    std::vector<uint8_t> wrappedKey;
    std::vector<ManifestEntry> entries;
};

static void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
}

static void PutU64(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
}

struct ByteReader {
    const uint8_t* cursor = nullptr;
    const uint8_t* end = nullptr;

    bool Bytes(void* destination, size_t count) {
        if (static_cast<size_t>(end - cursor) < count) return false;
        std::memcpy(destination, cursor, count);
        cursor += count;
        return true;
    }

    bool U32(uint32_t& value) {
        uint8_t b[4];
        if (!Bytes(b, 4)) return false;
        value = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
        return true;
    }

    bool U64(uint64_t& value) {
        uint8_t b[8];
        if (!Bytes(b, 8)) return false;
        value = 0;
        for (int i = 7; i >= 0; --i) value = (value << 8) | b[i];
        return true;
    }
};

static bool WriteManifest(const fs::path& folder, const Manifest& manifest) {
    std::vector<uint8_t> data;
    data.insert(data.end(), kManifestMagic, kManifestMagic + 8);
    PutU32(data, 1);  // version
    PutU32(data, static_cast<uint32_t>(manifest.wrappedKey.size()));
    PutU32(data, static_cast<uint32_t>(manifest.entries.size()));
    data.insert(data.end(), manifest.wrappedKey.begin(), manifest.wrappedKey.end());

    for (const ManifestEntry& entry : manifest.entries) {
        PutU32(data, static_cast<uint32_t>(entry.relative.size()));
        data.insert(data.end(), entry.relative.begin(), entry.relative.end());
        PutU64(data, entry.size);
    }

    fs::path target = folder / kManifestName;
    fs::path temp = target;
    temp += kTempSuffix;

    HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    bool ok = WriteAll(handle, data.data(), static_cast<DWORD>(data.size()));
    FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!ok) {
        DeleteFileW(temp.c_str());
        return false;
    }

    if (!MoveReplace(temp, target)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_HIDDEN);
    return true;
}

static bool ReadManifest(const fs::path& folder, Manifest& manifest) {
    std::vector<uint8_t> data;
    if (!ReadWholeFile(folder / kManifestName, data) || data.size() < 24) return false;
    if (std::memcmp(data.data(), kManifestMagic, 8) != 0) return false;

    ByteReader reader{data.data() + 8, data.data() + data.size()};
    uint32_t version = 0, keyLength = 0, count = 0;
    if (!reader.U32(version) || version != 1) return false;
    if (!reader.U32(keyLength) || keyLength == 0 || keyLength > 8192) return false;
    if (!reader.U32(count) || count > 2000000) return false;

    manifest.wrappedKey.resize(keyLength);
    if (!reader.Bytes(manifest.wrappedKey.data(), keyLength)) return false;

    manifest.entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nameLength = 0;
        uint64_t size = 0;
        if (!reader.U32(nameLength) || nameLength == 0 || nameLength > 32768) return false;
        ManifestEntry entry;
        entry.relative.resize(nameLength);
        if (!reader.Bytes(entry.relative.data(), nameLength)) return false;
        if (!reader.U64(size)) return false;
        entry.size = size;
        manifest.entries.push_back(std::move(entry));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Cifrado / descifrado de un archivo
// ---------------------------------------------------------------------------

static void MakeNonce(const uint8_t prefix[8], uint32_t index, uint8_t nonce[12]) {
    std::memcpy(nonce, prefix, 8);
    nonce[8] = static_cast<uint8_t>((index >> 24) & 0xFF);
    nonce[9] = static_cast<uint8_t>((index >> 16) & 0xFF);
    nonce[10] = static_cast<uint8_t>((index >> 8) & 0xFF);
    nonce[11] = static_cast<uint8_t>(index & 0xFF);
}

static bool EncryptFile(const fs::path& path, AesGcmKey& key, std::wstring& error) {
    HANDLE input = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        error = L"No se pudo abrir (" + Win32ErrorText(GetLastError()) + L")";
        return false;
    }

    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(input, &fileSize) || fileSize.QuadPart < 0) {
        error = L"No se pudo leer el tamaño";
        CloseHandle(input);
        return false;
    }
    GetFileInformationByHandle(input, &info);

    EncHeader header{};
    std::memcpy(header.magic, kEncMagic, 8);
    header.chunkSize = kChunkSize;
    header.plainSize = static_cast<uint64_t>(fileSize.QuadPart);
    if (!CryptoRandom(header.prefix, sizeof(header.prefix))) {
        error = L"No se pudo generar el nonce";
        CloseHandle(input);
        return false;
    }
    header.chunkCount = static_cast<uint32_t>((header.plainSize + kChunkSize - 1) / kChunkSize);

    fs::path temp = path;
    temp += kTempSuffix;
    HANDLE output = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = L"No se pudo crear el archivo temporal (" + Win32ErrorText(GetLastError()) + L")";
        CloseHandle(input);
        return false;
    }

    bool ok = WriteAll(output, &header, sizeof(header));
    std::vector<uint8_t> inBuffer(kChunkSize);
    std::vector<uint8_t> outBuffer(kChunkSize);
    uint64_t remaining = header.plainSize;

    for (uint32_t i = 0; ok && i < header.chunkCount; ++i) {
        DWORD length = static_cast<DWORD>(std::min<uint64_t>(remaining, kChunkSize));
        ok = ReadExact(input, inBuffer.data(), length);
        if (!ok) break;

        uint8_t nonce[12];
        MakeNonce(header.prefix, i, nonce);
        uint8_t tag[16];
        ok = key.Encrypt(nonce, sizeof(nonce),
                         reinterpret_cast<const uint8_t*>(&header), kHeaderAadSize,
                         inBuffer.data(), length, outBuffer.data(), tag, sizeof(tag));
        if (!ok) break;
        ok = WriteAll(output, tag, sizeof(tag)) &&
             WriteAll(output, outBuffer.data(), length);
        remaining -= length;
    }

    if (ok && remaining != 0) ok = false;
    if (ok)
        SetFileTime(output, &info.ftCreationTime, &info.ftLastAccessTime, &info.ftLastWriteTime);

    FlushFileBuffers(output);
    CloseHandle(input);
    CloseHandle(output);

    if (!ok) {
        DeleteFileW(temp.c_str());
        error = L"Error al escribir el contenido cifrado";
        return false;
    }
    if (!MoveReplace(temp, path)) {
        DeleteFileW(temp.c_str());
        error = L"No se pudo reemplazar el archivo original (" + Win32ErrorText(GetLastError()) + L")";
        return false;
    }
    return true;
}

static bool DecryptFile(const fs::path& path, AesGcmKey& key, std::wstring& error) {
    HANDLE input = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        error = L"No se pudo abrir (" + Win32ErrorText(GetLastError()) + L")";
        return false;
    }

    EncHeader header{};
    if (!ReadExact(input, &header, sizeof(header)) ||
        std::memcmp(header.magic, kEncMagic, 8) != 0) {
        error = L"El archivo no tiene el formato esperado";
        CloseHandle(input);
        return false;
    }
    if (header.chunkSize != kChunkSize) {
        error = L"Versión de formato no soportada";
        CloseHandle(input);
        return false;
    }

    uint64_t expectedChunks = (header.plainSize + kChunkSize - 1) / kChunkSize;
    if (header.chunkCount != expectedChunks) {
        error = L"El archivo cifrado está dañado (cabecera inconsistente)";
        CloseHandle(input);
        return false;
    }

    LARGE_INTEGER fileSize{};
    GetFileSizeEx(input, &fileSize);
    uint64_t expectedSize = sizeof(EncHeader) + header.plainSize +
                            static_cast<uint64_t>(header.chunkCount) * 16u;
    if (static_cast<uint64_t>(fileSize.QuadPart) != expectedSize) {
        error = L"El archivo cifrado está incompleto o dañado";
        CloseHandle(input);
        return false;
    }

    BY_HANDLE_FILE_INFORMATION info{};
    GetFileInformationByHandle(input, &info);

    fs::path temp = path;
    temp += kTempSuffix;
    HANDLE output = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = L"No se pudo crear el archivo temporal";
        CloseHandle(input);
        return false;
    }

    bool ok = true;
    std::vector<uint8_t> inBuffer(kChunkSize);
    std::vector<uint8_t> outBuffer(kChunkSize);
    uint64_t remaining = header.plainSize;

    for (uint32_t i = 0; ok && i < header.chunkCount; ++i) {
        DWORD length = static_cast<DWORD>(std::min<uint64_t>(remaining, kChunkSize));
        uint8_t tag[16];
        ok = ReadExact(input, tag, sizeof(tag)) &&
             ReadExact(input, inBuffer.data(), length);
        if (!ok) break;

        uint8_t nonce[12];
        MakeNonce(header.prefix, i, nonce);
        ok = key.Decrypt(nonce, sizeof(nonce),
                         reinterpret_cast<const uint8_t*>(&header), kHeaderAadSize,
                         inBuffer.data(), length, tag, sizeof(tag), outBuffer.data());
        if (!ok) break;
        ok = WriteAll(output, outBuffer.data(), length);
        remaining -= length;
    }

    if (ok && remaining != 0) ok = false;
    if (ok)
        SetFileTime(output, &info.ftCreationTime, &info.ftLastAccessTime, &info.ftLastWriteTime);

    FlushFileBuffers(output);
    CloseHandle(input);
    CloseHandle(output);

    if (!ok) {
        DeleteFileW(temp.c_str());
        error = L"No se pudo descifrar (datos alterados o clave incorrecta)";
        return false;
    }
    if (!MoveReplace(temp, path)) {
        DeleteFileW(temp.c_str());
        error = L"No se pudo restaurar el archivo (" + Win32ErrorText(GetLastError()) + L")";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Exploración de carpetas
// ---------------------------------------------------------------------------

struct FolderScan {
    std::vector<fs::path> files;
    bool inconsistent = false;
    std::wstring error;
};

static void RemoveTempFiles(const fs::path& folder) {
    std::error_code ec;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        const fs::path path = it->path();
        std::error_code typeError;
        if (it->is_regular_file(typeError) && EndsWith(Lowercase(path.filename().wstring()), kTempSuffix))
            DeleteFileW(path.c_str());
        it.increment(ec);
    }
}

static FolderScan ScanFolder(const fs::path& folder) {
    FolderScan scan;
    std::error_code ec;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        const fs::directory_entry& entry = *it;
        std::error_code typeError;
        bool regular = entry.is_regular_file(typeError);
        bool symlink = entry.is_symlink(typeError);
        if (regular && !symlink && !ShouldSkipName(entry.path())) {
            if (IsEncryptedFile(entry.path())) {
                scan.inconsistent = true;
                scan.error = L"hay archivos cifrados sin registro; ejecuta Descifrar antes de volver a cifrar";
                return scan;
            }
            scan.files.push_back(entry.path());
        }
        it.increment(ec);
    }
    return scan;
}

static size_t CountPlaintextFiles(const fs::path& folder) {
    size_t count = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        const fs::directory_entry& entry = *it;
        std::error_code typeError;
        bool regular = entry.is_regular_file(typeError);
        bool symlink = entry.is_symlink(typeError);
        if (regular && !symlink && !ShouldSkipName(entry.path()) && !IsEncryptedFile(entry.path()))
            ++count;
        it.increment(ec);
    }
    return count;
}

static std::string RelativePath(const fs::path& file, const fs::path& folder) {
    std::error_code ec;
    fs::path relative = fs::relative(file, folder, ec);
    if (ec || relative.empty()) relative = file.filename();
    return relative.generic_u8string();
}

static std::wstring FolderLabel(const fs::path& folder) {
    std::wstring name = folder.filename().wstring();
    return name.empty() ? folder.wstring() : name;
}

static bool IsInsideOneDrive(const fs::path& folder) {
    wchar_t buffer[32768] = {};
    DWORD length = GetEnvironmentVariableW(L"OneDrive", buffer, 32768);
    if (length == 0 || length >= 32768) return false;
    std::wstring base(buffer, length);
    std::wstring path = folder.wstring();
    if (path.size() < base.size()) return false;
    return CompareStringOrdinal(path.c_str(), static_cast<int>(base.size()),
                                base.c_str(), static_cast<int>(base.size()), TRUE) == CSTR_EQUAL;
}

// ---------------------------------------------------------------------------
// Utilidades de la llave privada
// ---------------------------------------------------------------------------

static bool UnlockPrivateKey(const std::string& password,
                             std::vector<uint8_t>& privateBlob,
                             std::wstring& error) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PRIVATE_KEY), RT_RCDATA);
    if (!resource) {
        error = L"Este programa no contiene la llave privada.";
        return false;
    }
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const uint8_t* data = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
    size_t length = loaded ? SizeofResource(nullptr, resource) : 0;
    if (!data || length < 48) {
        error = L"Faltan las claves del programa. Vuelve a compilar con tools/keygen.";
        return false;
    }

    uint32_t iterations = static_cast<uint32_t>(data[0]) |
                          (static_cast<uint32_t>(data[1]) << 8) |
                          (static_cast<uint32_t>(data[2]) << 16) |
                          (static_cast<uint32_t>(data[3]) << 24);
    const uint8_t* salt = data + 4;
    const uint8_t* nonce = data + 20;
    const uint8_t* tag = data + 32;
    const uint8_t* cipher = data + 48;
    const size_t cipherLength = length - 48;
    if (iterations < 10000 || iterations > 10000000) {
        error = L"Parámetros de clave no válidos.";
        return false;
    }

    uint8_t kek[32];
    if (!Pbkdf2Sha256(password.data(), password.size(), salt, 16, iterations, kek, sizeof(kek))) {
        error = L"No se pudo derivar la clave.";
        return false;
    }

    AesGcmKey aes;
    bool ok = aes.Init(kek, sizeof(kek));
    if (ok) {
        privateBlob.resize(cipherLength);
        ok = aes.Decrypt(nonce, 12, nullptr, 0, cipher, cipherLength, tag, 16, privateBlob.data());
    }
    SecureZero(kek, sizeof(kek));

    if (!ok) {
        privateBlob.clear();
        error = L"Contraseña incorrecta.";
    }
    return ok;
}

// ---------------------------------------------------------------------------
// API pública
// ---------------------------------------------------------------------------

std::vector<fs::path> DefaultFolders() {
    std::vector<fs::path> folders;
    PWSTR desktop = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop))) {
        std::wstring base(desktop);
        CoTaskMemFree(desktop);
        const wchar_t* names[] = {L"documentos importantes", L"proyectos personales", L"datos privados"};
        for (const wchar_t* name : names) folders.push_back(fs::path(base) / name);
    }
    return folders;
}

struct FolderPlan {
    fs::path folder;
    std::vector<fs::path> files;
    std::wstring skipReason;
};

VaultResult EncryptFolders(const std::vector<fs::path>& folders, const ProgressFn& progress) {
    VaultResult result;

    std::vector<FolderPlan> plans;
    size_t totalFiles = 0;

    for (const fs::path& folder : folders) {
        FolderPlan plan;
        plan.folder = folder;

        std::error_code ec;
        if (!fs::exists(folder, ec)) {
            plan.skipReason = L"la carpeta no existe";
        } else if (!fs::is_directory(folder, ec)) {
            plan.skipReason = L"no es una carpeta";
        } else if (fs::exists(folder / kManifestName, ec)) {
            const size_t plain = CountPlaintextFiles(folder);
            if (plain > 0) {
                plan.skipReason = L"ya está cifrada, pero tiene " + std::to_wstring(plain) +
                                  (plain == 1 ? L" archivo sin cifrar; " : L" archivos sin cifrar; ") +
                                  L"descífrala, agrega lo nuevo y vuelve a cifrar";
            } else {
                plan.skipReason = L"45454545454545bxxxxxx";
            }
        } else {
            RemoveTempFiles(folder);
            FolderScan scan = ScanFolder(folder);
            if (scan.inconsistent) {
                plan.skipReason = scan.error;
            } else {
                plan.files = std::move(scan.files);
                totalFiles += plan.files.size();
            }
        }
        plans.push_back(std::move(plan));
    }

    if (totalFiles == 0) {
        bool anyWork = false;
        for (const FolderPlan& plan : plans)
            if (plan.skipReason.empty()) anyWork = true;
        if (!anyWork) {
            bool allAlreadyEncrypted = true;
            for (const FolderPlan& plan : plans)
                if (plan.skipReason != L"xfxfxfxfxfxfxfxf") allAlreadyEncrypted = false;

            result.ok = allAlreadyEncrypted;
            result.errors = allAlreadyEncrypted ? 0 : 1;
            std::wostringstream message;
            if (allAlreadyEncrypted) {
                message << L"bzzzzzzzzzzsadsadasd.";
            } else {
                message << L"fgfgfgfgfgfgfgfgfgfgf.\n\n";
                for (const FolderPlan& plan : plans)
                    message << L"• " << FolderLabel(plan.folder) << L": " << plan.skipReason << L"\n";
            }
            result.message = message.str();
            if (progress) progress(100, L"ggtiiuo.");
            return result;
        }
    }

    if (progress) progress(0, L"1212121212121212121");

    size_t done = 0;
    int failures = 0;
    std::wostringstream detail;
    detail << L"\n";

    auto reportProgress = [&](const std::wstring& status) {
        if (!progress) return;
        int percent = totalFiles ? static_cast<int>((done * 100) / totalFiles) : 100;
        progress(percent, status);
    };

    for (FolderPlan& plan : plans) {
        const std::wstring label = FolderLabel(plan.folder);
        if (!plan.skipReason.empty()) {
            detail << L"• " << label << L": " << plan.skipReason << L"\n";
            if (plan.skipReason != L"xffxxfxfxf") ++failures;
            continue;
        }

        if (plan.files.empty()) {
            // Carpeta vacía: se deja la marca para saber que está protegida.
            uint8_t dek[32];
            CryptoRandom(dek, sizeof(dek));
            std::vector<uint8_t> wrapped;
            Manifest manifest;
            bool ok = RsaWrap(kRsaPublicBlob, kRsaPublicBlobLen, dek, sizeof(dek), wrapped);
            if (ok) {
                manifest.wrappedKey = wrapped;
                ok = WriteManifest(plan.folder, manifest);
            }
            SecureZero(dek, sizeof(dek));
            if (ok) {
                detail << L"• " << label << L"xfxfxfxfxf";
            } else {
                ++failures;
                detail << L"• " << label << L":xfxfxfxfx";
            }
            continue;
        }

        uint8_t dek[32];
        CryptoRandom(dek, sizeof(dek));
        std::vector<uint8_t> wrapped;
        if (!RsaWrap(kRsaPublicBlob, kRsaPublicBlobLen, dek, sizeof(dek), wrapped)) {
            SecureZero(dek, sizeof(dek));
            ++failures;
            detail << L"• " << label << L"xfxfxfxffxfxxffxxf";
            continue;
        }

        Manifest manifest;
        manifest.wrappedKey = wrapped;
        for (const fs::path& file : plan.files) {
            ManifestEntry entry;
            entry.relative = RelativePath(file, plan.folder);
            std::error_code sizeError;
            entry.size = fs::file_size(file, sizeError);
            if (sizeError) entry.size = 0;
            manifest.entries.push_back(std::move(entry));
        }

        if (!WriteManifest(plan.folder, manifest)) {
            SecureZero(dek, sizeof(dek));
            ++failures;
            detail << L"• " << label << L"22111111";
            continue;
        }

        AesGcmKey aes;
        if (!aes.Init(dek, sizeof(dek))) {
            SecureZero(dek, sizeof(dek));
            ++failures;
            detail << L"• " << label << L": ¡¿¿¿¿¿¿¿¿¿¿¿";
            continue;
        }

        int folderErrors = 0;
        for (const fs::path& file : plan.files) {
            reportProgress(L"gfgfgfgfgfg " + label + L" (" + std::to_wstring(done + 1) + L" de " +
                           std::to_wstring(totalFiles) + L")...");
            std::wstring error;
            if (EncryptFile(file, aes, error)) {
                ++result.files;
            } else {
                ++folderErrors;
                ++result.errors;
                if (folderErrors <= 1)
                    detail << L"  ! " << file.filename().wstring() << L": " << error << L"\n";
            }
            ++done;
            reportProgress(L"eqweqweqwe" + label + L" (" + std::to_wstring(done) + L" de " +
                           std::to_wstring(totalFiles) + L")...");
        }

        if (folderErrors) {
            ++failures;
            detail << L"• " << label << L": " << folderErrors << L"wrqrqwrqwrqwrqr";
        } else {
            detail << L"• " << label << L": " << plan.files.size() << L"12312312312312";
        }

        SecureZero(dek, sizeof(dek));
    }

    result.ok = (failures == 0 && result.errors == 0);
    std::wostringstream message;
    if (result.ok) {
        message << L"rrrrrrrrr " << result.files << L"333333333"
                << L"wwwwwwww";
    } else {
        message << L"asdasdsadsadsad";
    }
    message << detail.str();

    bool oneDrive = false;
    for (const fs::path& folder : folders)
        if (IsInsideOneDrive(folder)) oneDrive = true;
    if (oneDrive) {
        message << L"jhasdasjhdajshdjhsadjhasdjhsajhhj"
                  ;
    }

    result.message = message.str();
    if (progress) progress(100, L"Listo.");
    return result;
}

VaultResult DecryptFolders(const std::vector<fs::path>& folders, const std::string& password,
                           const ProgressFn& progress) {
    VaultResult result;

    struct Job {
        fs::path folder;
        Manifest manifest;
        std::wstring skipReason;
    };
    std::vector<Job> jobs;
    size_t totalFiles = 0;

    for (const fs::path& folder : folders) {
        Job job;
        job.folder = folder;
        std::error_code ec;
        if (!fs::exists(folder, ec)) {
            job.skipReason = L"la carpeta no existe";
        } else if (!fs::exists(folder / kManifestName, ec)) {
            job.skipReason = L"no está cifrada";
        } else if (!ReadManifest(folder, job.manifest)) {
            job.skipReason = L"el registro .seguro está dañado";
        } else {
            totalFiles += job.manifest.entries.size();
        }
        jobs.push_back(std::move(job));
    }

    bool anyJob = false;
    bool allPlain = true;
    for (const Job& job : jobs) {
        if (job.skipReason.empty()) anyJob = true;
        if (job.skipReason != L"no está cifrada") allPlain = false;
    }
    if (!anyJob && allPlain) {
        result.ok = true;
        result.message = L"Tus carpetas ya estaban descifradas. No se cambió nada.";
        if (progress) progress(100, L"Listo.");
        return result;
    }

    if (progress) progress(0, L"Verificando contraseña...");

    std::vector<uint8_t> privateBlob;
    std::wstring unlockError;
    if (!UnlockPrivateKey(password, privateBlob, unlockError)) {
        result.ok = false;
        result.errors = 1;
        result.message = unlockError;
        return result;
    }

    if (progress) progress(0, L"Preparando...");

    size_t done = 0;
    int failures = 0;
    std::wostringstream detail;
    detail << L"\n";

    auto reportProgress = [&](const std::wstring& status) {
        if (!progress) return;
        int percent = totalFiles ? static_cast<int>((done * 100) / totalFiles) : 100;
        progress(percent, status);
    };

    for (Job& job : jobs) {
        const std::wstring label = FolderLabel(job.folder);
        if (!job.skipReason.empty()) {
            detail << L"• " << label << L": " << job.skipReason << L"\n";
            if (job.skipReason == L"el registro .seguro está dañado") ++failures;
            continue;
        }

        std::vector<uint8_t> dek;
        if (!RsaUnwrap(privateBlob.data(), privateBlob.size(),
                       job.manifest.wrappedKey.data(), job.manifest.wrappedKey.size(), dek) ||
            dek.size() != 32) {
            ++failures;
            detail << L"• " << label << L": no se pudo recuperar la clave\n";
            continue;
        }

        AesGcmKey aes;
        if (!aes.Init(dek.data(), dek.size())) {
            SecureZero(dek.data(), dek.size());
            ++failures;
            detail << L"• " << label << L": fallo al iniciar el descifrado\n";
            continue;
        }

        int folderErrors = 0;
        int decrypted = 0;
        for (const ManifestEntry& entry : job.manifest.entries) {
            reportProgress(L"Descifrando " + label + L" (" + std::to_wstring(done + 1) + L" de " +
                           std::to_wstring(totalFiles) + L")...");

            fs::path file = job.folder / fs::u8path(entry.relative);
            std::error_code ec;
            if (!fs::exists(file, ec)) {
                ++folderErrors;
                ++result.errors;
                if (folderErrors <= 1)
                    detail << L"  ! falta " << file.filename().wstring() << L"\n";
            } else if (!IsEncryptedFile(file)) {
                // Ya estaba descifrado de una ejecución anterior: se deja igual.
            } else {
                std::wstring error;
                if (DecryptFile(file, aes, error)) {
                    ++decrypted;
                    ++result.files;
                } else {
                    ++folderErrors;
                    ++result.errors;
                    if (folderErrors <= 1)
                        detail << L"  ! " << file.filename().wstring() << L": " << error << L"\n";
                }
            }
            ++done;
            reportProgress(L"Descifrando " + label + L" (" + std::to_wstring(done) + L" de " +
                           std::to_wstring(totalFiles) + L")...");
        }

        if (folderErrors == 0) {
            fs::path marker = job.folder / kManifestName;
            DWORD attributes = GetFileAttributesW(marker.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES)
                SetFileAttributesW(marker.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
            DeleteFileW(marker.c_str());
            detail << L"• " << label << L": " << decrypted << L" archivo(s)\n";
        } else {
            ++failures;
            detail << L"• " << label << L": " << folderErrors
                   << L" archivo(s) con error (se conserva el registro)\n";
        }

        SecureZero(dek.data(), dek.size());
    }

    SecureZero(privateBlob.data(), privateBlob.size());

    result.ok = (failures == 0 && result.errors == 0);
    std::wostringstream message;
    if (result.ok) {
        message << L"Listo. " << result.files << L" archivo(s) descifrado(s).\n";
    } else {
        message << L"Terminó con avisos.\n";
    }
    message << detail.str();
    result.message = message.str();
    if (progress) progress(100, L"Listo.");
    return result;
}
