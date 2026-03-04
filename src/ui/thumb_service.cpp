#include "fme/thumb_service.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shobjidl.h>

namespace fme::ui {

HBITMAP CreateImageThumbnailBitmap(const std::wstring& path, int thumb_size) {
    Gdiplus::Bitmap src(path.c_str());
    if (src.GetLastStatus() != Gdiplus::Ok || src.GetWidth() == 0 || src.GetHeight() == 0) {
        return nullptr;
    }

    Gdiplus::Bitmap canvas(thumb_size, thumb_size, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&canvas);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.Clear(Gdiplus::Color(255, 245, 245, 245));

    const double sx = static_cast<double>(thumb_size) / static_cast<double>(src.GetWidth());
    const double sy = static_cast<double>(thumb_size) / static_cast<double>(src.GetHeight());
    const double scale = sx < sy ? sx : sy;
    const int dw = static_cast<int>(src.GetWidth() * scale);
    const int dh = static_cast<int>(src.GetHeight() * scale);
    const int dx = (thumb_size - dw) / 2;
    const int dy = (thumb_size - dh) / 2;
    g.DrawImage(&src, dx, dy, dw, dh);

    HBITMAP hbitmap = nullptr;
    if (canvas.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbitmap) != Gdiplus::Ok || !hbitmap) {
        return nullptr;
    }
    return hbitmap;
}

HBITMAP CreateVideoThumbnailBitmap(const std::wstring& path, int thumb_size) {
    IShellItemImageFactory* factory = nullptr;
    const HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        return nullptr;
    }

    SIZE size{ thumb_size, thumb_size };
    HBITMAP hbitmap = nullptr;
    HRESULT img_hr = factory->GetImage(size, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hbitmap);
    if (FAILED(img_hr) || !hbitmap) {
        img_hr = factory->GetImage(size, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbitmap);
    }
    factory->Release();
    if (FAILED(img_hr) || !hbitmap) {
        return nullptr;
    }
    return hbitmap;
}

} // namespace fme::ui
