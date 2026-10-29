#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "vault.h"

struct AppConfig {
    std::wstring windowTitle;
    std::wstring heading;
    std::wstring subtitle;
    std::wstring buttonText;
    std::wstring cue;
    std::wstring footer;
    std::wstring destinationPath;
    bool passwordField = false;
    bool showDestinationPopup = false;
    bool minimalLayout = false;
};

using AppAction = std::function<VaultResult(const std::wstring& input, const ProgressFn& progress)>;

int RunApp(HINSTANCE instance, const AppConfig& config, AppAction action);
