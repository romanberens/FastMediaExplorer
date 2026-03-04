#pragma once

#include <windows.h>
#include <string>

namespace fme::ui {

HBITMAP CreateImageThumbnailBitmap(const std::wstring& path, int thumb_size);
HBITMAP CreateVideoThumbnailBitmap(const std::wstring& path, int thumb_size);

} // namespace fme::ui

