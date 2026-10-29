#include <windows.h>

#include <string>

#include "ui.h"
#include "vault.h"

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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    AppConfig config;
    config.windowTitle = L"Descifrar mis carpetas";
    config.heading = L"Abrir mis carpetas";
    config.subtitle = L"Escribe tu contraseña y presiona Descifrar.";
    config.buttonText = L"Descifrar ahora";
    config.cue = L"Contraseña";
    config.footer = L"Guarda una copia de este programa en un lugar seguro";
    config.passwordField = true;

    return RunApp(instance, config,
                  [](const std::wstring& input, const ProgressFn& progress) {
                      std::string password = ToUtf8(input);
                      VaultResult result = DecryptFolders(DefaultFolders(), password, progress);
                      if (!password.empty())
                          SecureZeroMemory(password.data(), password.size());
                      return result;
                  });
}
