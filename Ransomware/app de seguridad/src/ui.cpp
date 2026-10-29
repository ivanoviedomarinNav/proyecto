#include "ui.h"

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <shlwapi.h>

#include <atomic>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Gdiplus;

namespace {

constexpr UINT kProgressMessage = WM_APP + 1;
constexpr UINT kDoneMessage = WM_APP + 2;
constexpr UINT_PTR kCountdownTimerId = 1;
constexpr int kCountdownStartSeconds = 14400;
constexpr int kButtonId = 1001;
constexpr int kEditId = 1002;
constexpr int kPopupOkId = 2001;
constexpr int kPopupCancelId = 2002;

const COLORREF kBackground = RGB(12, 12, 14);
const COLORREF kSurface = RGB(25, 24, 27);
const COLORREF kCardBorder = RGB(72, 24, 31);
const COLORREF kText = RGB(245, 242, 243);
const COLORREF kMuted = RGB(180, 165, 168);
const COLORREF kAccent = RGB(220, 20, 60);
const COLORREF kAccentHover = RGB(190, 15, 50);
const COLORREF kAccentPressed = RGB(150, 10, 38);
const COLORREF kAccentSoft = RGB(45, 16, 23);
const COLORREF kBorder = RGB(75, 45, 51);
const COLORREF kProgressTrack = RGB(57, 37, 41);
const COLORREF kPopupText = RGB(22, 28, 38);
const COLORREF kPopupBorder = RGB(214, 221, 232);
const COLORREF kPopupField = RGB(248, 249, 252);

struct ProgressPayload {
    int percent;
    std::wstring status;
};

struct DonePayload {
    VaultResult result;
};

struct AppState {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND edit = nullptr;
    HWND button = nullptr;
    UINT dpi = 96;
    bool busy = false;
    bool hover = false;
    bool pressed = false;
    int progress = 0;
    int countdownSeconds = kCountdownStartSeconds;
    std::wstring status;
    std::wstring inputHint;
    AppConfig config;
    AppAction action;

    HFONT fontHeading = nullptr;
    HFONT fontSubtitle = nullptr;
    HFONT fontEdit = nullptr;
    HFONT fontButton = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontTiny = nullptr;

    Image* image = nullptr;
    bool imageTried = false;

    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
};

AppState* StateFrom(HWND window) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

std::wstring FormatCountdown(int seconds) {
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int remainingSeconds = seconds % 60;
    return (hours < 10 ? L"0" : L"") + std::to_wstring(hours) + L":" +
           (minutes < 10 ? L"0" : L"") + std::to_wstring(minutes) + L":" +
           (remainingSeconds < 10 ? L"0" : L"") + std::to_wstring(remainingSeconds);
}

HFONT MakeFontForDpi(UINT dpi, int points, int weight) {
    return CreateFontW(-MulDiv(points, static_cast<int>(dpi), 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Segoe UI");
}

HFONT MakeFont(AppState* state, int points, int weight) {
    return MakeFontForDpi(state->dpi, points, weight);
}

void FillRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

void StrokeRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawCenteredText(HDC dc, const std::wstring& text, const RECT& rect, HFONT font,
                      COLORREF color, UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    COLORREF oldColor = SetTextColor(dc, color);
    int oldMode = SetBkMode(dc, TRANSPARENT);
    RECT copy = rect;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &copy, flags);
    SetBkMode(dc, oldMode);
    SetTextColor(dc, oldColor);
    SelectObject(dc, oldFont);
}

void DrawTextLeft(HDC dc, const std::wstring& text, const RECT& rect, HFONT font,
                  COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    COLORREF oldColor = SetTextColor(dc, color);
    int oldMode = SetBkMode(dc, TRANSPARENT);
    RECT copy = rect;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &copy,
              flags | DT_NOPREFIX);
    SetBkMode(dc, oldMode);
    SetTextColor(dc, oldColor);
    SelectObject(dc, oldFont);
}

std::wstring ExecutableDirectory() {
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    std::wstring path(buffer, length);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

Image* LoadImageFromResource(int resourceId) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return nullptr;
    HGLOBAL loaded = LoadResource(nullptr, resource);
    if (!loaded) return nullptr;
    void* data = LockResource(loaded);
    DWORD size = SizeofResource(nullptr, resource);
    if (!data || !size) return nullptr;

    IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(data), size);
    if (!stream) return nullptr;
    Image* image = Image::FromStream(stream, FALSE);
    stream->Release();
    return image;
}

Image* LoadImageFromFile(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    Image* image = Image::FromFile(path.c_str(), FALSE);
    if (image && image->GetLastStatus() != Ok) {
        delete image;
        return nullptr;
    }
    return image;
}

Image* EnsureImage(AppState* state) {
    if (state->imageTried) return state->image;
    state->imageTried = true;

    state->image = LoadImageFromResource(IDR_APP_IMAGE);
    if (!state->image) {
        std::wstring base = ExecutableDirectory();
        state->image = LoadImageFromFile(base + L"\\imagen.png");
        if (!state->image) state->image = LoadImageFromFile(base + L"\\imagen.jpg");
        if (!state->image) state->image = LoadImageFromFile(base + L"\\imagen.jpeg");
    }
    return state->image;
}

Rect FitRect(Image* image, const Rect& area) {
    if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) return area;
    REAL scaleX = static_cast<REAL>(area.Width) / image->GetWidth();
    REAL scaleY = static_cast<REAL>(area.Height) / image->GetHeight();
    REAL scale = scaleX < scaleY ? scaleX : scaleY;
    int width = static_cast<int>(image->GetWidth() * scale);
    int height = static_cast<int>(image->GetHeight() * scale);
    return Rect(area.X + (area.Width - width) / 2, area.Y + (area.Height - height) / 2, width, height);
}

void DrawPlaceholder(HDC dc, AppState* state, const RECT& area) {
    const int centerX = (area.left + area.right) / 2;
    const int lockWidth = state->Scale(58);
    const int lockHeight = state->Scale(46);
    const int bodyTop = (area.top + area.bottom) / 2 - state->Scale(6);
    RECT body{centerX - lockWidth / 2, bodyTop, centerX + lockWidth / 2, bodyTop + lockHeight};
    FillRoundedRect(dc, body, state->Scale(10), kAccentSoft);
    StrokeRoundedRect(dc, body, state->Scale(10), kCardBorder);

    HPEN pen = CreatePen(PS_SOLID, state->Scale(4), kAccent);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Arc(dc, centerX - state->Scale(15), bodyTop - state->Scale(28),
        centerX + state->Scale(15), bodyTop + state->Scale(22),
        centerX + state->Scale(15), bodyTop - state->Scale(14),
        centerX - state->Scale(15), bodyTop - state->Scale(14));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    if (!state->config.minimalLayout) {
        RECT text{area.left, body.bottom + state->Scale(14), area.right,
                  body.bottom + state->Scale(40)};
        DrawCenteredText(dc, L"Coloca aquí tu imagen", text, state->fontSmall, kMuted);
    }
}

// ---------------------------------------------------------------------------
// Ventanita previa: cualquiera de sus dos botones inicia la operación.
// ---------------------------------------------------------------------------

constexpr int kPopupClientWidth = 330;
constexpr int kPopupClientHeight = 130;

struct PopupState {
    HINSTANCE instance = nullptr;
    UINT dpi = 96;
    HWND window = nullptr;
    HWND ok = nullptr;
    HWND cancel = nullptr;
    bool startAction = false;
    HFONT fontLabel = nullptr;
    HFONT fontPath = nullptr;
    HFONT fontButton = nullptr;
    std::wstring path;

    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
};

void PaintDestination(PopupState* state, HDC dc, const RECT& client) {
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &client, background);
    DeleteObject(background);

    RECT label{state->Scale(16), state->Scale(12), client.right - state->Scale(16),
               state->Scale(12) + state->Scale(20)};
    DrawTextLeft(dc, L"Destination Folder", label, state->fontLabel, kPopupText);

    RECT field{state->Scale(16), state->Scale(38), client.right - state->Scale(16),
               state->Scale(38) + state->Scale(32)};
    FillRoundedRect(dc, field, state->Scale(6), kPopupField);
    StrokeRoundedRect(dc, field, state->Scale(6), kPopupBorder);

    RECT pathRect{field.left + state->Scale(10), field.top, field.right - state->Scale(10),
                  field.bottom};
    DrawTextLeft(dc, state->path, pathRect, state->fontPath, kPopupText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

LRESULT CALLBACK DestinationProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    PopupState* state = reinterpret_cast<PopupState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
        case WM_CREATE: {
            auto* created = reinterpret_cast<PopupState*>(
                reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
            created->window = window;

            const int buttonTop = created->Scale(88);
            const int buttonHeight = created->Scale(30);
            created->ok = CreateWindowExW(
                0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                created->Scale(146), buttonTop, created->Scale(74), buttonHeight, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPopupOkId)), created->instance,
                nullptr);
            created->cancel = CreateWindowExW(
                0, L"BUTTON", L"Cancelar",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                created->Scale(228), buttonTop, created->Scale(86), buttonHeight, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPopupCancelId)), created->instance,
                nullptr);
            SendMessageW(created->ok, WM_SETFONT,
                         reinterpret_cast<WPARAM>(created->fontButton), TRUE);
            SendMessageW(created->cancel, WM_SETFONT,
                         reinterpret_cast<WPARAM>(created->fontButton), TRUE);
            SetFocus(created->ok);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            if (state) {
                RECT client;
                GetClientRect(window, &client);
                PaintDestination(state, dc, client);
            }
            EndPaint(window, &paint);
            return 0;
        }

        case WM_CTLCOLORBTN:
            SetTextColor(reinterpret_cast<HDC>(wParam), kPopupText);
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if ((id == kPopupOkId || id == kPopupCancelId) && HIWORD(wParam) == BN_CLICKED) {
                if (state) state->startAction = true;
                DestroyWindow(window);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                if (state) state->startAction = true;
                DestroyWindow(window);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                DestroyWindow(window);
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool ShowDestinationPopup(HINSTANCE instance, UINT dpi, const std::wstring& path) {
    const wchar_t* className = L"SeguroDestinationWindow";

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DestinationProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = className;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON),
                                                      IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    if (!windowClass.hIcon) windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    PopupState state;
    state.instance = instance;
    state.dpi = dpi;
    state.path = path.empty() ? L"C:\\Program Files\\Carpetas Seguras" : path;
    state.fontLabel = MakeFontForDpi(dpi, 9, FW_SEMIBOLD);
    state.fontPath = MakeFontForDpi(dpi, 10, FW_NORMAL);
    state.fontButton = MakeFontForDpi(dpi, 9, FW_NORMAL);

    RECT desired{0, 0, state.Scale(kPopupClientWidth), state.Scale(kPopupClientHeight)};
    AdjustWindowRectEx(&desired, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    const int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;

    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, className, L"7-z",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y, width, height, nullptr, nullptr, instance,
        &state);
    if (window) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);

        MSG message;
        while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    } else {
        if (state.fontLabel) DeleteObject(state.fontLabel);
        if (state.fontPath) DeleteObject(state.fontPath);
        if (state.fontButton) DeleteObject(state.fontButton);
        return false;
    }

    const bool startAction = state.startAction;
    if (state.fontLabel) DeleteObject(state.fontLabel);
    if (state.fontPath) DeleteObject(state.fontPath);
    if (state.fontButton) DeleteObject(state.fontButton);
    return startAction;
}

void PaintWindow(AppState* state, HDC target, const RECT& client) {
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);

    HBRUSH background = CreateSolidBrush(kBackground);
    FillRect(memory, &client, background);
    DeleteObject(background);

    const bool minimal = state->config.minimalLayout;

    // Encabezado (solo en la ventana completa)
    if (!minimal) {
        RECT headingRect{state->Scale(30), state->Scale(24), client.right - state->Scale(30),
                         state->Scale(24) + state->Scale(38)};
        DrawCenteredText(memory, state->config.heading, headingRect, state->fontHeading, kText);
    }

    // Tarjeta de imagen
    RECT card = minimal
                    ? RECT{state->Scale(24), state->Scale(24), client.right - state->Scale(24),
                           state->Scale(348)}
                    : RECT{state->Scale(28), state->Scale(80), client.right - state->Scale(28),
                           state->Scale(352)};
    FillRoundedRect(memory, card, state->Scale(16), kSurface);
    StrokeRoundedRect(memory, card, state->Scale(16), kCardBorder);

    RECT imageArea{card.left + state->Scale(14), card.top + state->Scale(14),
                   card.right - state->Scale(14), card.bottom - state->Scale(14)};
    Image* image = EnsureImage(state);
    if (image) {
        Graphics graphics(memory);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        graphics.SetSmoothingMode(SmoothingModeHighQuality);
        Rect destination = FitRect(image, Rect(imageArea.left, imageArea.top,
                                               imageArea.right - imageArea.left,
                                               imageArea.bottom - imageArea.top));
        graphics.DrawImage(image, destination);
    } else {
        DrawPlaceholder(memory, state, imageArea);
    }

    if (!minimal) {
        // Subtítulo
        RECT subtitleRect{state->Scale(40), state->Scale(358), client.right - state->Scale(40),
                          state->Scale(358) + state->Scale(26)};
        DrawCenteredText(memory, state->config.subtitle, subtitleRect, state->fontSubtitle,
                         kMuted);
    }

    // Marco del campo de texto (el EDIT se dibuja encima)
    RECT editFrame = minimal
                         ? RECT{state->Scale(100), state->Scale(370), state->Scale(420),
                                state->Scale(410)}
                         : RECT{state->Scale(100), state->Scale(392), state->Scale(420),
                                state->Scale(432)};
    FillRoundedRect(memory, editFrame, state->Scale(9), kSurface);
    StrokeRoundedRect(memory, editFrame, state->Scale(9),
                      GetFocus() == state->edit ? kAccent : kBorder);

    if (!minimal) {
        // Estado
        RECT statusRect{state->Scale(40), state->Scale(512), client.right - state->Scale(40),
                        state->Scale(512) + state->Scale(32)};
        DrawCenteredText(memory, state->status, statusRect, state->fontSmall,
                         state->status == L"No se pudo completar." ? RGB(255, 92, 110) : kMuted,
                         DT_CENTER | DT_VCENTER | DT_WORDBREAK);

        // Barra de progreso
        RECT track{state->Scale(60), state->Scale(554), client.right - state->Scale(60),
                   state->Scale(554) + state->Scale(6)};
        FillRoundedRect(memory, track, state->Scale(3), kProgressTrack);
        if (state->busy || state->progress > 0) {
            int width =
                (track.right - track.left) * (state->progress < 0 ? 0 : state->progress) / 100;
            if (width > 0) {
                RECT fill{track.left, track.top, track.left + width, track.bottom};
                FillRoundedRect(memory, fill, state->Scale(3), kAccent);
            }
        }

        // Pie
        RECT footerRect{state->Scale(30), state->Scale(572), client.right - state->Scale(30),
                        state->Scale(572) + state->Scale(22)};
        DrawCenteredText(memory, state->config.footer, footerRect, state->fontTiny,
                         RGB(135, 120, 124));
    } else {
        RECT countdownRect{state->Scale(30), state->Scale(478),
                           client.right - state->Scale(30), state->Scale(514)};
        const std::wstring countdown = L"Tiempo restante: " +
                                       FormatCountdown(state->countdownSeconds);
        DrawCenteredText(memory, countdown, countdownRect, state->fontSmall,
                         RGB(255, 72, 92));
    }

    BitBlt(target, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);

    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

void DrawButton(AppState* state, const DRAWITEMSTRUCT* item) {
    if (item->CtlID != kButtonId) return;

    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    const bool enabled = (item->itemState & ODS_DISABLED) == 0;

    COLORREF fill = kAccent;
    if (!enabled)
        fill = RGB(82, 70, 74);
    else if (state->pressed)
        fill = kAccentPressed;
    else if (state->hover)
        fill = kAccentHover;

    FillRoundedRect(dc, rect, state->Scale(12), fill);

    RECT text = rect;
    DrawCenteredText(dc, state->config.buttonText, text, state->fontButton, kText);

    if (item->itemState & ODS_FOCUS) {
        RECT inner{rect.left + state->Scale(3), rect.top + state->Scale(3),
                   rect.right - state->Scale(3), rect.bottom - state->Scale(3)};
        StrokeRoundedRect(dc, inner, state->Scale(10), kText, 1);
    }
}

void StartWork(AppState* state) {
    if (state->busy) return;

    std::wstring input;
    if (state->edit) {
        int length = GetWindowTextLengthW(state->edit);
        input.resize(static_cast<size_t>(length) + 1);
        GetWindowTextW(state->edit, input.data(), length + 1);
        input.resize(static_cast<size_t>(length));
    }

    if (state->config.passwordField && input.empty()) {
        MessageBoxW(state->window, L"Escribe la contraseña para descifrar.", L"Falta la contraseña",
                    MB_OK | MB_ICONINFORMATION);
        SetFocus(state->edit);
        return;
    }
    if (!state->config.passwordField) input.clear();

    state->busy = true;
    state->progress = 0;
    state->status = L"Trabajando...";
    state->hover = false;
    state->pressed = false;
    EnableWindow(state->edit, FALSE);
    EnableWindow(state->button, FALSE);
    InvalidateRect(state->window, nullptr, FALSE);
    InvalidateRect(state->button, nullptr, FALSE);

    HWND window = state->window;
    AppAction action = state->action;
    std::thread([window, input, action]() {
        ProgressFn progress = [window](int percent, const std::wstring& status) {
            auto* payload = new ProgressPayload{percent, status};
            PostMessageW(window, kProgressMessage, 0, reinterpret_cast<LPARAM>(payload));
        };
        VaultResult result;
        try {
            result = action(input, progress);
        } catch (...) {
            result.ok = false;
            result.message = L"Ocurrió un error inesperado.";
        }
        if (!input.empty())
            SecureZeroMemory(const_cast<wchar_t*>(input.data()), input.size() * sizeof(wchar_t));
        auto* payload = new DonePayload{std::move(result)};
        PostMessageW(window, kDoneMessage, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

LRESULT CALLBACK EditSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                              UINT_PTR, DWORD_PTR reference) {
    AppState* state = reinterpret_cast<AppState*>(reference);
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        if (state && !state->busy) StartWork(state);
        return 0;
    }
    if (message == WM_CHAR && wParam == L'\r') return 0;
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK ButtonSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR reference) {
    AppState* state = reinterpret_cast<AppState*>(reference);
    if (!state) return DefSubclassProc(window, message, wParam, lParam);

    switch (message) {
        case WM_MOUSEMOVE:
            if (!state->hover) {
                state->hover = true;
                TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
                InvalidateRect(window, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            state->hover = false;
            state->pressed = false;
            InvalidateRect(window, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
            state->pressed = true;
            InvalidateRect(window, nullptr, FALSE);
            break;
        case WM_LBUTTONUP:
            state->pressed = false;
            InvalidateRect(window, nullptr, FALSE);
            break;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        default:
            break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    AppState* state = StateFrom(window);

    switch (message) {
        case WM_CREATE: {
            auto* created = reinterpret_cast<AppState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
            created->window = window;

            created->fontHeading = MakeFont(created, 22, FW_SEMIBOLD);
            created->fontSubtitle = MakeFont(created, 11, FW_NORMAL);
            created->fontEdit = MakeFont(created, 12, FW_NORMAL);
            created->fontButton = MakeFont(created, 12, FW_SEMIBOLD);
            created->fontSmall = MakeFont(created, 10, FW_NORMAL);
            created->fontTiny = MakeFont(created, 8, FW_NORMAL);

            created->edit = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                    (created->config.passwordField ? ES_PASSWORD : 0),
                created->Scale(101), created->Scale(created->config.minimalLayout ? 371 : 393),
                created->Scale(318), created->Scale(38), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), created->instance, nullptr);
            SendMessageW(created->edit, WM_SETFONT, reinterpret_cast<WPARAM>(created->fontEdit), TRUE);
            SendMessageW(created->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(created->Scale(10), created->Scale(10)));
            SendMessageW(created->edit, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(created->config.cue.c_str()));
            SetWindowSubclass(created->edit, EditSubclass, 1, reinterpret_cast<DWORD_PTR>(created));

            created->button = CreateWindowExW(
                0, L"BUTTON", created->config.buttonText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                created->Scale(160), created->Scale(created->config.minimalLayout ? 424 : 454),
                created->Scale(200), created->Scale(50), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kButtonId)), created->instance, nullptr);
            SendMessageW(created->button, WM_SETFONT, reinterpret_cast<WPARAM>(created->fontButton), TRUE);
            SetWindowSubclass(created->button, ButtonSubclass, 1, reinterpret_cast<DWORD_PTR>(created));

            SetFocus(created->edit);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            if (state) {
                RECT client;
                GetClientRect(window, &client);
                PaintWindow(state, dc, client);
            }
            EndPaint(window, &paint);
            return 0;
        }

        case WM_DRAWITEM:
            if (state) DrawButton(state, reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == kButtonId && HIWORD(wParam) == BN_CLICKED) {
                if (state) StartWork(state);
            } else if (LOWORD(wParam) == IDOK) {
                if (state) StartWork(state);
            }
            return 0;

        case WM_TIMER:
            if (state && wParam == kCountdownTimerId && state->countdownSeconds > 0) {
                --state->countdownSeconds;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kSurface);
            static HBRUSH editBrush = CreateSolidBrush(kSurface);
            return reinterpret_cast<LRESULT>(editBrush);
        }

        case kProgressMessage: {
            auto* payload = reinterpret_cast<ProgressPayload*>(lParam);
            if (state && payload) {
                state->progress = payload->percent;
                state->status = payload->status;
                InvalidateRect(window, nullptr, FALSE);
            }
            delete payload;
            return 0;
        }

        case kDoneMessage: {
            auto* payload = reinterpret_cast<DonePayload*>(lParam);
            if (state && payload) {
                state->busy = false;
                state->progress = 100;
                state->status = payload->result.ok ? L"Listo." : L"No se pudo completar.";
                EnableWindow(state->edit, TRUE);
                EnableWindow(state->button, TRUE);
                if (state->config.passwordField)
                    SetWindowTextW(state->edit, L"");
                InvalidateRect(window, nullptr, FALSE);
                InvalidateRect(state->button, nullptr, FALSE);
                SetFocus(state->edit);

                UINT icon = payload->result.ok ? MB_ICONINFORMATION : MB_ICONWARNING;
                MessageBoxW(window, payload->result.message.c_str(),
                            state->config.windowTitle.c_str(), MB_OK | icon);
            }
            delete payload;
            return 0;
        }

        case WM_CLOSE:
            if (state && state->busy) {
                MessageBoxW(window, L"Espera a que termine la operación.", L"En proceso",
                            MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            if (state) {
                KillTimer(window, kCountdownTimerId);
                if (state->fontHeading) DeleteObject(state->fontHeading);
                if (state->fontSubtitle) DeleteObject(state->fontSubtitle);
                if (state->fontEdit) DeleteObject(state->fontEdit);
                if (state->fontButton) DeleteObject(state->fontButton);
                if (state->fontSmall) DeleteObject(state->fontSmall);
                if (state->fontTiny) DeleteObject(state->fontTiny);
                delete state->image;
                state->image = nullptr;
            }
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int RunApp(HINSTANCE instance, const AppConfig& config, AppAction action) {
    GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &startup, nullptr) != Ok) return 1;

    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    AppState state;
    state.instance = instance;
    state.config = config;
    state.action = action;
    state.status = L"Listo para comenzar.";

    HDC screen = GetDC(nullptr);
    if (screen) {
        state.dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }
    if (state.dpi < 96) state.dpi = 96;

    bool startAction = false;
    if (config.showDestinationPopup) {
        startAction = ShowDestinationPopup(instance, state.dpi, config.destinationPath);
        if (!startAction) {
            GdiplusShutdown(token);
            return 0;
        }
    }

    const wchar_t* className = L"SeguroPersonalWindow";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = className;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON),
                                                      IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    if (!windowClass.hIcon)
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    RegisterClassExW(&windowClass);

    RECT desired{0, 0, state.Scale(520),
                 state.Scale(config.minimalLayout ? 520 : 612)};
    AdjustWindowRectEx(&desired, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                       FALSE, 0);
    int width = desired.right - desired.left;
    int height = desired.bottom - desired.top;

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;

    HWND window = CreateWindowExW(
        0, className, config.windowTitle.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        x, y, width, height, nullptr, nullptr, instance, &state);
    if (!window) {
        GdiplusShutdown(token);
        return 1;
    }

    if (config.minimalLayout && !SetTimer(window, kCountdownTimerId, 1000, nullptr)) {
        MessageBoxW(window, L"No se pudo iniciar el contador.", config.windowTitle.c_str(),
                    MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        GdiplusShutdown(token);
        return 1;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    if (startAction) StartWork(&state);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    GdiplusShutdown(token);
    return static_cast<int>(message.wParam);
}
