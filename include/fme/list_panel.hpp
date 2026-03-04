#pragma once

#include <windows.h>
#include <commctrl.h>

namespace fme::ui {

void InitListColumns(HWND list);
void ConfigureListViewMode(HWND list, bool icons_mode);

} // namespace fme::ui

