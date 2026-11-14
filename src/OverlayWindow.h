#pragma once
#include <windows.h>
#include <string>

class OverlayWindow
{
public:
    OverlayWindow() = default;
    ~OverlayWindow();

    bool Create();
    void Show(int themeState, const std::wstring& target);
    void OnThemeChanged();
    void Hide();
    void Destroy();
    void ResetGraphicsResources();
};

extern OverlayWindow g_OverlayWindow;
