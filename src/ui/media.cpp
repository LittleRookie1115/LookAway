#include "ui/media.hpp"

#include <algorithm>
#include <cstring>

namespace lookaway::media {

GifAnimation::~GifAnimation() {
    clear();
}

bool GifAnimation::load(HINSTANCE instance, int resource_id) {
    clear();
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) {
        return false;
    }
    HGLOBAL loaded_resource = LoadResource(instance, resource);
    const DWORD resource_size = SizeofResource(instance, resource);
    const void* resource_data = LockResource(loaded_resource);
    if (!resource_data || resource_size == 0) {
        return false;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, resource_size);
    if (!memory) {
        return false;
    }
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, resource_data, resource_size);
    GlobalUnlock(memory);

    if (CreateStreamOnHGlobal(memory, TRUE, &stream_) != S_OK) {
        GlobalFree(memory);
        return false;
    }
    image_.reset(Gdiplus::Image::FromStream(stream_, FALSE));
    if (!image_ || image_->GetLastStatus() != Gdiplus::Ok) {
        clear();
        return false;
    }

    const UINT dimension_count = image_->GetFrameDimensionsCount();
    if (dimension_count == 0) {
        clear();
        return false;
    }
    std::vector<GUID> dimensions(dimension_count);
    if (image_->GetFrameDimensionsList(dimensions.data(), dimension_count) != Gdiplus::Ok) {
        clear();
        return false;
    }
    frame_dimension_ = dimensions.front();
    frame_count_ = image_->GetFrameCount(&frame_dimension_);
    if (frame_count_ == 0) {
        clear();
        return false;
    }

    frame_delays_.assign(frame_count_, 100);
    constexpr PROPID frame_delay_property = 0x5100;
    const UINT property_size = image_->GetPropertyItemSize(frame_delay_property);
    if (property_size >= sizeof(Gdiplus::PropertyItem)) {
        std::vector<BYTE> property_buffer(property_size);
        auto* property = reinterpret_cast<Gdiplus::PropertyItem*>(property_buffer.data());
        if (image_->GetPropertyItem(frame_delay_property, property_size, property) == Gdiplus::Ok &&
            property->value && property->length >= frame_count_ * sizeof(UINT)) {
            const auto* delays = static_cast<const UINT*>(property->value);
            for (UINT index = 0; index < frame_count_; ++index) {
                frame_delays_[index] = std::max<UINT>(20, delays[index] * 10);
            }
        }
    }
    restart();
    return true;
}

void GifAnimation::restart() {
    frame_index_ = 0;
    elapsed_in_frame_ = 0;
    if (image_ && frame_count_ > 0) {
        image_->SelectActiveFrame(&frame_dimension_, 0);
    }
}

bool GifAnimation::advance(ULONGLONG elapsed_ms) {
    if (!image_ || frame_count_ <= 1) {
        return false;
    }
    elapsed_in_frame_ += std::min<ULONGLONG>(elapsed_ms, 1000);
    bool changed = false;
    while (elapsed_in_frame_ >= frame_delays_[frame_index_]) {
        elapsed_in_frame_ -= frame_delays_[frame_index_];
        frame_index_ = (frame_index_ + 1) % frame_count_;
        image_->SelectActiveFrame(&frame_dimension_, frame_index_);
        changed = true;
    }
    return changed;
}

void GifAnimation::draw(HDC dc, const RECT& bounds) const {
    if (!image_) {
        return;
    }
    const UINT source_width = image_->GetWidth();
    const UINT source_height = image_->GetHeight();
    if (source_width == 0 || source_height == 0) {
        return;
    }
    const int available_width = bounds.right - bounds.left;
    const int available_height = bounds.bottom - bounds.top;
    const double scale = std::min(
        static_cast<double>(available_width) / source_width,
        static_cast<double>(available_height) / source_height);
    const int width = std::max(1, static_cast<int>(source_width * scale));
    const int height = std::max(1, static_cast<int>(source_height * scale));
    const int left = bounds.left + (available_width - width) / 2;
    const int top = bounds.top + (available_height - height) / 2;

    Gdiplus::Graphics graphics(dc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.DrawImage(image_.get(), left, top, width, height);
}

void GifAnimation::clear() {
    image_.reset();
    if (stream_) {
        stream_->Release();
        stream_ = nullptr;
    }
    frame_delays_.clear();
    frame_count_ = 0;
    frame_index_ = 0;
    elapsed_in_frame_ = 0;
}

StaticImage::~StaticImage() {
    clear();
}

bool StaticImage::load(HINSTANCE instance, int resource_id) {
    clear();
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) {
        return false;
    }
    HGLOBAL loaded_resource = LoadResource(instance, resource);
    const DWORD resource_size = SizeofResource(instance, resource);
    const void* resource_data = LockResource(loaded_resource);
    if (!resource_data || resource_size == 0) {
        return false;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, resource_size);
    if (!memory) {
        return false;
    }
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, resource_data, resource_size);
    GlobalUnlock(memory);

    if (CreateStreamOnHGlobal(memory, TRUE, &stream_) != S_OK) {
        GlobalFree(memory);
        return false;
    }
    image_.reset(Gdiplus::Image::FromStream(stream_, FALSE));
    if (!image_ || image_->GetLastStatus() != Gdiplus::Ok) {
        clear();
        return false;
    }
    return true;
}

bool StaticImage::loaded() const {
    return image_ != nullptr;
}

void StaticImage::draw(HDC dc, const RECT& bounds) const {
    if (!image_) {
        return;
    }
    const UINT source_width = image_->GetWidth();
    const UINT source_height = image_->GetHeight();
    const int available_width = bounds.right - bounds.left;
    const int available_height = bounds.bottom - bounds.top;
    if (source_width == 0 || source_height == 0 || available_width <= 0 ||
        available_height <= 0) {
        return;
    }
    const double scale = std::min(
        static_cast<double>(available_width) / source_width,
        static_cast<double>(available_height) / source_height);
    const int width = std::max(1, static_cast<int>(source_width * scale));
    const int height = std::max(1, static_cast<int>(source_height * scale));
    const int left = bounds.left + (available_width - width) / 2;
    const int top = bounds.top + (available_height - height) / 2;

    Gdiplus::Graphics graphics(dc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.DrawImage(image_.get(), left, top, width, height);
}

void StaticImage::clear() {
    image_.reset();
    if (stream_) {
        stream_->Release();
        stream_ = nullptr;
    }
}

}  // namespace lookaway::media
