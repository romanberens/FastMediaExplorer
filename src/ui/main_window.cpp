#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <objidl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <condition_variable>
#include <list>

#include "fme/app.hpp"
#include "fme/duplicate_analysis.hpp"
#include "fme/file_indexer.hpp"
#include "fme/list_panel.hpp"
#include "fme/media_query.hpp"
#include "fme/thumb_service.hpp"

namespace {

constexpr wchar_t kWindowClassName[] = L"FME_MainWindowClass";
constexpr wchar_t kPreviewClassName[] = L"FME_PreviewPanelClass";
constexpr wchar_t kDateRangeClassName[] = L"FME_DateRangeClass";
constexpr wchar_t kWindowTitle[] = L"Fast Media Explorer";
constexpr int kTreeViewId = 1001;
constexpr int kListViewId = 1002;
constexpr int kSearchEditId = 1003;
constexpr int kTypeFilterId = 1004;
constexpr int kProgressId = 1005;
constexpr int kRecursiveCheckId = 1006;
constexpr int kDateFilterId = 1007;
constexpr int kQuickPresetId = 1008;
constexpr int kViewModeId = 1009;
constexpr int kCancelThumbsId = 1010;
constexpr int kListOverlayId = 1011;
constexpr UINT_PTR kSearchDebounceTimerId = 1;
constexpr UINT_PTR kOverlayTimerId = 2;
constexpr UINT kSearchDebounceMs = 220;
constexpr UINT kOverlayTickMs = 120;
constexpr int kDummyNodeParam = 1;
constexpr UINT kMsgScanCompleted = WM_APP + 1;
constexpr UINT kMsgThumbReady = WM_APP + 2;
constexpr UINT kMsgRenderChunk = WM_APP + 3;
constexpr UINT kMsgDupCompleted = WM_APP + 4;
constexpr WORD kCmdFocusSearch = 40001;
constexpr WORD kCmdRefreshFolder = 40002;
constexpr WORD kCmdOpenFolder = 40003;
constexpr WORD kCmdCtxOpen = 40101;
constexpr WORD kCmdCtxShowInFolder = 40102;
constexpr WORD kCmdCtxCopyPath = 40103;
constexpr WORD kCmdCtxDelete = 40104;
constexpr WORD kCmdCtxRefresh = 40105;
constexpr WORD kCmdToolsDupSizeOnly = 41001;
constexpr WORD kCmdToolsDupExactHash = 41002;
constexpr WORD kCmdToolsDupExactHashNameExt = 41003;
constexpr WORD kCmdToolsDupLastResult = 41004;
constexpr WORD kCmdToolsDupClear = 41005;
constexpr WORD kCmdToolsTopLargest = 41006;
constexpr WORD kCmdHelpAbout = 42001;
constexpr int kDateFromPickerId = 5001;
constexpr int kDateToPickerId = 5002;
constexpr int kDateOkButtonId = 5003;
constexpr int kDateCancelButtonId = 5004;

struct AsyncScanResult {
    std::uint64_t generation = 0;
    std::wstring folder;
    std::vector<fme::MediaFile> files;
};

struct AsyncDupResult {
    std::uint64_t generation = 0;
    fme::DuplicateMode mode = fme::DuplicateMode::SizeOnly;
    fme::DuplicateAnalysisResult result{};
};

struct DateRangeDialogState {
    HWND owner = nullptr;
    HWND picker_from = nullptr;
    HWND picker_to = nullptr;
    bool accepted = false;
    bool done = false;
    SYSTEMTIME from{};
    SYSTEMTIME to{};
};

enum class QuickPreset {
    None,
    Recent48h,
    LargeOver100MB,
    ImagesOnly,
    VideosOnly,
    PotentialDuplicates,
    LargestTop200
};

struct ThumbnailTask {
    std::uint64_t generation = 0;
    size_t data_index = 0;
    std::wstring path;
    fme::MediaType media_type = fme::MediaType::Other;
};

struct ThumbnailReady {
    std::uint64_t generation = 0;
    size_t data_index = 0;
    std::wstring path;
    HBITMAP hbitmap = nullptr;
};

struct ThumbnailCacheEntry {
    HBITMAP bitmap = nullptr;
    size_t bytes = 0;
};

enum class ViewMode {
    Details,
    Icons
};

struct MainWindowState {
    HWND tree = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HWND search_edit = nullptr;
    HWND quick_preset = nullptr;
    HWND view_mode = nullptr;
    HWND cancel_thumbs = nullptr;
    HWND list_overlay = nullptr;
    HWND type_filter = nullptr;
    HWND date_filter = nullptr;
    HWND progress = nullptr;
    HWND recursive_check = nullptr;
    HWND preview = nullptr;

    std::wstring root_path;
    std::wstring current_folder;
    std::wstring search_query;

    fme::MediaTypeFilter type_filter_value = fme::MediaTypeFilter::All;
    fme::DateFilter date_filter_value = fme::DateFilter::All;
    std::chrono::system_clock::time_point custom_date_from{};
    std::chrono::system_clock::time_point custom_date_to{};
    fme::SortField sort_field = fme::SortField::Name;
    bool sort_ascending = true;
    bool recursive_scan = false;
    QuickPreset quick_preset_value = QuickPreset::None;
    std::uintmax_t min_size_filter = 0;
    std::uintmax_t max_size_filter = (std::numeric_limits<std::uintmax_t>::max)();
    bool duplicates_by_size_only = false;
    size_t max_results = 0;
    ViewMode view_mode_value = ViewMode::Details;

    std::uint64_t scan_generation = 0;
    bool scanning = false;
    std::uint64_t render_generation = 0;
    size_t render_cursor = 0;
    size_t render_batch_size = 180;
    size_t render_batch_min = 40;
    size_t render_batch_max = 420;
    bool render_in_progress = false;
    size_t render_next_row = 0;
    std::vector<unsigned char> row_rendered;

    std::vector<fme::MediaFile> current_files;
    std::vector<size_t> visible_indices;
    int shown_count = 0;
    std::uintmax_t shown_bytes = 0;
    std::wstring selected_file_path;
    Gdiplus::Image* preview_image = nullptr;
    HIMAGELIST large_image_list = nullptr;
    int icon_generic = 0;
    int icon_image = 0;
    int icon_video = 0;
    int icon_audio = 0;
    std::unordered_map<std::wstring, int> thumbnail_icon_indices;
    std::unordered_map<std::wstring, ThumbnailCacheEntry> thumbnail_cache;
    std::list<std::wstring> thumbnail_lru;
    std::unordered_map<std::wstring, std::list<std::wstring>::iterator> thumbnail_lru_pos;
    size_t thumbnail_cache_bytes = 0;
    size_t thumbnail_cache_limit = 256ull * 1024ull * 1024ull;
    std::unordered_map<size_t, int> visible_row_by_data_index;
    std::uint64_t thumb_generation = 0;
    int thumbs_total = 0;
    int thumbs_done = 0;
    std::thread thumb_worker;
    std::mutex thumb_mutex;
    std::condition_variable thumb_cv;
    std::deque<ThumbnailTask> thumb_queue;
    std::unordered_set<std::wstring> thumb_queued_paths;
    bool thumb_worker_stop = false;
    int thumb_throttle_ms = 2;
    int thumb_recent_failures = 0;
    int overlay_frame = 0;
    ULONGLONG scan_started_tick = 0;
    ULONGLONG last_scan_ms = 0;
    ULONGLONG render_started_tick = 0;
    ULONGLONG last_render_ms = 0;
    std::uint64_t dup_generation = 0;
    bool duplicate_analysis_running = false;
    bool duplicate_filter_active = false;
    fme::DuplicateMode duplicate_mode = fme::DuplicateMode::SizeOnly;
    fme::DuplicateAnalysisResult duplicate_last_result{};
    std::unordered_set<size_t> duplicate_indices;
    bool suppress_tree_selection_scan = false;
};

void RefreshList(MainWindowState* state);
bool TryGetSelectedFileIndex(MainWindowState* state, size_t* out_index);
void SetStatusText(HWND status, const std::wstring& text);
void ApplyQuickPreset(MainWindowState* state, QuickPreset preset);

std::wstring FormatSize(std::uintmax_t bytes) {
    std::wostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);

    constexpr double kb = 1024.0;
    constexpr double mb = 1024.0 * 1024.0;
    constexpr double gb = 1024.0 * 1024.0 * 1024.0;

    if (bytes >= static_cast<std::uintmax_t>(gb)) {
        out << (static_cast<double>(bytes) / gb) << L" GB";
    } else if (bytes >= static_cast<std::uintmax_t>(mb)) {
        out << (static_cast<double>(bytes) / mb) << L" MB";
    } else if (bytes >= static_cast<std::uintmax_t>(kb)) {
        out << (static_cast<double>(bytes) / kb) << L" KB";
    } else {
        out.precision(0);
        out << bytes << L" B";
    }
    return out.str();
}

std::wstring FormatDateYmd(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local_tm{};
    localtime_s(&local_tm, &t);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04d-%02d-%02d", local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);
    return buffer;
}

std::wstring DateFilterLabel(const MainWindowState* state) {
    switch (state->date_filter_value) {
    case fme::DateFilter::Today:
        return L"Today";
    case fme::DateFilter::Last7Days:
        return L"Last 7 days";
    case fme::DateFilter::Last30Days:
        return L"Last 30 days";
    case fme::DateFilter::CustomRange:
        return L"Custom: " + FormatDateYmd(state->custom_date_from) + L" -> " + FormatDateYmd(state->custom_date_to);
    default:
        return L"All dates";
    }
}

std::wstring QuickPresetLabel(const MainWindowState* state) {
    switch (state->quick_preset_value) {
    case QuickPreset::Recent48h:
        return L"Recent (48h)";
    case QuickPreset::LargeOver100MB:
        return L"Large >100MB";
    case QuickPreset::ImagesOnly:
        return L"Images only";
    case QuickPreset::VideosOnly:
        return L"Videos only";
    case QuickPreset::PotentialDuplicates:
        return L"Potential duplicates";
    case QuickPreset::LargestTop200:
        return L"Top 200 largest";
    default:
        return L"None";
    }
}

std::wstring CustomRangeComboLabel(const MainWindowState* state) {
    return L"Custom: " + FormatDateYmd(state->custom_date_from) + L" -> " + FormatDateYmd(state->custom_date_to);
}

void UpdateCustomRangeComboItem(MainWindowState* state) {
    if (!state || !state->date_filter) {
        return;
    }
    constexpr int kCustomIndex = 4;
    const std::wstring label = CustomRangeComboLabel(state);
    SendMessageW(state->date_filter, CB_DELETESTRING, kCustomIndex, 0);
    SendMessageW(state->date_filter, CB_INSERTSTRING, kCustomIndex, reinterpret_cast<LPARAM>(label.c_str()));
}

std::wstring BuildReadyStatusText(const MainWindowState* state, int shown_count, std::uintmax_t shown_bytes) {
    std::wostringstream status_text;
    status_text << L"Folder: " << state->current_folder
                << L" | Files: " << shown_count << L"/" << state->current_files.size()
                << L" | Size: " << FormatSize(shown_bytes)
                << L" | Mode: " << (state->recursive_scan ? L"Recursive" : L"Current folder")
                << L" | Date: " << DateFilterLabel(state)
                << L" | Preset: " << QuickPresetLabel(state)
                << L" | Scan: " << state->last_scan_ms << L" ms"
                << L" | Render: " << state->last_render_ms << L" ms";
    if (state->view_mode_value == ViewMode::Icons && state->thumbs_total > 0) {
        status_text << L" | Thumbnails: " << state->thumbs_done << L"/" << state->thumbs_total;
    }
    if (state->duplicate_filter_active) {
        status_text << L" | Duplicates: ON";
    }
    return status_text.str();
}

std::wstring BuildRenderingStatusText(const MainWindowState* state) {
    std::wostringstream status_text;
    status_text << L"Rendering: " << state->render_cursor << L"/" << state->visible_indices.size()
                << L" | Folder: " << state->current_folder
                << L" | Thumbnails: " << state->thumbs_done << L"/" << state->thumbs_total;
    return status_text.str();
}

void UpdateProgressIndicator(MainWindowState* state) {
    if (!state || !state->progress) {
        return;
    }
    if (state->scanning) {
        ShowWindow(state->progress, SW_SHOW);
        SendMessageW(state->progress, PBM_SETMARQUEE, TRUE, 35);
        return;
    }
    SendMessageW(state->progress, PBM_SETMARQUEE, FALSE, 0);
    if (state->view_mode_value == ViewMode::Icons && state->thumbs_total > 0 && state->thumbs_done < state->thumbs_total) {
        ShowWindow(state->progress, SW_SHOW);
        SendMessageW(state->progress, PBM_SETRANGE32, 0, static_cast<LPARAM>(state->thumbs_total));
        SendMessageW(state->progress, PBM_SETPOS, static_cast<WPARAM>(state->thumbs_done), 0);
    } else {
        ShowWindow(state->progress, SW_HIDE);
    }
}

std::wstring BuildOverlayText(const MainWindowState* state) {
    static constexpr wchar_t kFrames[] = L"|/-\\";
    const wchar_t frame = kFrames[state->overlay_frame % 4];
    if (state->scanning) {
        return std::wstring(L"Scanning folder... ") + frame;
    }
    if (state->duplicate_analysis_running) {
        return std::wstring(L"Analyzing duplicates... ") + frame;
    }
    if (state->view_mode_value == ViewMode::Icons && state->thumbs_done < state->thumbs_total) {
        return std::wstring(L"Rendering thumbnails... ") + frame +
            L"  " + std::to_wstring(state->thumbs_done) + L"/" + std::to_wstring(state->thumbs_total);
    }
    return std::wstring(L"Rendering thumbnails... ") + frame;
}

void UpdateListOverlay(MainWindowState* state, HWND hwnd) {
    if (!state || !state->list_overlay) {
        return;
    }
    (void)hwnd;
    const bool large_folder = state->current_files.size() > 150 || state->visible_indices.size() > 150;
    const bool thumbs_pending = (state->view_mode_value == ViewMode::Icons) && (state->thumbs_done < state->thumbs_total);
    const bool busy = state->scanning || state->render_in_progress || thumbs_pending;
    const bool show = busy && (state->scanning || large_folder);
    if (show) {
        const std::wstring text = BuildOverlayText(state);
        SetWindowTextW(state->list_overlay, text.c_str());
        ShowWindow(state->list_overlay, SW_SHOW);
        SetWindowPos(state->list_overlay, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetTimer(GetParent(state->list), kOverlayTimerId, kOverlayTickMs, nullptr);
    } else {
        KillTimer(GetParent(state->list), kOverlayTimerId);
        ShowWindow(state->list_overlay, SW_HIDE);
    }
}

int ComputeThumbnailThrottleMs(MainWindowState* state) {
    if (!state) {
        return 2;
    }
    size_t queued = 0;
    {
        std::lock_guard<std::mutex> lock(state->thumb_mutex);
        queued = state->thumb_queue.size();
    }
    // Adaptive throttle:
    // high queue -> keep throughput high, low queue -> reduce disk pressure.
    if (queued > 200) return 0;
    if (queued > 80) return 1;
    if (queued > 25) return 2;
    return 4;
}

std::wstring DuplicateModeText(fme::DuplicateMode mode) {
    switch (mode) {
    case fme::DuplicateMode::ExactHash:
        return L"Exact hash";
    case fme::DuplicateMode::ExactHashNameExt:
        return L"Exact hash + name/ext";
    default:
        return L"Size only";
    }
}

std::wstring BuildAboutText() {
    return
        L"Fast Media Explorer v0.2 alpha\n\n"
        L"Tworca: Roman Berens\n"
        L"Firma: OneNetworks Roman Berens\n"
        L"Strona: https://www.onenetworks.pl\n\n"
        L"Opis:\n"
        L"Lekkie narzedzie desktop do szybkiego przegladania,\n"
        L"filtrowania i analizy plikow multimedialnych.\n\n"
        L"Inspiracja i kierunek:\n"
        L"OneNetworks - infrastruktura cyfrowa, automatyzacja\n"
        L"i bezpieczenstwo z podejsciem praktycznym.\n\n"
        L"Glowne funkcje:\n"
        L"- TreeView + ListView (Details/Icons)\n"
        L"- wyszukiwanie, filtry typu i daty (w tym custom range)\n"
        L"- skanowanie asynchroniczne i renderowanie porcjami\n"
        L"- miniatury obrazow/wideo z cache i kolejka worker\n"
        L"- analiza duplikatow: size-only, exact hash,\n"
        L"  exact hash + name/ext\n"
        L"- szybkie presety i status wydajnosci\n\n"
        L"Licencja: MIT License\n"
        L"Projekt rozwijany iteracyjnie (alpha).";
}

void ClearDuplicateFilter(MainWindowState* state) {
    state->duplicate_filter_active = false;
    state->duplicate_indices.clear();
}

void ApplyDuplicateResultFilter(MainWindowState* state) {
    state->duplicate_indices.clear();
    for (const auto& g : state->duplicate_last_result.groups) {
        for (size_t idx : g.indices) {
            state->duplicate_indices.insert(idx);
        }
    }
    state->duplicate_filter_active = !state->duplicate_indices.empty();
}

void BeginDuplicateAnalysis(MainWindowState* state, HWND hwnd, fme::DuplicateMode mode) {
    if (!state || state->current_files.empty()) {
        MessageBoxW(hwnd, L"No files to analyze in current folder scope.", L"Duplicates", MB_ICONINFORMATION);
        return;
    }
    state->duplicate_mode = mode;
    state->duplicate_analysis_running = true;
    ++state->dup_generation;
    UpdateListOverlay(state, hwnd);
    SetStatusText(state->status, L"Analyzing duplicates (" + DuplicateModeText(mode) + L") ...");

    const std::uint64_t generation = state->dup_generation;
    const auto files = state->current_files;
    const auto min_size = state->min_size_filter;

    std::thread([hwnd, generation, mode, files, min_size]() mutable {
        fme::DuplicateAnalysisOptions options{};
        options.mode = mode;
        options.min_size = min_size;
        auto* payload = new AsyncDupResult();
        payload->generation = generation;
        payload->mode = mode;
        payload->result = fme::AnalyzeDuplicates(files, options);
        if (!PostMessageW(hwnd, kMsgDupCompleted, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

HMENU BuildAppMenu() {
    HMENU main = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    HMENU tools = CreatePopupMenu();
    HMENU dup = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    if (!main || !file || !view || !tools || !dup || !help) {
        return main;
    }

    AppendMenuW(file, MF_STRING, kCmdOpenFolder, L"&Open folder...\tCtrl+O");
    AppendMenuW(file, MF_STRING, kCmdRefreshFolder, L"&Refresh\tF5");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, SC_CLOSE, L"E&xit");

    AppendMenuW(view, MF_STRING, kCmdFocusSearch, L"&Search\tCtrl+F");

    AppendMenuW(dup, MF_STRING, kCmdToolsDupSizeOnly, L"&Quick (size only)");
    AppendMenuW(dup, MF_STRING, kCmdToolsDupExactHash, L"&Exact (hash)");
    AppendMenuW(dup, MF_STRING, kCmdToolsDupExactHashNameExt, L"Exact + &name/ext");
    AppendMenuW(dup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(dup, MF_STRING, kCmdToolsDupLastResult, L"&Last result");
    AppendMenuW(dup, MF_STRING, kCmdToolsDupClear, L"&Clear result");

    AppendMenuW(tools, MF_POPUP, reinterpret_cast<UINT_PTR>(dup), L"&Duplicate analysis");
    AppendMenuW(tools, MF_STRING, kCmdToolsTopLargest, L"Top 200 &largest");

    AppendMenuW(help, MF_STRING, kCmdHelpAbout, L"&About");

    AppendMenuW(main, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(main, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
    AppendMenuW(main, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"&Tools");
    AppendMenuW(main, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    return main;
}

bool HasSubfolders(const std::filesystem::path& path) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            return false;
        }
        std::error_code entry_ec;
        if (entry.is_directory(entry_ec) && !entry_ec) {
            return true;
        }
    }
    return false;
}

HTREEITEM InsertFolderNode(HWND tree, HTREEITEM parent, const std::filesystem::path& path) {
    auto* full_path = new std::wstring(path.wstring());
    std::wstring label = path.filename().empty() ? path.wstring() : path.filename().wstring();

    TVINSERTSTRUCTW insert{};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM;
    insert.item.pszText = const_cast<wchar_t*>(label.c_str());
    insert.item.lParam = reinterpret_cast<LPARAM>(full_path);

    const HTREEITEM item = TreeView_InsertItem(tree, &insert);
    if (!item) {
        delete full_path;
        return nullptr;
    }

    if (HasSubfolders(path)) {
        TVINSERTSTRUCTW dummy{};
        dummy.hParent = item;
        dummy.hInsertAfter = TVI_LAST;
        dummy.item.mask = TVIF_TEXT | TVIF_PARAM;
        dummy.item.pszText = const_cast<wchar_t*>(L"");
        dummy.item.lParam = kDummyNodeParam;
        TreeView_InsertItem(tree, &dummy);
    }

    return item;
}

void PopulateFolderChildren(HWND tree, HTREEITEM parent_item, const std::filesystem::path& parent_path) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(parent_path, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            continue;
        }
        std::error_code entry_ec;
        if (!entry.is_directory(entry_ec) || entry_ec) {
            continue;
        }
        InsertFolderNode(tree, parent_item, entry.path());
    }
}

void EnsureExpandedItemIsLoaded(HWND tree, HTREEITEM item) {
    HTREEITEM first_child = TreeView_GetChild(tree, item);
    if (!first_child) {
        return;
    }

    TVITEMW child_info{};
    child_info.mask = TVIF_PARAM;
    child_info.hItem = first_child;
    if (!TreeView_GetItem(tree, &child_info)) {
        return;
    }

    if (child_info.lParam != kDummyNodeParam) {
        return;
    }

    TreeView_DeleteItem(tree, first_child);

    TVITEMW item_info{};
    item_info.mask = TVIF_PARAM;
    item_info.hItem = item;
    if (!TreeView_GetItem(tree, &item_info) || item_info.lParam <= kDummyNodeParam) {
        return;
    }

    const auto* full_path = reinterpret_cast<std::wstring*>(item_info.lParam);
    PopulateFolderChildren(tree, item, *full_path);
}

void FreeTreeItemData(HWND tree, HTREEITEM item) {
    while (item) {
        HTREEITEM child = TreeView_GetChild(tree, item);
        if (child) {
            FreeTreeItemData(tree, child);
        }

        TVITEMW data{};
        data.mask = TVIF_PARAM;
        data.hItem = item;
        if (TreeView_GetItem(tree, &data) && data.lParam > kDummyNodeParam) {
            auto* path = reinterpret_cast<std::wstring*>(data.lParam);
            delete path;
        }

        item = TreeView_GetNextSibling(tree, item);
    }
}

void InitListColumns(HWND list) {
    fme::ui::InitListColumns(list);
}

void InitListIcons(MainWindowState* state) {
    if (state->large_image_list) {
        ImageList_Destroy(state->large_image_list);
        state->large_image_list = nullptr;
    }
    state->thumbnail_icon_indices.clear();

    state->large_image_list = ImageList_Create(64, 64, ILC_COLOR32 | ILC_MASK, 4, 4);
    if (!state->large_image_list) {
        return;
    }

    state->icon_generic = ImageList_AddIcon(state->large_image_list, LoadIconW(nullptr, IDI_APPLICATION));
    state->icon_image = ImageList_AddIcon(state->large_image_list, LoadIconW(nullptr, IDI_INFORMATION));
    state->icon_video = ImageList_AddIcon(state->large_image_list, LoadIconW(nullptr, IDI_SHIELD));
    state->icon_audio = ImageList_AddIcon(state->large_image_list, LoadIconW(nullptr, IDI_WARNING));
    ListView_SetImageList(state->list, state->large_image_list, LVSIL_NORMAL);
}

int IconIndexForMedia(const MainWindowState* state, fme::MediaType type) {
    switch (type) {
    case fme::MediaType::Image:
        return state->icon_image;
    case fme::MediaType::Video:
        return state->icon_video;
    case fme::MediaType::Audio:
        return state->icon_audio;
    default:
        return state->icon_generic;
    }
}

size_t EstimateBitmapBytes(HBITMAP hbitmap) {
    if (!hbitmap) {
        return 0;
    }
    BITMAP bm{};
    if (!GetObjectW(hbitmap, sizeof(bm), &bm)) {
        return 64ull * 64ull * 4ull;
    }
    const size_t width = static_cast<size_t>(std::max<LONG>(1, bm.bmWidth));
    const size_t height = static_cast<size_t>(std::max<LONG>(1, bm.bmHeight));
    const size_t bpp = static_cast<size_t>(std::max<WORD>(32, bm.bmBitsPixel));
    return width * height * (bpp / 8);
}

void TouchThumbnailLru(MainWindowState* state, const std::wstring& path) {
    const auto pos = state->thumbnail_lru_pos.find(path);
    if (pos != state->thumbnail_lru_pos.end()) {
        state->thumbnail_lru.erase(pos->second);
        state->thumbnail_lru_pos.erase(pos);
    }
    state->thumbnail_lru.push_front(path);
    state->thumbnail_lru_pos[path] = state->thumbnail_lru.begin();
}

void EvictThumbnailCache(MainWindowState* state) {
    while (state->thumbnail_cache_bytes > state->thumbnail_cache_limit && !state->thumbnail_lru.empty()) {
        const std::wstring victim = state->thumbnail_lru.back();
        state->thumbnail_lru.pop_back();
        state->thumbnail_lru_pos.erase(victim);

        const auto it = state->thumbnail_cache.find(victim);
        if (it == state->thumbnail_cache.end()) {
            continue;
        }
        if (it->second.bitmap) {
            DeleteObject(it->second.bitmap);
        }
        if (state->thumbnail_cache_bytes >= it->second.bytes) {
            state->thumbnail_cache_bytes -= it->second.bytes;
        } else {
            state->thumbnail_cache_bytes = 0;
        }
        state->thumbnail_cache.erase(it);
        state->thumbnail_icon_indices.erase(victim);
    }
}

void PutThumbnailInCache(MainWindowState* state, const std::wstring& path, HBITMAP hbitmap) {
    if (!hbitmap) {
        return;
    }
    const size_t bytes = EstimateBitmapBytes(hbitmap);

    const auto existing = state->thumbnail_cache.find(path);
    if (existing != state->thumbnail_cache.end()) {
        if (existing->second.bitmap) {
            DeleteObject(existing->second.bitmap);
        }
        if (state->thumbnail_cache_bytes >= existing->second.bytes) {
            state->thumbnail_cache_bytes -= existing->second.bytes;
        } else {
            state->thumbnail_cache_bytes = 0;
        }
        existing->second.bitmap = hbitmap;
        existing->second.bytes = bytes;
    } else {
        state->thumbnail_cache[path] = ThumbnailCacheEntry{ hbitmap, bytes };
    }

    state->thumbnail_cache_bytes += bytes;
    TouchThumbnailLru(state, path);
    EvictThumbnailCache(state);
}

bool TryGetCachedThumbnailIcon(MainWindowState* state, const std::wstring& path, int* out_icon_index) {
    const auto existing_icon = state->thumbnail_icon_indices.find(path);
    if (existing_icon != state->thumbnail_icon_indices.end()) {
        *out_icon_index = existing_icon->second;
        TouchThumbnailLru(state, path);
        return true;
    }

    const auto cached = state->thumbnail_cache.find(path);
    if (cached == state->thumbnail_cache.end() || !cached->second.bitmap || !state->large_image_list) {
        return false;
    }

    const int idx = ImageList_Add(state->large_image_list, cached->second.bitmap, nullptr);
    if (idx < 0) {
        return false;
    }
    state->thumbnail_icon_indices[path] = idx;
    TouchThumbnailLru(state, path);
    *out_icon_index = idx;
    return true;
}

HBITMAP CreateThumbnailBitmap(const std::wstring& path) {
    return fme::ui::CreateImageThumbnailBitmap(path, 64);
}

HBITMAP CreateVideoThumbnailBitmap(const std::wstring& path) {
    return fme::ui::CreateVideoThumbnailBitmap(path, 64);
}

void UpdateCancelThumbsVisibility(MainWindowState* state) {
    if (!state || !state->cancel_thumbs) {
        return;
    }
    bool has_pending = state->thumbs_done < state->thumbs_total;
    if (!has_pending) {
        std::lock_guard<std::mutex> lock(state->thumb_mutex);
        has_pending = !state->thumb_queue.empty();
    }
    const bool show = (state->view_mode_value == ViewMode::Icons) && has_pending;
    ShowWindow(state->cancel_thumbs, show ? SW_SHOW : SW_HIDE);
}

void ClearThumbnailQueue(MainWindowState* state) {
    std::lock_guard<std::mutex> lock(state->thumb_mutex);
    state->thumb_queue.clear();
    state->thumb_queued_paths.clear();
}

void ClearThumbnailCache(MainWindowState* state) {
    for (auto& kv : state->thumbnail_cache) {
        if (kv.second.bitmap) {
            DeleteObject(kv.second.bitmap);
        }
    }
    state->thumbnail_cache.clear();
    state->thumbnail_icon_indices.clear();
    state->thumbnail_lru.clear();
    state->thumbnail_lru_pos.clear();
    state->thumbnail_cache_bytes = 0;
}

void EnqueueThumbnailTask(MainWindowState* state, const ThumbnailTask& task, bool high_priority) {
    {
        std::lock_guard<std::mutex> lock(state->thumb_mutex);
        if (state->thumb_queued_paths.find(task.path) != state->thumb_queued_paths.end()) {
            return;
        }
        state->thumb_queued_paths.insert(task.path);
        if (high_priority) {
            state->thumb_queue.push_front(task);
        } else {
            state->thumb_queue.push_back(task);
        }
    }
    state->thumb_cv.notify_one();
}

void ThumbnailWorkerLoop(HWND hwnd, MainWindowState* state) {
    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool com_initialized = SUCCEEDED(com_hr);
    while (true) {
        ThumbnailTask task{};
        {
            std::unique_lock<std::mutex> lock(state->thumb_mutex);
            state->thumb_cv.wait(lock, [&]() { return state->thumb_worker_stop || !state->thumb_queue.empty(); });
            if (state->thumb_worker_stop) {
                if (com_initialized) {
                    CoUninitialize();
                }
                return;
            }
            task = std::move(state->thumb_queue.front());
            state->thumb_queue.pop_front();
            state->thumb_queued_paths.erase(task.path);
        }

        HBITMAP hbitmap = nullptr;
        if (task.media_type == fme::MediaType::Image) {
            hbitmap = CreateThumbnailBitmap(task.path);
        } else if (task.media_type == fme::MediaType::Video) {
            hbitmap = CreateVideoThumbnailBitmap(task.path);
        }
        if (!hbitmap) {
            ++state->thumb_recent_failures;
            const int fail_backoff_ms = std::min(20, state->thumb_recent_failures);
            if (fail_backoff_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(fail_backoff_ms));
            }
            continue;
        }
        state->thumb_recent_failures = 0;

        auto* ready = new ThumbnailReady();
        ready->generation = task.generation;
        ready->data_index = task.data_index;
        ready->path = task.path;
        ready->hbitmap = hbitmap;
        if (!PostMessageW(hwnd, kMsgThumbReady, 0, reinterpret_cast<LPARAM>(ready))) {
            DeleteObject(hbitmap);
            delete ready;
        }

        state->thumb_throttle_ms = ComputeThumbnailThrottleMs(state);
        if (state->thumb_throttle_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(state->thumb_throttle_ms));
        }
    }
}

int IconIndexForFile(MainWindowState* state, const fme::MediaFile& file) {
    if (file.type == fme::MediaType::Image || file.type == fme::MediaType::Video) {
        const auto it = state->thumbnail_icon_indices.find(file.full_path);
        if (it != state->thumbnail_icon_indices.end()) {
            return it->second;
        }
    }
    return IconIndexForMedia(state, file.type);
}

void SetListViewMode(MainWindowState* state, ViewMode mode) {
    state->view_mode_value = mode;
    fme::ui::ConfigureListViewMode(state->list, mode == ViewMode::Icons);
    UpdateCancelThumbsVisibility(state);

    if (!state->scanning) {
        RefreshList(state);
    }
}

void SetStatusText(HWND status, const std::wstring& text) {
    SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void SetScanningState(MainWindowState* state, bool scanning) {
    state->scanning = scanning;
    if (scanning) {
        SetStatusText(
            state->status,
            L"Scanning: " + state->current_folder + (state->recursive_scan ? L" (recursive) ..." : L" ..."));
    }
    UpdateProgressIndicator(state);
    UpdateCancelThumbsVisibility(state);
}

std::wstring ExtensionFor(const fme::MediaFile& file) {
    return std::filesystem::path(file.full_path).extension().wstring();
}

bool IsImageFile(const fme::MediaFile& file) {
    return file.type == fme::MediaType::Image;
}

void ClearPreview(MainWindowState* state) {
    if (state->preview_image) {
        delete state->preview_image;
        state->preview_image = nullptr;
    }
    state->selected_file_path.clear();
    if (state->preview) {
        InvalidateRect(state->preview, nullptr, TRUE);
    }
}

void LoadPreviewFromFile(MainWindowState* state, const std::wstring& full_path) {
    ClearPreview(state);
    auto* image = Gdiplus::Image::FromFile(full_path.c_str(), FALSE);
    if (!image || image->GetLastStatus() != Gdiplus::Ok) {
        delete image;
        return;
    }
    state->selected_file_path = full_path;
    state->preview_image = image;
    InvalidateRect(state->preview, nullptr, TRUE);
}

void UpdatePreviewForSelection(MainWindowState* state) {
    size_t data_index = 0;
    if (!TryGetSelectedFileIndex(state, &data_index) || data_index >= state->current_files.size()) {
        ClearPreview(state);
        return;
    }
    if (IsImageFile(state->current_files[data_index])) {
        LoadPreviewFromFile(state, state->current_files[data_index].full_path);
    } else {
        ClearPreview(state);
    }
}

void RequestVisibleThumbnailTasks(MainWindowState* state) {
    if (!state || state->view_mode_value != ViewMode::Icons) {
        return;
    }
    const int top_index = ListView_GetTopIndex(state->list);
    int per_page = ListView_GetCountPerPage(state->list);
    if (per_page <= 0) {
        per_page = 30;
    }
    const int view_end = std::min(static_cast<int>(state->visible_indices.size()), top_index + per_page + 8);
    for (int row = std::max(0, top_index); row < view_end; ++row) {
        if (row >= static_cast<int>(state->visible_indices.size())) {
            break;
        }
        const size_t idx = state->visible_indices[static_cast<size_t>(row)];
        if (idx >= state->current_files.size()) {
            continue;
        }
        const auto& file = state->current_files[idx];
        if (file.type != fme::MediaType::Image && file.type != fme::MediaType::Video) {
            continue;
        }

        int icon_index = -1;
        if (TryGetCachedThumbnailIcon(state, file.full_path, &icon_index)) {
            continue;
        }

        ThumbnailTask task{};
        task.generation = state->thumb_generation;
        task.data_index = idx;
        task.path = file.full_path;
        task.media_type = file.type;
        EnqueueThumbnailTask(state, task, true);
    }
    UpdateCancelThumbsVisibility(state);
}

void RefreshList(MainWindowState* state) {
    ListView_SetItemCountEx(state->list, 0, LVSICF_NOINVALIDATEALL);
    ClearPreview(state);
    state->visible_row_by_data_index.clear();
    state->thumbs_done = 0;
    ++state->thumb_generation;
    ClearThumbnailQueue(state);

    fme::MediaQuery query{};
    query.name_substring = state->search_query;
    query.type_filter = state->type_filter_value;
    query.date_filter = state->date_filter_value;
    query.date_from = state->custom_date_from;
    query.date_to = state->custom_date_to;
    query.min_size = state->min_size_filter;
    query.max_size = state->max_size_filter;
    query.duplicates_by_size_only = state->duplicates_by_size_only;
    query.max_results = state->max_results;
    query.sort_field = state->sort_field;
    query.sort_ascending = state->sort_ascending;

    state->visible_indices = fme::ApplyMediaQuery(state->current_files, query);
    if (state->duplicate_filter_active && !state->duplicate_indices.empty()) {
        std::vector<size_t> filtered;
        filtered.reserve(state->visible_indices.size());
        for (size_t idx : state->visible_indices) {
            if (state->duplicate_indices.find(idx) != state->duplicate_indices.end()) {
                filtered.push_back(idx);
            }
        }
        state->visible_indices.swap(filtered);
    }

    if (state->view_mode_value == ViewMode::Icons && state->visible_indices.size() > 1800) {
        state->view_mode_value = ViewMode::Details;
        SendMessageW(state->view_mode, CB_SETCURSEL, 0, 0);
        fme::ui::ConfigureListViewMode(state->list, false);
    }

    std::uintmax_t shown_bytes = 0;
    int thumb_candidates = 0;
    for (size_t idx : state->visible_indices) {
        const auto& file = state->current_files[idx];
        shown_bytes += file.size;
        if (state->view_mode_value == ViewMode::Icons &&
            (file.type == fme::MediaType::Image || file.type == fme::MediaType::Video)) {
            const bool cached_icon = (state->thumbnail_icon_indices.find(file.full_path) != state->thumbnail_icon_indices.end());
            const bool cached_bitmap = (state->thumbnail_cache.find(file.full_path) != state->thumbnail_cache.end());
            if (!cached_icon && !cached_bitmap) {
                ++thumb_candidates;
            }
        }
    }

    state->thumbs_total = thumb_candidates;
    state->shown_count = static_cast<int>(state->visible_indices.size());
    state->shown_bytes = shown_bytes;
    state->render_started_tick = GetTickCount64();
    state->visible_row_by_data_index.reserve(state->visible_indices.size());
    for (size_t row = 0; row < state->visible_indices.size(); ++row) {
        state->visible_row_by_data_index[state->visible_indices[row]] = static_cast<int>(row);
    }
    ListView_SetItemCountEx(state->list, static_cast<int>(state->visible_indices.size()), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    ListView_RedrawItems(state->list, 0, std::max(0, static_cast<int>(state->visible_indices.size()) - 1));

    ++state->render_generation;
    state->render_cursor = 0;
    state->render_in_progress = (state->view_mode_value == ViewMode::Icons && state->thumbs_total > 0);
    state->render_next_row = 0;
    state->row_rendered.assign(state->visible_indices.size(), 0);
    SetStatusText(state->status, state->render_in_progress
        ? BuildRenderingStatusText(state)
        : BuildReadyStatusText(state, state->shown_count, state->shown_bytes));
    UpdateProgressIndicator(state);
    UpdateCancelThumbsVisibility(state);
    UpdateListOverlay(state, GetParent(state->list));
    if (state->render_in_progress) {
        PostMessageW(GetParent(state->list), kMsgRenderChunk, static_cast<WPARAM>(state->render_generation), 0);
    } else {
        state->last_render_ms = GetTickCount64() - state->render_started_tick;
    }
}

void BeginFolderScan(MainWindowState* state, HWND hwnd, const std::filesystem::path& folder) {
    state->current_folder = folder.wstring();
    state->current_files.clear();
    ClearDuplicateFilter(state);
    state->duplicate_analysis_running = false;
    ++state->dup_generation;
    state->visible_indices.clear();
    ListView_DeleteAllItems(state->list);
    ClearPreview(state);
    InitListIcons(state);

    const std::uint64_t generation = ++state->scan_generation;
    const bool recursive = state->recursive_scan;
    state->scan_started_tick = GetTickCount64();
    SetScanningState(state, true);
    UpdateListOverlay(state, hwnd);

    std::thread([hwnd, folder, generation, recursive]() {
        fme::FileIndexer worker_indexer;
        auto files = recursive ? worker_indexer.ScanFolderRecursive(folder) : worker_indexer.ScanFolderShallow(folder);

        auto* payload = new AsyncScanResult();
        payload->generation = generation;
        payload->folder = folder.wstring();
        payload->files = std::move(files);

        if (!PostMessageW(hwnd, kMsgScanCompleted, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

bool TrySelectFolderDialog(HWND owner, std::filesystem::path* out_folder) {
    if (!out_folder) {
        return false;
    }

    IFileOpenDialog* dialog = nullptr;
    const HRESULT create_hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(create_hr) || !dialog) {
        return false;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }

    const HRESULT show_hr = dialog->Show(owner);
    if (show_hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        return false;
    }
    if (FAILED(show_hr)) {
        dialog->Release();
        return false;
    }

    IShellItem* result = nullptr;
    const HRESULT result_hr = dialog->GetResult(&result);
    if (FAILED(result_hr) || !result) {
        dialog->Release();
        return false;
    }

    PWSTR path = nullptr;
    const HRESULT path_hr = result->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (SUCCEEDED(path_hr) && path) {
        *out_folder = std::filesystem::path(path);
    }
    if (path) {
        CoTaskMemFree(path);
    }
    result->Release();
    dialog->Release();
    return SUCCEEDED(path_hr);
}

bool SetRootFolderAndScan(MainWindowState* state, HWND hwnd, const std::filesystem::path& root_folder) {
    if (!state || !state->tree) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(root_folder, ec) || ec || !std::filesystem::is_directory(root_folder, ec) || ec) {
        MessageBoxW(hwnd, L"Selected item is not a folder.", L"Open folder", MB_ICONWARNING | MB_OK);
        return false;
    }

    const std::filesystem::path folder = std::filesystem::absolute(root_folder, ec);
    const std::filesystem::path final_folder = ec ? root_folder : folder;

    KillTimer(hwnd, kSearchDebounceTimerId);
    state->search_query.clear();
    SetWindowTextW(state->search_edit, L"");
    ApplyQuickPreset(state, QuickPreset::None);
    SendMessageW(state->quick_preset, CB_SETCURSEL, 0, 0);
    ClearDuplicateFilter(state);
    state->duplicate_last_result = {};
    state->duplicate_analysis_running = false;
    ++state->thumb_generation;
    ClearThumbnailQueue(state);
    ClearThumbnailCache(state);
    state->thumbs_total = 0;
    state->thumbs_done = 0;
    state->thumb_recent_failures = 0;
    state->last_scan_ms = 0;
    state->last_render_ms = 0;

    HTREEITEM old_root = TreeView_GetRoot(state->tree);
    if (old_root) {
        FreeTreeItemData(state->tree, old_root);
        TreeView_DeleteAllItems(state->tree);
    }

    state->root_path = final_folder.wstring();
    HTREEITEM root_item = InsertFolderNode(state->tree, TVI_ROOT, final_folder);
    if (!root_item) {
        SetStatusText(state->status, L"Failed to set root folder.");
        return false;
    }

    state->suppress_tree_selection_scan = true;
    TreeView_Expand(state->tree, root_item, TVE_EXPAND);
    TreeView_SelectItem(state->tree, root_item);
    state->suppress_tree_selection_scan = false;

    BeginFolderScan(state, hwnd, final_folder);
    return true;
}

void BeginScanForSelectedTreeItem(MainWindowState* state, HWND hwnd) {
    if (!state || !state->tree) {
        return;
    }
    const HTREEITEM selected = TreeView_GetSelection(state->tree);
    if (!selected) {
        return;
    }

    TVITEMW item{};
    item.mask = TVIF_PARAM;
    item.hItem = selected;
    if (!TreeView_GetItem(state->tree, &item) || item.lParam <= kDummyNodeParam) {
        return;
    }

    const auto* selected_path = reinterpret_cast<std::wstring*>(item.lParam);
    BeginFolderScan(state, hwnd, *selected_path);
}

bool TryGetSelectedFileIndex(MainWindowState* state, size_t* out_index) {
    const int selected_row = ListView_GetNextItem(state->list, -1, LVNI_SELECTED);
    if (selected_row < 0) {
        return false;
    }
    if (static_cast<size_t>(selected_row) >= state->visible_indices.size()) {
        return false;
    }
    const size_t data_index = state->visible_indices[static_cast<size_t>(selected_row)];
    if (data_index >= state->current_files.size()) {
        return false;
    }

    *out_index = data_index;
    return true;
}

void OpenSelectedFile(MainWindowState* state) {
    size_t index = 0;
    if (!TryGetSelectedFileIndex(state, &index)) {
        return;
    }

    const auto& path = state->current_files[index].full_path;
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        MessageBoxW(nullptr, L"Could not open selected file.", L"Open error", MB_ICONERROR);
    }
}

void DeleteSelectedFile(MainWindowState* state, HWND hwnd) {
    size_t index = 0;
    if (!TryGetSelectedFileIndex(state, &index)) {
        return;
    }

    const auto path = state->current_files[index].full_path;
    const auto name = state->current_files[index].name;
    const auto answer = MessageBoxW(
        hwnd,
        (L"Delete file?\n" + name).c_str(),
        L"Confirm delete",
        MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    if (answer != IDYES) {
        return;
    }

    // GDI+ preview may hold a lock on the selected image file.
    ClearPreview(state);

    std::error_code ec;
    const bool removed = std::filesystem::remove(path, ec);
    if (!removed || ec) {
        std::wstring details;
        if (ec) {
            const std::string msg = ec.message();
            const std::wstring wide_msg(msg.begin(), msg.end());
            details = L"\n\nReason: " + wide_msg;
        }
        MessageBoxW(hwnd, (L"Could not delete selected file." + details).c_str(), L"Delete error", MB_ICONERROR);
        return;
    }

    state->current_files.erase(state->current_files.begin() + static_cast<std::ptrdiff_t>(index));
    RefreshList(state);
}

std::chrono::system_clock::time_point MakeLocalDayStart(int year, int month, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::chrono::system_clock::time_point MakeLocalDayEnd(int year, int month, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 23;
    tm.tm_min = 59;
    tm.tm_sec = 59;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

SYSTEMTIME TimePointToSystemDate(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local_tm{};
    localtime_s(&local_tm, &t);

    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(local_tm.tm_year + 1900);
    st.wMonth = static_cast<WORD>(local_tm.tm_mon + 1);
    st.wDay = static_cast<WORD>(local_tm.tm_mday);
    return st;
}

int CompareSystemDates(const SYSTEMTIME& a, const SYSTEMTIME& b) {
    FILETIME a_ft{};
    FILETIME b_ft{};
    SystemTimeToFileTime(&a, &a_ft);
    SystemTimeToFileTime(&b, &b_ft);

    ULARGE_INTEGER a_u{};
    a_u.LowPart = a_ft.dwLowDateTime;
    a_u.HighPart = a_ft.dwHighDateTime;
    ULARGE_INTEGER b_u{};
    b_u.LowPart = b_ft.dwLowDateTime;
    b_u.HighPart = b_ft.dwHighDateTime;

    if (a_u.QuadPart < b_u.QuadPart) return -1;
    if (a_u.QuadPart > b_u.QuadPart) return 1;
    return 0;
}

LRESULT CALLBACK DateRangeDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<DateRangeDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE: {
        auto* s = reinterpret_cast<DateRangeDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        CreateWindowExW(0, L"STATIC", L"From:", WS_CHILD | WS_VISIBLE, 14, 18, 40, 20, hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"To:", WS_CHILD | WS_VISIBLE, 14, 54, 40, 20, hwnd, nullptr, nullptr, nullptr);
        s->picker_from = CreateWindowExW(
            0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT,
            62, 14, 140, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDateFromPickerId)), nullptr, nullptr);
        s->picker_to = CreateWindowExW(
            0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT,
            62, 50, 140, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDateToPickerId)), nullptr, nullptr);

        DateTime_SetSystemtime(s->picker_from, GDT_VALID, &s->from);
        DateTime_SetSystemtime(s->picker_to, GDT_VALID, &s->to);

        CreateWindowExW(
            0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            62, 90, 70, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDateOkButtonId)), nullptr, nullptr);
        CreateWindowExW(
            0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            136, 90, 70, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDateCancelButtonId)), nullptr, nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (!state) {
            return 0;
        }
        if (LOWORD(wparam) == kDateOkButtonId) {
            SYSTEMTIME from{};
            SYSTEMTIME to{};
            DateTime_GetSystemtime(state->picker_from, &from);
            DateTime_GetSystemtime(state->picker_to, &to);
            if (CompareSystemDates(from, to) > 0) {
                MessageBoxW(hwnd, L"'From' date must be before or equal to 'To' date.", L"Invalid range", MB_ICONWARNING);
                return 0;
            }
            state->from = from;
            state->to = to;
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wparam) == kDateCancelButtonId) {
            state->accepted = false;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (state) {
            state->accepted = false;
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

bool ShowDateRangeDialog(HWND owner, const SYSTEMTIME& initial_from, const SYSTEMTIME& initial_to, SYSTEMTIME* out_from, SYSTEMTIME* out_to) {
    DateRangeDialogState state{};
    state.owner = owner;
    state.from = initial_from;
    state.to = initial_to;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kDateRangeClassName,
        L"Select date range",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 235, 165,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (!dlg) {
        return false;
    }

    EnableWindow(owner, FALSE);
    MSG msg{};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);

    if (!state.accepted) {
        return false;
    }
    *out_from = state.from;
    *out_to = state.to;
    return true;
}

void LayoutChildren(HWND hwnd, MainWindowState* state) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    SendMessageW(state->status, WM_SIZE, 0, 0);
    RECT status_rc{};
    GetWindowRect(state->status, &status_rc);
    const int status_h = status_rc.bottom - status_rc.top;

    const int width = rc.right - rc.left;
    const int top_h = 34;
    const int height = rc.bottom - rc.top - status_h - top_h;
    const int tree_w = std::max(260, width * 30 / 100);
    const int list_w = width - tree_w;
    const int overlay_h = 28;
    const int preview_h = std::max(180, height * 35 / 100);
    const int list_h = std::max(120, height - preview_h - overlay_h);

    const int type_w = 150;
    const int date_w = 145;
    const int progress_w = 150;
    const int cancel_w = 110;
    const int recursive_w = 95;
    const int preset_w = 155;
    const int view_w = 95;
    const int right_margin = 10;
    const int gap = 8;
    const int type_x = width - type_w - right_margin;
    const int date_x = type_x - date_w - gap;
    const int progress_x = date_x - progress_w - gap;
    const int cancel_x = progress_x - cancel_w - gap;
    const int recursive_x = cancel_x - recursive_w - gap;
    const int view_x = recursive_x - view_w - gap;
    const int preset_x = view_x - preset_w - gap;

    const int search_w = std::max(120, preset_x - 20);
    MoveWindow(state->search_edit, 10, 6, search_w, 22, TRUE);
    MoveWindow(state->quick_preset, preset_x, 6, preset_w, 300, TRUE);
    MoveWindow(state->view_mode, view_x, 6, view_w, 300, TRUE);
    MoveWindow(state->recursive_check, recursive_x, 8, recursive_w, 20, TRUE);
    MoveWindow(state->cancel_thumbs, cancel_x, 6, cancel_w, 22, TRUE);
    MoveWindow(state->progress, progress_x, 7, progress_w, 20, TRUE);
    MoveWindow(state->date_filter, date_x, 6, date_w, 300, TRUE);
    MoveWindow(state->type_filter, type_x, 6, type_w, 300, TRUE);
    MoveWindow(state->tree, 0, top_h, tree_w, height, TRUE);
    MoveWindow(state->list, tree_w, top_h, list_w, list_h, TRUE);
    MoveWindow(state->list_overlay, tree_w, top_h + list_h, list_w, overlay_h, TRUE);
    MoveWindow(state->preview, tree_w, top_h + list_h + overlay_h, list_w, height - list_h - overlay_h, TRUE);
}

fme::MediaTypeFilter TypeFilterFromComboSelection(LRESULT selected) {
    switch (selected) {
    case 1:
        return fme::MediaTypeFilter::Image;
    case 2:
        return fme::MediaTypeFilter::Video;
    case 3:
        return fme::MediaTypeFilter::Audio;
    case 4:
        return fme::MediaTypeFilter::Other;
    default:
        return fme::MediaTypeFilter::All;
    }
}

fme::DateFilter DateFilterFromComboSelection(LRESULT selected) {
    switch (selected) {
    case 1:
        return fme::DateFilter::Today;
    case 2:
        return fme::DateFilter::Last7Days;
    case 3:
        return fme::DateFilter::Last30Days;
    case 4:
        return fme::DateFilter::CustomRange;
    default:
        return fme::DateFilter::All;
    }
}

int DateFilterComboIndex(fme::DateFilter filter) {
    switch (filter) {
    case fme::DateFilter::Today: return 1;
    case fme::DateFilter::Last7Days: return 2;
    case fme::DateFilter::Last30Days: return 3;
    case fme::DateFilter::CustomRange: return 4;
    default: return 0;
    }
}

QuickPreset QuickPresetFromComboSelection(LRESULT selected) {
    switch (selected) {
    case 1: return QuickPreset::Recent48h;
    case 2: return QuickPreset::LargeOver100MB;
    case 3: return QuickPreset::ImagesOnly;
    case 4: return QuickPreset::VideosOnly;
    case 5: return QuickPreset::PotentialDuplicates;
    case 6: return QuickPreset::LargestTop200;
    default: return QuickPreset::None;
    }
}

void ApplyQuickPreset(MainWindowState* state, QuickPreset preset) {
    using namespace std::chrono;
    state->quick_preset_value = preset;
    state->min_size_filter = 0;
    state->max_size_filter = (std::numeric_limits<std::uintmax_t>::max)();
    state->duplicates_by_size_only = false;
    state->max_results = 0;
    state->type_filter_value = fme::MediaTypeFilter::All;
    state->date_filter_value = fme::DateFilter::All;
    state->sort_field = fme::SortField::Name;
    state->sort_ascending = true;

    switch (preset) {
    case QuickPreset::Recent48h:
        state->date_filter_value = fme::DateFilter::CustomRange;
        state->custom_date_to = system_clock::now();
        state->custom_date_from = state->custom_date_to - hours(48);
        UpdateCustomRangeComboItem(state);
        break;
    case QuickPreset::LargeOver100MB:
        state->min_size_filter = 100ull * 1024ull * 1024ull;
        break;
    case QuickPreset::ImagesOnly:
        state->type_filter_value = fme::MediaTypeFilter::Image;
        break;
    case QuickPreset::VideosOnly:
        state->type_filter_value = fme::MediaTypeFilter::Video;
        break;
    case QuickPreset::PotentialDuplicates:
        state->duplicates_by_size_only = true;
        state->sort_field = fme::SortField::Size;
        state->sort_ascending = false;
        break;
    case QuickPreset::LargestTop200:
        state->sort_field = fme::SortField::Size;
        state->sort_ascending = false;
        state->max_results = 200;
        break;
    case QuickPreset::None:
    default:
        break;
    }

    SendMessageW(state->type_filter, CB_SETCURSEL, [&]() -> LRESULT {
        switch (state->type_filter_value) {
        case fme::MediaTypeFilter::Image: return 1;
        case fme::MediaTypeFilter::Video: return 2;
        case fme::MediaTypeFilter::Audio: return 3;
        case fme::MediaTypeFilter::Other: return 4;
        default: return 0;
        }
    }(), 0);
    SendMessageW(state->date_filter, CB_SETCURSEL, DateFilterComboIndex(state->date_filter_value), 0);
}

bool TryGetSelectedFilePath(MainWindowState* state, std::wstring* out_path) {
    size_t index = 0;
    if (!TryGetSelectedFileIndex(state, &index)) {
        return false;
    }
    *out_path = state->current_files[index].full_path;
    return true;
}

void ShowSelectedFileInFolder(MainWindowState* state, HWND hwnd) {
    std::wstring path;
    if (!TryGetSelectedFilePath(state, &path)) {
        return;
    }
    const std::wstring args = L"/select,\"" + path + L"\"";
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        MessageBoxW(hwnd, L"Could not show file in folder.", L"Open error", MB_ICONERROR);
    }
}

void CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) {
        return;
    }
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hmem) {
        void* data = GlobalLock(hmem);
        if (data) {
            memcpy(data, text.c_str(), bytes);
            GlobalUnlock(hmem);
            SetClipboardData(CF_UNICODETEXT, hmem);
            hmem = nullptr;
        }
    }
    if (hmem) {
        GlobalFree(hmem);
    }
    CloseClipboard();
}

void CopySelectedFilePath(MainWindowState* state, HWND hwnd) {
    std::wstring path;
    if (!TryGetSelectedFilePath(state, &path)) {
        return;
    }
    CopyTextToClipboard(hwnd, path);
}

void ShowListContextMenu(MainWindowState* state, HWND hwnd, POINT screen_pt) {
    if (!state) {
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kCmdCtxOpen, L"Open");
    AppendMenuW(menu, MF_STRING, kCmdCtxShowInFolder, L"Show in folder");
    AppendMenuW(menu, MF_STRING, kCmdCtxCopyPath, L"Copy path");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdCtxDelete, L"Delete");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdCtxRefresh, L"Refresh");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

fme::SortField SortFieldFromColumn(int column) {
    switch (column) {
    case 1:
        return fme::SortField::Type;
    case 2:
        return fme::SortField::Extension;
    case 3:
        return fme::SortField::Size;
    default:
        return fme::SortField::Name;
    }
}

LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    auto* state = reinterpret_cast<MainWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

        if (!state || !state->preview_image) {
            DrawTextW(hdc, L"Preview", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }

        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        const UINT iw = state->preview_image->GetWidth();
        const UINT ih = state->preview_image->GetHeight();
        const int cw = rc.right - rc.left;
        const int ch = rc.bottom - rc.top;

        if (iw == 0 || ih == 0 || cw <= 0 || ch <= 0) {
            EndPaint(hwnd, &ps);
            return 0;
        }

        const double sx = static_cast<double>(cw) / static_cast<double>(iw);
        const double sy = static_cast<double>(ch) / static_cast<double>(ih);
        const double scale = sx < sy ? sx : sy;
        const int dw = static_cast<int>(iw * scale);
        const int dh = static_cast<int>(ih * scale);
        const int dx = (cw - dw) / 2;
        const int dy = (ch - dh) / 2;
        graphics.DrawImage(state->preview_image, dx, dy, dw, dh);

        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<MainWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* new_state = new MainWindowState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new_state));
        SetMenu(hwnd, BuildAppMenu());
        DragAcceptFiles(hwnd, TRUE);
        const auto now = std::chrono::system_clock::now();
        new_state->custom_date_to = now;
        new_state->custom_date_from = now - std::chrono::hours(24 * 30);
        new_state->thumb_worker = std::thread(ThumbnailWorkerLoop, hwnd, new_state);

        new_state->tree = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_TREEVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS,
            0, 0, 300, 600,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTreeViewId)),
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->list = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
            300, 0, 800, 600,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListViewId)),
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->status = CreateWindowExW(
            0,
            STATUSCLASSNAMEW,
            L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->search_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            10, 6, 500, 22,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEditId)),
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->quick_preset = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            200, 6, 155, 300,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQuickPresetId)),
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->view_mode = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            360, 6, 95, 300,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewModeId)),
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->type_filter = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            520, 6, 170, 300,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTypeFilterId)),
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->date_filter = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            360, 6, 150, 300,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDateFilterId)),
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->recursive_check = CreateWindowExW(
            0,
            L"BUTTON",
            L"Recursive",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            600, 8, 95, 20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRecursiveCheckId)),
            GetModuleHandleW(nullptr),
            nullptr);

        new_state->progress = CreateWindowExW(
            0,
            PROGRESS_CLASSW,
            L"",
            WS_CHILD | PBS_MARQUEE,
            700, 7, 150, 20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProgressId)),
            GetModuleHandleW(nullptr),
            nullptr);
        ShowWindow(new_state->progress, SW_HIDE);

        new_state->cancel_thumbs = CreateWindowExW(
            0,
            L"BUTTON",
            L"Cancel thumbs",
            WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 100, 22,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelThumbsId)),
            GetModuleHandleW(nullptr),
            nullptr);
        ShowWindow(new_state->cancel_thumbs, SW_HIDE);

        new_state->preview = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            kPreviewClassName,
            L"",
            WS_CHILD | WS_VISIBLE,
            300, 500, 800, 140,
            hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            new_state);

        new_state->list_overlay = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"STATIC",
            L"",
            WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
            0, 0, 200, 34,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListOverlayId)),
            GetModuleHandleW(nullptr),
            nullptr);
        ShowWindow(new_state->list_overlay, SW_HIDE);

        SendMessageW(new_state->type_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All types"));
        SendMessageW(new_state->type_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Images"));
        SendMessageW(new_state->type_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Videos"));
        SendMessageW(new_state->type_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Audio"));
        SendMessageW(new_state->type_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Other"));
        SendMessageW(new_state->type_filter, CB_SETCURSEL, 0, 0);
        SendMessageW(new_state->quick_preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No preset"));
        SendMessageW(new_state->quick_preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Recent (48h)"));
        SendMessageW(new_state->quick_preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Large > 100MB"));
        SendMessageW(new_state->quick_preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Images only"));
        SendMessageW(new_state->quick_preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Videos only"));
        SendMessageW(new_state->quick_preset, CB_SETCURSEL, 0, 0);
        SendMessageW(new_state->view_mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Details"));
        SendMessageW(new_state->view_mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Icons"));
        SendMessageW(new_state->view_mode, CB_SETCURSEL, 0, 0);
        SendMessageW(new_state->date_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All dates"));
        SendMessageW(new_state->date_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Today"));
        SendMessageW(new_state->date_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Last 7 days"));
        SendMessageW(new_state->date_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Last 30 days"));
        SendMessageW(new_state->date_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom range..."));
        UpdateCustomRangeComboItem(new_state);
        SendMessageW(new_state->date_filter, CB_SETCURSEL, 0, 0);

        SendMessageW(new_state->search_edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search files by name..."));
        InitListColumns(new_state->list);
        InitListIcons(new_state);
        SetListViewMode(new_state, ViewMode::Details);

        const auto cwd = std::filesystem::current_path();
        SetRootFolderAndScan(new_state, hwnd, cwd);

        if (!new_state->scanning) {
            SetStatusText(new_state->status, L"Ready.");
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == SC_CLOSE) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdFocusSearch) {
            SetFocus(state->search_edit);
            SendMessageW(state->search_edit, EM_SETSEL, 0, -1);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdOpenFolder) {
            std::filesystem::path folder;
            if (TrySelectFolderDialog(hwnd, &folder)) {
                SetRootFolderAndScan(state, hwnd, folder);
            }
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdRefreshFolder) {
            BeginScanForSelectedTreeItem(state, hwnd);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdCtxOpen) {
            OpenSelectedFile(state);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdCtxShowInFolder) {
            ShowSelectedFileInFolder(state, hwnd);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdCtxCopyPath) {
            CopySelectedFilePath(state, hwnd);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdCtxDelete) {
            DeleteSelectedFile(state, hwnd);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdCtxRefresh) {
            BeginScanForSelectedTreeItem(state, hwnd);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdToolsDupSizeOnly) {
            SendMessageW(state->quick_preset, CB_SETCURSEL, 0, 0);
            BeginDuplicateAnalysis(state, hwnd, fme::DuplicateMode::SizeOnly);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdToolsDupExactHash) {
            SendMessageW(state->quick_preset, CB_SETCURSEL, 0, 0);
            BeginDuplicateAnalysis(state, hwnd, fme::DuplicateMode::ExactHash);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdToolsDupExactHashNameExt) {
            SendMessageW(state->quick_preset, CB_SETCURSEL, 0, 0);
            BeginDuplicateAnalysis(state, hwnd, fme::DuplicateMode::ExactHashNameExt);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdToolsDupLastResult) {
            if (state->duplicate_last_result.groups.empty()) {
                MessageBoxW(hwnd, L"No duplicate result available yet.", L"Duplicate analysis", MB_ICONINFORMATION);
            } else {
                ApplyDuplicateResultFilter(state);
                RefreshList(state);
            }
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdToolsDupClear) {
            ClearDuplicateFilter(state);
            RefreshList(state);
            return 0;
        }
        if (state && LOWORD(wparam) == kCmdToolsTopLargest) {
            ApplyQuickPreset(state, QuickPreset::LargestTop200);
            SendMessageW(state->quick_preset, CB_SETCURSEL, 0, 0);
            RefreshList(state);
            return 0;
        }
        if (LOWORD(wparam) == kCmdHelpAbout) {
            MessageBoxW(hwnd, BuildAboutText().c_str(), L"About Fast Media Explorer", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (state && LOWORD(wparam) == kSearchEditId && HIWORD(wparam) == EN_CHANGE) {
            wchar_t buffer[512]{};
            GetWindowTextW(state->search_edit, buffer, 512);
            state->search_query = buffer;
            KillTimer(hwnd, kSearchDebounceTimerId);
            SetTimer(hwnd, kSearchDebounceTimerId, kSearchDebounceMs, nullptr);
            return 0;
        }
        if (state && LOWORD(wparam) == kTypeFilterId && HIWORD(wparam) == CBN_SELCHANGE) {
            const LRESULT selected = SendMessageW(state->type_filter, CB_GETCURSEL, 0, 0);
            state->type_filter_value = TypeFilterFromComboSelection(selected);
            if (!state->scanning) {
                RefreshList(state);
            }
            return 0;
        }
        if (state && LOWORD(wparam) == kQuickPresetId && HIWORD(wparam) == CBN_SELCHANGE) {
            const LRESULT selected = SendMessageW(state->quick_preset, CB_GETCURSEL, 0, 0);
            ApplyQuickPreset(state, QuickPresetFromComboSelection(selected));
            if (!state->scanning) {
                RefreshList(state);
            }
            return 0;
        }
        if (state && LOWORD(wparam) == kViewModeId && HIWORD(wparam) == CBN_SELCHANGE) {
            const LRESULT selected = SendMessageW(state->view_mode, CB_GETCURSEL, 0, 0);
            SetListViewMode(state, selected == 1 ? ViewMode::Icons : ViewMode::Details);
            return 0;
        }
        if (state && LOWORD(wparam) == kDateFilterId && HIWORD(wparam) == CBN_SELCHANGE) {
            const LRESULT selected = SendMessageW(state->date_filter, CB_GETCURSEL, 0, 0);
            const auto chosen = DateFilterFromComboSelection(selected);
            if (chosen == fme::DateFilter::CustomRange) {
                SYSTEMTIME from = TimePointToSystemDate(state->custom_date_from);
                SYSTEMTIME to = TimePointToSystemDate(state->custom_date_to);
                SYSTEMTIME picked_from{};
                SYSTEMTIME picked_to{};
                if (ShowDateRangeDialog(hwnd, from, to, &picked_from, &picked_to)) {
                    state->custom_date_from = MakeLocalDayStart(picked_from.wYear, picked_from.wMonth, picked_from.wDay);
                    state->custom_date_to = MakeLocalDayEnd(picked_to.wYear, picked_to.wMonth, picked_to.wDay);
                    state->date_filter_value = fme::DateFilter::CustomRange;
                    UpdateCustomRangeComboItem(state);
                    SendMessageW(state->date_filter, CB_SETCURSEL, 4, 0);
                } else {
                    // User canceled custom range selection, revert to previous filter.
                    int restore = 0;
                    switch (state->date_filter_value) {
                    case fme::DateFilter::Today: restore = 1; break;
                    case fme::DateFilter::Last7Days: restore = 2; break;
                    case fme::DateFilter::Last30Days: restore = 3; break;
                    case fme::DateFilter::CustomRange: restore = 4; break;
                    default: restore = 0; break;
                    }
                    SendMessageW(state->date_filter, CB_SETCURSEL, restore, 0);
                    return 0;
                }
            } else {
                state->date_filter_value = chosen;
            }
            if (!state->scanning) {
                RefreshList(state);
            }
            return 0;
        }
        if (state && LOWORD(wparam) == kRecursiveCheckId && HIWORD(wparam) == BN_CLICKED) {
            state->recursive_scan = (SendMessageW(state->recursive_check, BM_GETCHECK, 0, 0) == BST_CHECKED);
            BeginScanForSelectedTreeItem(state, hwnd);
            return 0;
        }
        if (state && LOWORD(wparam) == kCancelThumbsId && HIWORD(wparam) == BN_CLICKED) {
            ++state->thumb_generation;
            ClearThumbnailQueue(state);
            state->thumbs_total = state->thumbs_done;
            UpdateCancelThumbsVisibility(state);
            UpdateProgressIndicator(state);
            UpdateListOverlay(state, hwnd);
            if (state->render_in_progress) {
                SetStatusText(state->status, BuildRenderingStatusText(state));
            } else {
                SetStatusText(state->status, BuildReadyStatusText(state, state->shown_count, state->shown_bytes));
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SIZE:
        if (state) {
            LayoutChildren(hwnd, state);
            UpdateListOverlay(state, hwnd);
        }
        return 0;
    case WM_TIMER:
        if (state && wparam == kOverlayTimerId) {
            ++state->overlay_frame;
            UpdateListOverlay(state, hwnd);
            return 0;
        }
        if (state && wparam == kSearchDebounceTimerId) {
            KillTimer(hwnd, kSearchDebounceTimerId);
            if (!state->scanning) {
                RefreshList(state);
            }
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        if (state && reinterpret_cast<HWND>(wparam) == state->list) {
            POINT screen_pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            if (screen_pt.x != -1 && screen_pt.y != -1) {
                POINT client_pt = screen_pt;
                ScreenToClient(state->list, &client_pt);
                LVHITTESTINFO hit{};
                hit.pt = client_pt;
                const int item = ListView_HitTest(state->list, &hit);
                if (item >= 0) {
                    ListView_SetItemState(state->list, -1, 0, LVIS_SELECTED);
                    ListView_SetItemState(state->list, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            } else {
                const int selected = ListView_GetNextItem(state->list, -1, LVNI_SELECTED);
                if (selected >= 0) {
                    RECT r{};
                    ListView_GetItemRect(state->list, selected, &r, LVIR_BOUNDS);
                    POINT client_pt{ r.left, r.bottom };
                    ClientToScreen(state->list, &client_pt);
                    screen_pt = client_pt;
                } else {
                    GetCursorPos(&screen_pt);
                }
            }
            ShowListContextMenu(state, hwnd, screen_pt);
            return 0;
        }
        break;
    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
        if (state && state->render_in_progress) {
            PostMessageW(hwnd, kMsgRenderChunk, static_cast<WPARAM>(state->render_generation), 0);
        } else if (state && state->view_mode_value == ViewMode::Icons) {
            RequestVisibleThumbnailTasks(state);
        }
        break;
    case WM_DROPFILES: {
        if (!state) {
            return 0;
        }

        const HDROP drop = reinterpret_cast<HDROP>(wparam);
        const UINT dropped = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (dropped == 0) {
            DragFinish(drop);
            return 0;
        }

        std::filesystem::path dropped_path;
        bool found_folder = false;
        for (UINT i = 0; i < dropped; ++i) {
            const UINT path_len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(path_len + 1, L'\0');
            DragQueryFileW(drop, i, path.data(), path_len + 1);
            std::error_code path_ec;
            const std::filesystem::path candidate(path.c_str());
            if (std::filesystem::is_directory(candidate, path_ec) && !path_ec) {
                dropped_path = candidate;
                found_folder = true;
                break;
            }
        }
        DragFinish(drop);

        if (!found_folder) {
            MessageBoxW(hwnd, L"Dropped items do not contain a folder. Please drop a folder.", L"Drag and drop", MB_ICONWARNING | MB_OK);
            return 0;
        }

        SetRootFolderAndScan(state, hwnd, dropped_path);
        return 0;
    }
    case WM_NOTIFY: {
        if (!state) {
            return 0;
        }

        const auto* hdr = reinterpret_cast<NMHDR*>(lparam);
        if (hdr->idFrom == kTreeViewId && hdr->code == TVN_ITEMEXPANDINGW) {
            const auto* tv = reinterpret_cast<const NMTREEVIEWW*>(lparam);
            if (tv->action == TVE_EXPAND) {
                EnsureExpandedItemIsLoaded(state->tree, tv->itemNew.hItem);
            }
        }

        if (hdr->idFrom == kTreeViewId && hdr->code == TVN_SELCHANGEDW) {
            const auto* tv = reinterpret_cast<const NMTREEVIEWW*>(lparam);
            if (!state->suppress_tree_selection_scan && tv->itemNew.lParam > kDummyNodeParam) {
                const auto* selected_path = reinterpret_cast<std::wstring*>(tv->itemNew.lParam);
                BeginFolderScan(state, hwnd, *selected_path);
            }
        }
        if (hdr->idFrom == kListViewId && hdr->code == LVN_COLUMNCLICK) {
            const auto* info = reinterpret_cast<const NMLISTVIEW*>(lparam);
            const auto clicked_field = SortFieldFromColumn(info->iSubItem);
            if (state->sort_field == clicked_field) {
                state->sort_ascending = !state->sort_ascending;
            } else {
                state->sort_field = clicked_field;
                state->sort_ascending = true;
            }
            if (!state->scanning) {
                RefreshList(state);
            }
            return 0;
        }
        if (hdr->idFrom == kListViewId && hdr->code == LVN_GETDISPINFOW) {
            auto* disp = reinterpret_cast<NMLVDISPINFOW*>(lparam);
            const int row = disp->item.iItem;
            if (row < 0 || static_cast<size_t>(row) >= state->visible_indices.size()) {
                return 0;
            }
            const size_t data_index = state->visible_indices[static_cast<size_t>(row)];
            if (data_index >= state->current_files.size()) {
                return 0;
            }
            const auto& file = state->current_files[data_index];

            if ((disp->item.mask & LVIF_PARAM) != 0) {
                disp->item.lParam = static_cast<LPARAM>(data_index);
            }
            if ((disp->item.mask & LVIF_IMAGE) != 0) {
                disp->item.iImage = IconIndexForFile(state, file);
            }
            if ((disp->item.mask & LVIF_TEXT) != 0) {
                static thread_local std::wstring text;
                switch (disp->item.iSubItem) {
                case 1:
                    text = fme::MediaTypeToText(file.type);
                    break;
                case 2:
                    text = ExtensionFor(file);
                    break;
                case 3:
                    text = FormatSize(file.size);
                    break;
                default:
                    text = file.name;
                    break;
                }
                disp->item.pszText = const_cast<wchar_t*>(text.c_str());
            }
            return 0;
        }
        if (hdr->idFrom == kListViewId && hdr->code == LVN_ODCACHEHINT) {
            if (state->view_mode_value == ViewMode::Icons) {
                RequestVisibleThumbnailTasks(state);
            }
            return 0;
        }
        if (hdr->idFrom == kListViewId && hdr->code == NM_CLICK) {
            UpdatePreviewForSelection(state);
            return 0;
        }
        if (hdr->idFrom == kListViewId && hdr->code == LVN_ITEMCHANGED) {
            const auto* info = reinterpret_cast<const NMLISTVIEW*>(lparam);
            if ((info->uChanged & LVIF_STATE) != 0) {
                const bool became_selected = ((info->uNewState & LVIS_SELECTED) != 0) && ((info->uOldState & LVIS_SELECTED) == 0);
                const bool became_deselected = ((info->uNewState & LVIS_SELECTED) == 0) && ((info->uOldState & LVIS_SELECTED) != 0);
                if (became_selected || became_deselected) {
                    UpdatePreviewForSelection(state);
                }
            }
            return 0;
        }
        if (hdr->idFrom == kListViewId && hdr->code == LVN_KEYDOWN) {
            const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(lparam);
            if (key->wVKey == VK_RETURN) {
                OpenSelectedFile(state);
                return 0;
            }
            if (key->wVKey == VK_DELETE) {
                DeleteSelectedFile(state, hwnd);
                return 0;
            }
        }
        return 0;
    }
    case kMsgRenderChunk: {
        if (!state) {
            return 0;
        }
        const std::uint64_t generation = static_cast<std::uint64_t>(wparam);
        if (generation != state->render_generation) {
            return 0;
        }

        const ULONGLONG t0 = GetTickCount64();
        std::vector<size_t> chunk_rows;
        chunk_rows.reserve(state->render_batch_size);

        // 1) Prioritize what user can currently see.
        const int top_index = ListView_GetTopIndex(state->list);
        int per_page = ListView_GetCountPerPage(state->list);
        if (per_page <= 0) {
            per_page = 30;
        }
        const int view_end = top_index + per_page + 8; // slight prefetch below viewport
        for (int r = std::max(0, top_index); r < view_end && chunk_rows.size() < state->render_batch_size; ++r) {
            if (r < static_cast<int>(state->row_rendered.size()) && !state->row_rendered[r]) {
                chunk_rows.push_back(static_cast<size_t>(r));
            }
        }

        // 2) Fill remaining budget with sequential rows.
        while (chunk_rows.size() < state->render_batch_size && state->render_next_row < state->visible_indices.size()) {
            const size_t r = state->render_next_row++;
            if (!state->row_rendered[r]) {
                chunk_rows.push_back(r);
            }
        }

        for (size_t row_index : chunk_rows) {
            if (row_index >= state->visible_indices.size() || state->row_rendered[row_index]) {
                continue;
            }
            const size_t idx = state->visible_indices[row_index];
            const auto& file = state->current_files[idx];
            const int row = static_cast<int>(row_index);
            if (state->view_mode_value == ViewMode::Icons &&
                (file.type == fme::MediaType::Image || file.type == fme::MediaType::Video)) {
                int icon_index = -1;
                if (TryGetCachedThumbnailIcon(state, file.full_path, &icon_index)) {
                } else if (row >= top_index && row < view_end) {
                    ThumbnailTask task{};
                    task.generation = state->thumb_generation;
                    task.data_index = idx;
                    task.path = file.full_path;
                    task.media_type = file.type;
                    EnqueueThumbnailTask(state, task, true);
                }
            }
            state->row_rendered[row_index] = 1;
            ++state->render_cursor;
        }

        while (state->render_next_row < state->row_rendered.size() && state->row_rendered[state->render_next_row]) {
            ++state->render_next_row;
        }

        const ULONGLONG elapsed_ms = GetTickCount64() - t0;
        if (elapsed_ms > 24 && state->render_batch_size > state->render_batch_min) {
            state->render_batch_size = std::max(state->render_batch_min, state->render_batch_size * 3 / 4);
        } else if (elapsed_ms < 10 && state->render_batch_size < state->render_batch_max) {
            state->render_batch_size = std::min(state->render_batch_max, state->render_batch_size + 24);
        }

        if (state->render_cursor < state->visible_indices.size()) {
            SetStatusText(state->status, BuildRenderingStatusText(state));
            UpdateCancelThumbsVisibility(state);
            UpdateProgressIndicator(state);
            UpdateListOverlay(state, hwnd);
            PostMessageW(hwnd, kMsgRenderChunk, static_cast<WPARAM>(generation), 0);
        } else {
            state->render_in_progress = false;
            state->last_render_ms = GetTickCount64() - state->render_started_tick;
            RequestVisibleThumbnailTasks(state);
            SetStatusText(state->status, BuildReadyStatusText(state, state->shown_count, state->shown_bytes));
            UpdateCancelThumbsVisibility(state);
            UpdateProgressIndicator(state);
            UpdateListOverlay(state, hwnd);
        }
        return 0;
    }
    case kMsgScanCompleted: {
        auto* payload = reinterpret_cast<AsyncScanResult*>(lparam);
        if (!payload) {
            return 0;
        }
        if (!state) {
            delete payload;
            return 0;
        }
        if (payload->generation != state->scan_generation) {
            delete payload;
            return 0;
        }

        state->current_folder = std::move(payload->folder);
        state->current_files = std::move(payload->files);
        state->last_scan_ms = (state->scan_started_tick == 0) ? 0 : (GetTickCount64() - state->scan_started_tick);
        SetScanningState(state, false);
        RefreshList(state);
        UpdateListOverlay(state, hwnd);
        delete payload;
        return 0;
    }
    case kMsgThumbReady: {
        auto* ready = reinterpret_cast<ThumbnailReady*>(lparam);
        if (!ready) {
            return 0;
        }
        if (!state || ready->generation != state->thumb_generation) {
            if (ready->hbitmap) {
                DeleteObject(ready->hbitmap);
            }
            delete ready;
            return 0;
        }

        PutThumbnailInCache(state, ready->path, ready->hbitmap);
        ready->hbitmap = nullptr;

        int icon_index = -1;
        (void)TryGetCachedThumbnailIcon(state, ready->path, &icon_index);

        if (icon_index >= 0) {
            const auto row_it = state->visible_row_by_data_index.find(ready->data_index);
            if (row_it != state->visible_row_by_data_index.end()) {
                const int row = row_it->second;
                const int top = ListView_GetTopIndex(state->list);
                int per_page = ListView_GetCountPerPage(state->list);
                if (per_page <= 0) {
                    per_page = 30;
                }
                if (row >= top && row <= (top + per_page + 2)) {
                    ListView_RedrawItems(state->list, row, row);
                }
            }
        }

        if (ready->hbitmap) {
            DeleteObject(ready->hbitmap);
        }
        ++state->thumbs_done;
        if (state->render_in_progress) {
            SetStatusText(state->status, BuildRenderingStatusText(state));
        } else {
            SetStatusText(state->status, BuildReadyStatusText(state, state->shown_count, state->shown_bytes));
        }
        UpdateProgressIndicator(state);
        UpdateCancelThumbsVisibility(state);
        UpdateListOverlay(state, hwnd);
        delete ready;
        return 0;
    }
    case kMsgDupCompleted: {
        auto* payload = reinterpret_cast<AsyncDupResult*>(lparam);
        if (!payload) {
            return 0;
        }
        if (!state) {
            delete payload;
            return 0;
        }
        if (payload->generation != state->dup_generation) {
            delete payload;
            return 0;
        }

        state->duplicate_analysis_running = false;
        state->duplicate_mode = payload->mode;
        state->duplicate_last_result = std::move(payload->result);
        ApplyDuplicateResultFilter(state);
        RefreshList(state);
        UpdateListOverlay(state, hwnd);

        std::wostringstream msg;
        msg << L"Duplicate analysis (" << DuplicateModeText(state->duplicate_mode) << L"): groups "
            << state->duplicate_last_result.groups.size()
            << L", files " << state->duplicate_last_result.duplicate_file_count
            << L", reclaimable " << FormatSize(state->duplicate_last_result.reclaimable_bytes);
        MessageBoxW(hwnd, msg.str().c_str(), L"Duplicate analysis finished", MB_ICONINFORMATION);
        delete payload;
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kSearchDebounceTimerId);
        KillTimer(hwnd, kOverlayTimerId);
        DragAcceptFiles(hwnd, FALSE);
        if (state && state->tree) {
            HTREEITEM root = TreeView_GetRoot(state->tree);
            if (root) {
                FreeTreeItemData(state->tree, root);
            }
        }
        if (state) {
            ++state->scan_generation;
            ++state->dup_generation;
            SetScanningState(state, false);
            ClearPreview(state);
            {
                std::lock_guard<std::mutex> lock(state->thumb_mutex);
                state->thumb_worker_stop = true;
                state->thumb_queue.clear();
                state->thumb_queued_paths.clear();
            }
            state->thumb_cv.notify_all();
            if (state->thumb_worker.joinable()) {
                state->thumb_worker.join();
            }
            for (auto& kv : state->thumbnail_cache) {
                if (kv.second.bitmap) {
                    DeleteObject(kv.second.bitmap);
                }
            }
            state->thumbnail_cache.clear();
            state->thumbnail_lru.clear();
            state->thumbnail_lru_pos.clear();
            state->thumbnail_cache_bytes = 0;
            if (state->large_image_list) {
                ImageList_Destroy(state->large_image_list);
                state->large_image_list = nullptr;
            }
        }
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool RegisterMainWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;
    return RegisterClassExW(&wc) != 0;
}

bool RegisterPreviewWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PreviewWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kPreviewClassName;
    return RegisterClassExW(&wc) != 0;
}

bool RegisterDateRangeWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DateRangeDlgProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kDateRangeClassName;
    return RegisterClassExW(&wc) != 0;
}

}  // namespace

namespace fme {

int RunApp(HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmd_line, int show_cmd) {
    (void)prev_instance;
    (void)cmd_line;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_DATE_CLASSES;
    InitCommonControlsEx(&icc);

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool com_initialized = (com_hr == S_OK || com_hr == S_FALSE);
    if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr, L"Could not initialize COM.", L"Error", MB_ICONERROR);
        return -11;
    }

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok) {
        if (com_initialized) {
            CoUninitialize();
        }
        MessageBoxW(nullptr, L"Could not initialize GDI+.", L"Error", MB_ICONERROR);
        return -10;
    }

    if (!RegisterMainWindowClass(instance)) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        if (com_initialized) {
            CoUninitialize();
        }
        MessageBoxW(nullptr, L"Could not register window class.", L"Error", MB_ICONERROR);
        return -1;
    }
    if (!RegisterPreviewWindowClass(instance)) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        if (com_initialized) {
            CoUninitialize();
        }
        MessageBoxW(nullptr, L"Could not register preview class.", L"Error", MB_ICONERROR);
        return -3;
    }
    if (!RegisterDateRangeWindowClass(instance)) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        if (com_initialized) {
            CoUninitialize();
        }
        MessageBoxW(nullptr, L"Could not register date range class.", L"Error", MB_ICONERROR);
        return -4;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 760,
        nullptr, nullptr, instance, nullptr);

    if (!hwnd) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        if (com_initialized) {
            CoUninitialize();
        }
        MessageBoxW(nullptr, L"Could not create main window.", L"Error", MB_ICONERROR);
        return -2;
    }

    ACCEL accelerators[3]{};
    accelerators[0].fVirt = FCONTROL | FVIRTKEY;
    accelerators[0].key = 'F';
    accelerators[0].cmd = kCmdFocusSearch;
    accelerators[1].fVirt = FVIRTKEY;
    accelerators[1].key = VK_F5;
    accelerators[1].cmd = kCmdRefreshFolder;
    accelerators[2].fVirt = FCONTROL | FVIRTKEY;
    accelerators[2].key = 'O';
    accelerators[2].cmd = kCmdOpenFolder;
    HACCEL haccel = CreateAcceleratorTableW(accelerators, 3);

    ShowWindow(hwnd, show_cmd);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!haccel || !TranslateAcceleratorW(hwnd, haccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (haccel) {
        DestroyAcceleratorTable(haccel);
    }
    Gdiplus::GdiplusShutdown(gdiplus_token);
    if (com_initialized) {
        CoUninitialize();
    }

    return static_cast<int>(msg.wParam);
}

}  // namespace fme



