#pragma once

#define IDI_APP_ICON                    101
#define IDI_ICON_SUN                    102
#define IDI_ICON_MOON                   103

// Tray context menu items
#define IDM_TRAY_TOGGLE                 1001
#define IDM_TRAY_SETTINGS               1002
#define IDM_TRAY_ABOUT                  1003
#define IDM_TRAY_EXIT                   1004

// Hotkey IDs
#define ID_HOTKEY_TOGGLE                2001

// IPC Event Names
#define EVENT_NAME_SHOW_SETTINGS        L"ClickDarkMode_Event_ShowSettings"
#define EVENT_NAME_EXIT                 L"ClickDarkMode_Event_Exit"
#define EVENT_NAME_TOGGLE               L"ClickDarkMode_Event_Toggle"

// Custom Windows Messages
#define WM_APP_TRAYMSG                  (WM_APP + 1)
#define WM_APP_THEME_CHANGED            (WM_APP + 2)

// Timer IDs
#define IDT_OVERLAY_HIDE                3001
#define IDT_OVERLAY_UNCLOAK             3002
