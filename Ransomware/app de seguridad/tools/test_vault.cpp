// test_vault.cpp - prueba automatica del ciclo cifrar/descifrar.
// Uso: test_vault.exe <contrasena>

#include "../src/vault.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;

// Convierte a ASCII para que la consola no pierda el hilo con acentos.
static std::string Ascii(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        switch (c) {
            case L'\n': out += '\n'; break;
            case L'\r': out += '\r'; break;
            case L'\t': out += '\t'; break;
            case L'\u2022': out += '-'; break;
            case L'\u00e1': out += 'a'; break;
            case L'\u00e9': out += 'e'; break;
            case L'\u00ed': out += 'i'; break;
            case L'\u00f3': out += 'o'; break;
            case L'\u00fa': out += 'u'; break;
            case L'\u00c1': out += 'A'; break;
            case L'\u00c9': out += 'E'; break;
            case L'\u00cd': out += 'I'; break;
            case L'\u00d3': out += 'O'; break;
            case L'\u00da': out += 'U'; break;
            case L'\u00f1': out += 'n'; break;
            case L'\u00d1': out += 'N'; break;
            case L'\u00fc': out += 'u'; break;
            case L'\u00bf': out += '?'; break;
            case L'\u00a1': out += '!'; break;
            case L'\u2014': out += '-'; break;
            case L'\u00b7': out += '-'; break;
            default:
                out += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
                break;
        }
    }
    return out;
}

static void Check(bool condition, const wchar_t* name) {
    ++g_checks;
    std::cout << (condition ? "  [OK]   " : "  [FALLA] ") << Ascii(name) << "\n";
    if (!condition) ++g_failures;
}

static bool WriteBytes(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
}

static bool ReadBytes(const fs::path& path, std::vector<uint8_t>& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

static bool IsEncrypted(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    char magic[8] = {};
    if (!in) return false;
    in.read(magic, 8);
    return in.gcount() == 8 && std::memcmp(magic, "SGENC001", 8) == 0;
}

static std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                     nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed > 0 ? needed : 0), '\0');
    if (needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                            out.data(), needed, nullptr, nullptr);
    return out;
}

static void PrintProgress(int percent, const std::wstring& status) {
    if (percent % 25 == 0) std::cout << "   " << percent << "% " << Ascii(status) << "\n";
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cout << "Uso: test_vault.exe <contrasena>\n";
        return 2;
    }
    std::string password = ToUtf8(argv[1]);

    const fs::path base = fs::temp_directory_path() / L"seguro_selftest";
    std::error_code ec;
    fs::remove_all(base, ec);

    const fs::path documentos = base / L"documentos importantes";
    const fs::path proyectos = base / L"proyectos personales";
    const fs::path vacia = base / L"datos privados";
    fs::create_directories(documentos / L"sub carpeta");
    fs::create_directories(proyectos);
    fs::create_directories(vacia);

    std::vector<uint8_t> fileEmpty;
    std::vector<uint8_t> fileSmall = {'H', 'o', 'l', 'a', ' ', 'm', 'u', 'n', 'd', 'o', '\n'};
    std::vector<uint8_t> fileBig(3 * 1024 * 1024 + 123);
    for (size_t i = 0; i < fileBig.size(); ++i)
        fileBig[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
    std::vector<uint8_t> fileNested = {'a', 'n', 'i', 'd', 'a', 'd', 'o', '\n'};
    std::vector<uint8_t> fileCsv = {'a', ',', 'b', '\n', '1', ',', '2', '\n'};

    Check(WriteBytes(documentos / L"vacio.txt", fileEmpty), L"crear archivo vacio");
    Check(WriteBytes(documentos / L"saludo.txt", fileSmall), L"crear archivo pequeno");
    Check(WriteBytes(documentos / L"grande.bin", fileBig), L"crear archivo de 3 MB");
    Check(WriteBytes(documentos / L"sub carpeta" / L"anidado.txt", fileNested), L"crear archivo anidado");
    Check(WriteBytes(proyectos / L"datos.csv", fileCsv), L"crear CSV");

    const std::vector<fs::path> folders{documentos, proyectos, vacia};

    std::cout << "\n1) Cifrando...\n";
    VaultResult encrypted = EncryptFolders(folders, PrintProgress);
    std::cout << Ascii(encrypted.message) << "\n";
    Check(encrypted.ok, L"el cifrado termino sin errores");
    Check(encrypted.files == 5, L"se cifraron los 5 archivos");

    Check(fs::exists(documentos / L"saludo.txt"), L"las carpetas y nombres siguen existiendo");
    Check(IsEncrypted(documentos / L"saludo.txt"), L"el contenido quedo cifrado");
    Check(IsEncrypted(documentos / L"grande.bin"), L"el archivo grande quedo cifrado");
    Check(IsEncrypted(documentos / L"sub carpeta" / L"anidado.txt"), L"el archivo anidado quedo cifrado");
    Check(fs::exists(documentos / L".seguro"), L"se creo el registro .seguro");
    Check(fs::exists(vacia / L".seguro"), L"la carpeta vacia tambien quedo marcada");

    std::vector<uint8_t> afterEncrypt;
    ReadBytes(documentos / L"saludo.txt", afterEncrypt);
    Check(afterEncrypt.size() != fileSmall.size(), L"el tamano cambio al cifrar");

    std::cout << "\n1b) Carpeta ya cifrada con un archivo nuevo sin cifrar...\n";
    Check(WriteBytes(documentos / L"nuevo.txt", fileSmall), L"crear archivo nuevo sin cifrar");
    VaultResult mixed = EncryptFolders(folders, nullptr);
    std::cout << Ascii(mixed.message) << "\n";
    Check(!mixed.ok, L"se avisa de que hay archivos sin cifrar");
    Check(mixed.message.find(L"sin cifrar") != std::wstring::npos,
          L"el aviso menciona los archivos sin cifrar");
    Check(mixed.files == 0, L"no se vuelve a cifrar nada");
    Check(fs::exists(documentos / L".seguro"), L"el registro .seguro se conserva");
    Check(!IsEncrypted(documentos / L"nuevo.txt"), L"el archivo nuevo no se toca");
    Check(fs::remove(documentos / L"nuevo.txt"), L"borrar el archivo de prueba");

    std::cout << "\n2) Intentando con contrasena incorrecta...\n";
    VaultResult wrong = DecryptFolders(folders, "clave-equivocada", nullptr);
    std::cout << Ascii(wrong.message) << "\n";
    Check(!wrong.ok, L"la contrasena incorrecta es rechazada");
    Check(IsEncrypted(documentos / L"saludo.txt"), L"los archivos siguen cifrados");

    std::cout << "\n3) Descifrando con la contrasena correcta...\n";
    VaultResult decrypted = DecryptFolders(folders, password, PrintProgress);
    std::cout << Ascii(decrypted.message) << "\n";
    Check(decrypted.ok, L"el descifrado termino sin errores");
    Check(decrypted.files == 5, L"se descifraron los 5 archivos");

    std::vector<uint8_t> restored;
    Check(ReadBytes(documentos / L"saludo.txt", restored) && restored == fileSmall,
          L"el archivo pequeno volvio identico");
    Check(ReadBytes(documentos / L"grande.bin", restored) && restored == fileBig,
          L"el archivo de 3 MB volvio identico");
    Check(ReadBytes(documentos / L"vacio.txt", restored) && restored.empty(),
          L"el archivo vacio volvio identico");
    Check(ReadBytes(documentos / L"sub carpeta" / L"anidado.txt", restored) && restored == fileNested,
          L"el archivo anidado volvio identico");
    Check(!fs::exists(documentos / L".seguro"), L"el registro .seguro fue eliminado");
    Check(!fs::exists(documentos / L"saludo.txt.sgtmp"), L"no quedaron archivos temporales");

    std::cout << "\n4) Segundo ciclo cifrar/descifrar...\n";
    VaultResult again = EncryptFolders(folders, nullptr);
    Check(again.ok && again.files == 5, L"segundo cifrado correcto");
    VaultResult againBack = DecryptFolders(folders, password, nullptr);
    Check(againBack.ok && againBack.files == 5, L"segundo descifrado correcto");
    Check(ReadBytes(documentos / L"grande.bin", restored) && restored == fileBig,
          L"el archivo grande sigue identico tras el segundo ciclo");

    std::cout << "\n5) Cifrar dos veces seguidas...\n";
    VaultResult first = EncryptFolders(folders, nullptr);
    VaultResult second = EncryptFolders(folders, nullptr);
    Check(first.ok, L"primer cifrado correcto");
    Check(second.ok && second.files == 0, L"el segundo intento no vuelve a cifrar");
    VaultResult backAgain = DecryptFolders(folders, password, nullptr);
    Check(backAgain.ok && backAgain.files == 5, L"los archivos siguen descifrables");
    VaultResult nothing = DecryptFolders(folders, password, nullptr);
    Check(nothing.ok && nothing.files == 0, L"descifrar sin nada cifrado no rompe nada");

    std::cout << "\n----------------------------------------\n";
    std::cout << (g_failures == 0 ? "RESULTADO: TODO CORRECTO" : "RESULTADO: HAY FALLOS")
              << " (" << (g_checks - g_failures) << "/" << g_checks << ")\n";

    fs::remove_all(base, ec);
    return g_failures == 0 ? 0 : 1;
}
