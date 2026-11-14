#include "pch.h"
#include "OverlayWindow.h"
#include "OverlayWindow.xaml.h"
#include "ThemeHelper.h"

OverlayWindow g_OverlayWindow;

static winrt::ThemeSwitcherApp::OverlayWindow s_overlayWindow{ nullptr };

static winrt::ThemeSwitcherApp::implementation::OverlayWindow* GetImpl()
{
    if (!s_overlayWindow)
    {
        try
        {
            s_overlayWindow = winrt::make<winrt::ThemeSwitcherApp::implementation::OverlayWindow>();
        }
        catch (...)
        {
        }
    }
    if (s_overlayWindow)
    {
        return winrt::get_self<winrt::ThemeSwitcherApp::implementation::OverlayWindow>(s_overlayWindow);
    }
    return nullptr;
}

OverlayWindow::~OverlayWindow()
{
    Destroy();
}

bool OverlayWindow::Create()
{
    return (GetImpl() != nullptr);
}

void OverlayWindow::Show(int themeState, const std::wstring& target)
{
    auto p = GetImpl();
    if (p)
    {
        p->Show(themeState, target);
    }
}

void OverlayWindow::OnThemeChanged()
{
    // The overlay theme strictly matches the target theme being shifted to (m_displayedState),
    // and does not follow the Windows system setting.
}

void OverlayWindow::Hide()
{
    if (s_overlayWindow)
    {
        auto p = winrt::get_self<winrt::ThemeSwitcherApp::implementation::OverlayWindow>(s_overlayWindow);
        if (p) p->HideWindow();
    }
}

void OverlayWindow::Destroy()
{
    if (s_overlayWindow)
    {
        auto p = winrt::get_self<winrt::ThemeSwitcherApp::implementation::OverlayWindow>(s_overlayWindow);
        if (p) p->DestroyWindow();
        s_overlayWindow = nullptr;
    }
}

void OverlayWindow::ResetGraphicsResources()
{
    if (s_overlayWindow)
    {
        auto p = winrt::get_self<winrt::ThemeSwitcherApp::implementation::OverlayWindow>(s_overlayWindow);
        if (p) p->ResetGraphics();
    }
}
