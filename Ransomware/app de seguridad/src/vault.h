#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using ProgressFn = std::function<void(int percent, const std::wstring& status)>;

struct VaultResult {
    bool ok = true;
    int files = 0;
    int errors = 0;
    std::wstring message;
};

// Las tres carpetas del Escritorio que maneja la aplicacion.
std::vector<std::filesystem::path> DefaultFolders();

VaultResult EncryptFolders(const std::vector<std::filesystem::path>& folders,
                           const ProgressFn& progress);

VaultResult DecryptFolders(const std::vector<std::filesystem::path>& folders,
                           const std::string& password,
                           const ProgressFn& progress);
