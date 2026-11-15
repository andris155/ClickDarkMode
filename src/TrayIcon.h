#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>

namespace TrayIcon {

bool Init(HWND hWnd);
void Remove();
void SyncUI();
void ShowContextMenu();
void CloseTrayMenu();
void HandleTrayMessage(WPARAM wParam, LPARAM lParam);

HICON LoadIconResource(const std::wstring& path, int iconNumber, int cx, int cy);
bool PickIcon(HWND hWndParent, std::wstring& inOutFile, int& inOutNumber);
bool IsBundledIcon(const std::wstring& path);

} // namespace TrayIcon
