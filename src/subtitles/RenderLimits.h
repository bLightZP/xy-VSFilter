#pragma once

#include <cstddef>
#include <limits.h>
#include <cmath>

// Admission limits, not allocation targets. 8K output remains supported.
namespace RenderLimits {
    const int MinUserLayout = 16;
    const int MaxUserLayout = 12800;
    const int MaxDimension = 32768;
    const std::size_t MaxBufferBytes = std::size_t(256) * 1024 * 1024;

    inline bool UserLayout(const SIZE& size) {
        return size.cx >= MinUserLayout && size.cy >= MinUserLayout &&
            size.cx <= MaxUserLayout && size.cy <= MaxUserLayout;
    }
    inline bool Size(const SIZE& size) {
        return size.cx > 0 && size.cy > 0 && size.cx <= MaxDimension && size.cy <= MaxDimension;
    }
    inline bool Rect(const RECT& rect) {
        const LONGLONG w = LONGLONG(rect.right) - rect.left, h = LONGLONG(rect.bottom) - rect.top;
        return w > 0 && h > 0 && w <= MaxDimension && h <= MaxDimension &&
            rect.left >= -MaxDimension && rect.top >= -MaxDimension &&
            rect.right <= MaxDimension && rect.bottom <= MaxDimension;
    }
    inline bool Geometry(const RECT& video, const RECT& target, const SIZE& layout) {
        return Rect(video) && Rect(target) && Size(layout) &&
            LONGLONG(video.right) - video.left <= LONGLONG(layout.cx) * 64 &&
            LONGLONG(video.bottom) - video.top <= LONGLONG(layout.cy) * 64;
    }
    inline bool Buffer(int width, int height, int bytesPerPixel, int& pitch, std::size_t& bytes) {
        if (width <= 0 || height <= 0 || width > MaxDimension || height > MaxDimension ||
            bytesPerPixel <= 0 || bytesPerPixel > 4) return false;
        pitch = ((width + 15) & ~15) * bytesPerPixel;
        if (std::size_t(height) > MaxBufferBytes / std::size_t(pitch)) return false;
        bytes = std::size_t(pitch) * height;
        return bytes <= MaxBufferBytes;
    }
}
