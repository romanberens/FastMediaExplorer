#include "fme/list_panel.hpp"

namespace fme::ui {

void InitListColumns(HWND list) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    col.cx = 320;
    col.pszText = const_cast<wchar_t*>(L"Name");
    col.iSubItem = 0;
    ListView_InsertColumn(list, 0, &col);

    col.cx = 80;
    col.pszText = const_cast<wchar_t*>(L"Type");
    col.iSubItem = 1;
    ListView_InsertColumn(list, 1, &col);

    col.cx = 120;
    col.pszText = const_cast<wchar_t*>(L"Extension");
    col.iSubItem = 2;
    ListView_InsertColumn(list, 2, &col);

    col.cx = 120;
    col.pszText = const_cast<wchar_t*>(L"Size");
    col.iSubItem = 3;
    ListView_InsertColumn(list, 3, &col);
}

void ConfigureListViewMode(HWND list, bool icons_mode) {
    LONG_PTR style = GetWindowLongPtrW(list, GWL_STYLE);
    style &= ~static_cast<LONG_PTR>(LVS_TYPEMASK);
    style |= icons_mode ? LVS_ICON : LVS_REPORT;
    SetWindowLongPtrW(list, GWL_STYLE, style);

    SetWindowPos(
        list,
        nullptr,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    ListView_SetView(list, icons_mode ? LV_VIEW_ICON : LV_VIEW_DETAILS);
}

} // namespace fme::ui

