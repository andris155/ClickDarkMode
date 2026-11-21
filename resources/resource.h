#pragma once

#define IDI_APP_ICON                    101
#define IDI_ICON_SUN                    102
#define IDI_ICON_MOON                   103
#define IDI_ICON_SUN_WHITE              104
#define IDI_ICON_SUN_DARK               105
#define IDI_ICON_MOON_WHITE             106
#define IDI_ICON_MOON_DARK              107

// Tray context menu items
#define IDM_TRAY_TOGGLE                 1001
#define IDM_TRAY_SETTINGS               1002
#define IDM_TRAY_ABOUT                  1003
#define IDM_TRAY_EXIT                   1004
#define IDM_TRAY_DARK                   1005
#define IDM_TRAY_LIGHT                  1006

// Hotkey IDs
#define ID_HOTKEY_TOGGLE                2001

// IPC Event Names
#define EVENT_NAME_SHOW_SETTINGS        L"ClickDarkMode_Event_ShowSettings"
#define EVENT_NAME_EXIT                 L"ClickDarkMode_Event_Exit"
#define EVENT_NAME_TOGGLE               L"ClickDarkMode_Event_Toggle"
#define EVENT_NAME_DARK                 L"ClickDarkMode_Event_Dark"
#define EVENT_NAME_LIGHT                L"ClickDarkMode_Event_Light"

// Custom Windows Messages
#define WM_APP_TRAYMSG                  (WM_APP + 1)
#define WM_APP_THEME_CHANGED            (WM_APP + 2)

// Timer IDs
#define IDT_OVERLAY_HIDE                3001
#define IDT_OVERLAY_UNCLOAK             3002
