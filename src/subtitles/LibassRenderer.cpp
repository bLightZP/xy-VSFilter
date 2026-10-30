#include "stdafx.h"
#include "LibassRenderer.h"
#include "xy_bitmap.h"
#include "ass.h"
#include "../dsutil/DSUtil.h"
#include <limits>
#include <stdexcept>

static void LibassMessage(int level, const char* format, va_list args, void*)
{
    if (level > 2) return;
    char text[2048];
    vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    XY_LOG_WARN("libass: " << text);
}

LibassRenderer::LibassRenderer()
    : m_library(NULL), m_renderer(NULL), m_track(NULL), m_embedded(false) {}

LibassRenderer::~LibassRenderer()
{
    if (m_track) ass_free_track(m_track);
    if (m_renderer) ass_renderer_done(m_renderer);
    if (m_library) ass_library_done(m_library);
}

void LibassRenderer::EnsureLibrary()
{
    if (m_library) return;
    m_library = ass_library_init();
    if (!m_library) throw std::bad_alloc();
    ass_set_message_cb(m_library, LibassMessage, NULL);
    ass_set_extract_fonts(m_library, 1);
}

void LibassRenderer::Invalidate()
{
    if (m_track) ass_free_track(m_track);
    m_track = NULL;
}

void LibassRenderer::SetSource(const CStringA& source, bool embedded)
{
    Invalidate();
    m_source = source;
    m_embedded = embedded;
    m_packets.clear();
}

void LibassRenderer::SetEmbedded(bool embedded)
{
    if (m_embedded != embedded) { m_embedded = embedded; Invalidate(); }
}

void LibassRenderer::EnsureTrack()
{
    if (m_track && m_renderer) return;
    EnsureLibrary();
    if (!m_track) {
    if (!m_overrides.IsEmpty()) {
        std::vector<char*> fields;
        char* context = NULL;
        CStringA copy(m_overrides);
        for (char* p = strtok_s(copy.GetBuffer(), "\n", &context); p; p = strtok_s(NULL, "\n", &context)) fields.push_back(p);
        fields.push_back(NULL);
        ass_set_style_overrides(m_library, &fields[0]);
    } else ass_set_style_overrides(m_library, NULL);
    if (m_embedded) {
        m_track = ass_new_track(m_library);
        if (m_track) ass_process_codec_private(m_track, m_source.GetString(), m_source.GetLength());
    } else {
        m_track = ass_read_memory(m_library, m_source.GetBuffer(), m_source.GetLength(), "UTF-8");
        m_source.ReleaseBuffer();
    }
    if (!m_track) throw std::runtime_error("libass cannot load ASS track");
    ass_process_force_style(m_track);
    for (size_t i = 0; i < m_packets.size(); ++i) {
        const Packet& p = m_packets[i];
        ass_process_chunk(m_track, p.data.GetString(), p.data.GetLength(), p.start / 10000, (p.stop - p.start) / 10000);
    }
    }
    if (!m_renderer) {
        m_renderer = ass_renderer_init(m_library);
        if (!m_renderer) throw std::bad_alloc();
        ass_set_fonts(m_renderer, NULL, "Arial", ASS_FONTPROVIDER_DIRECTWRITE, NULL, 1);
    }
}

void LibassRenderer::AddPacket(const BYTE* data, int length, REFERENCE_TIME start, REFERENCE_TIME stop)
{
    if (!m_embedded || !data || length <= 0 || stop <= start) return;
    Packet packet = { CStringA(reinterpret_cast<const char*>(data), length), start, stop };
    m_packets.push_back(packet);
    if (m_track) ass_process_chunk(m_track, packet.data.GetString(), length, start / 10000, (stop - start) / 10000);
}

void LibassRenderer::Flush()
{
    m_packets.clear();
    if (m_track) ass_flush_events(m_track);
}

void LibassRenderer::AddFont(const char* name, const void* data, int length)
{
    EnsureLibrary();
    Invalidate();
    // A font selector must be rebuilt after adding attachments.
    if (m_renderer) { ass_renderer_done(m_renderer); m_renderer = NULL; }
    ass_add_font(m_library, name, static_cast<const char*>(data), length);
}

void LibassRenderer::LoadFonts(const wchar_t* folder)
{
    WIN32_FIND_DATAW item;
    CStringW pattern = CStringW(folder) + L"\\*";
    HANDLE search = FindFirstFileW(pattern, &item);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        CStringW name(item.cFileName), ext = name.Right(4);
        if (ext.CompareNoCase(L".ttf") && ext.CompareNoCase(L".otf")) continue;
        CFile file;
        if (!file.Open(CStringW(folder) + L"\\" + name, CFile::modeRead | CFile::shareDenyNone)) continue;
        ULONGLONG length = file.GetLength();
        if (!length || length > INT_MAX) continue;
        std::vector<BYTE> data(static_cast<size_t>(length));
        if (file.Read(&data[0], static_cast<UINT>(length)) == length) AddFont(UTF16To8(name), &data[0], static_cast<int>(length));
    } while (FindNextFileW(search, &item));
    FindClose(search);
}

static CStringA StyleOverrides(const STSStyle& style, const CStringA& prefix)
{
    CStringA value;
    {
        const CRect& margins = style.marginRect.get();
        value.Format("FontName=%s\nFontSize=%.9g\nBold=%d\nItalic=%d\nUnderline=%d\nStrikeOut=%d\nScaleX=%.9g\nScaleY=%.9g\nSpacing=%.9g\nAngle=%.9g\nBorderStyle=%d\nOutline=%.9g\nShadow=%.9g\nAlignment=%d\nMarginL=%d\nMarginR=%d\nMarginV=%d\n",
            UTF16To8(style.fontName).GetString(), style.fontSize, style.fontWeight >= FW_BOLD, style.fItalic, style.fUnderline, style.fStrikeOut,
            style.fontScaleX, style.fontScaleY, style.fontSpacing, style.fontAngleZ, style.borderStyle == 1 ? 3 : 1,
            style.outlineWidthX, style.shadowDepthX, style.scrAlignment, margins.left, margins.right, margins.bottom);
        const char* colors[] = {"PrimaryColour", "SecondaryColour", "OutlineColour", "BackColour"};
        for (int i = 0; i < 4; ++i) {
            CStringA color;
            color.Format("%s=&H%02X%06X\n", colors[i], style.alpha[i], style.colors[i] & 0xffffff);
            value += color;
        }
    }
    CStringA result;
    int at=0;
    while (at < value.GetLength()) {
        const int end=value.Find('\n',at);
        if (end < 0) break;
        result += prefix + value.Mid(at,end-at) + "\n";
        at=end+1;
    }
    return result;
}

void LibassRenderer::Configure(const STSStyle& style, bool forceDefault,
    const CSTSStyleMap& styles, const StyleBaseline& originals)
{
    CStringA value;
    if (forceDefault) value=StyleOverrides(style,CStringA());
    else {
        POSITION pos=styles.GetStartPosition();
        while (pos) {
            CString name; STSStyle* current;
            styles.GetNextAssoc(pos,name,current);
            StyleBaseline::const_iterator old=originals.find(name);
            if (old==originals.end() || *current!=old->second)
                value += StyleOverrides(*current,UTF16To8(name)+".");
        }
    }
    if (value != m_overrides) { m_overrides=value; Invalidate(); }
}

HRESULT LibassRenderer::Render(IXySubRenderFrame** output, const RECT& video, const SIZE& storage,
    REFERENCE_TIME time, XySubRenderFrameCreater* creator)
{
    EnsureTrack();
    CRect frame(video);
    if (frame.Width() <= 0 || frame.Height() <= 0 || storage.cx <= 0 || storage.cy <= 0) return E_INVALIDARG;
    ass_set_frame_size(m_renderer, frame.Width(), frame.Height());
    ass_set_storage_size(m_renderer, storage.cx, storage.cy);
    ass_set_pixel_aspect(m_renderer, double(frame.Width()) * storage.cy / (double(frame.Height()) * storage.cx));
    int changed;
    ASS_Image* images = ass_render_frame(m_renderer, m_track, time / 10000, &changed);
    CRect bounds(0,0,0,0), viewport(0,0,frame.Width(),frame.Height());
    for (ASS_Image* image = images; image; image = image->next) {
        if (image->w <= 0 || image->h <= 0 || (image->color & 255) == 255) continue;
        CRect rect(image->dst_x, image->dst_y, image->dst_x + image->w, image->dst_y + image->h);
        rect &= viewport;
        bounds |= rect;
    }
    CComPtr<IXySubRenderFrame> owned;
    XySubRenderFrame* result = creator->NewXySubRenderFrame(bounds.IsRectEmpty() ? 0 : 1);
    owned = result;
    if (!bounds.IsRectEmpty()) {
        XySubRenderFrame::SharedBitmap bitmap(creator->CreateBitmap(bounds));
        if (!bitmap) return E_OUTOFMEMORY;
        XyColorSpace space;
        creator->GetColorSpace(&space);
        for (ASS_Image* image = images; image; image = image->next) {
            if (image->w <= 0 || image->h <= 0 || (image->color & 255) == 255) continue;
            CRect area(image->dst_x, image->dst_y, image->dst_x + image->w, image->dst_y + image->h);
            area &= bounds;
            const unsigned opacity = 255 - (image->color & 255);
            const DWORD rgba = image->color;
            const DWORD argb = 0xff000000 | ((rgba >> 8) & 0xffffff);
            const DWORD color = creator->TransColor(argb);
            for (int y = area.top; y < area.bottom; ++y) {
                const BYTE* mask = image->bitmap + (y - image->dst_y) * image->stride + area.left - image->dst_x;
                for (int x = area.left; x < area.right; ++x, ++mask) {
                    const unsigned alpha = (*mask * opacity + 127) / 255, inverse = 255 - alpha;
                    if (bitmap->type == XyBitmap::PLANNA) {
                        const size_t offset = (y - bounds.top) * bitmap->pitch + x - bounds.left;
                        bitmap->plans[0][offset] = (bitmap->plans[0][offset] * inverse + 127) / 255;
                        for (int c = 1; c < 4; ++c) {
                            const unsigned component = (color >> (8 * (3 - c))) & 255;
                            bitmap->plans[c][offset] = (component * alpha + bitmap->plans[c][offset] * inverse + 127) / 255;
                        }
                    } else {
                        BYTE* pixel = bitmap->plans[0] + (y - bounds.top) * bitmap->pitch + (x - bounds.left) * 4;
                        for (int c = 0; c < 3; ++c) pixel[c] = (((color >> (8*c)) & 255) * alpha + pixel[c] * inverse + 127) / 255;
                        pixel[3] = (pixel[3] * inverse + 127) / 255;
                    }
                }
            }
        }
        if (space == XY_CS_ARGB_F) XyBitmap::FlipAlphaValue(bitmap->bits, bitmap->w, bitmap->h, bitmap->pitch);
        static LONG identity = 0;
        result->m_bitmap_ids[0] = InterlockedIncrement(&identity);
        result->m_bitmaps[0] = bitmap;
    }
    result->MoveTo(video.left, video.top);
    *output = owned.Detach();
    return bounds.IsRectEmpty() ? S_FALSE : S_OK;
}
