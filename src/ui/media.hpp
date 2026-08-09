#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <memory>
#include <vector>

namespace lookaway::media {

class GifAnimation {
public:
    GifAnimation() = default;
    GifAnimation(const GifAnimation&) = delete;
    GifAnimation& operator=(const GifAnimation&) = delete;
    ~GifAnimation();

    bool load(HINSTANCE instance, int resource_id);
    void restart();
    bool advance(ULONGLONG elapsed_ms);
    void draw(HDC dc, const RECT& bounds) const;

private:
    void clear();

    IStream* stream_{};
    std::unique_ptr<Gdiplus::Image> image_;
    GUID frame_dimension_{};
    std::vector<UINT> frame_delays_;
    UINT frame_count_{};
    UINT frame_index_{};
    ULONGLONG elapsed_in_frame_{};
};

class StaticImage {
public:
    StaticImage() = default;
    StaticImage(const StaticImage&) = delete;
    StaticImage& operator=(const StaticImage&) = delete;
    ~StaticImage();

    bool load(HINSTANCE instance, int resource_id);
    [[nodiscard]] bool loaded() const;
    void draw(HDC dc, const RECT& bounds) const;

private:
    void clear();

    IStream* stream_{};
    std::unique_ptr<Gdiplus::Image> image_;
};

}  // namespace lookaway::media
