#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <Microsoft.UI.Xaml.Window.h>

#include <shellapi.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <strsafe.h>
#include <shlobj.h>

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <memory>
