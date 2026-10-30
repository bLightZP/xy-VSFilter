#include "stdafx.h"
#include "XySubRenderProviderWrapper.h"
#include "../subtitles/RenderLimits.h"
#include <memory>
#include <new>

#if ENABLE_XY_LOG_RENDERER_REQUEST2
#  define TRACE_RENDERER_REQUEST(msg) XY_LOG_TRACE(msg)
#  define TRACE_RENDERER_REQUEST_TIMING(msg) XY_AUTO_TIMING(msg)
#else
#  define TRACE_RENDERER_REQUEST(msg)
#  define TRACE_RENDERER_REQUEST_TIMING(msg)
#endif

XySubRenderProviderWrapper::XySubRenderProviderWrapper( ISubPicProviderEx *provider
    , HRESULT* phr/*=NULL*/ )
    : CUnknown(NAME("XySubRenderProviderWrapper"), NULL, phr)
    , m_provider(provider)
    , m_consumer(NULL)
    , m_use_dst_alpha(false)
{
    HRESULT hr = NOERROR;
    if (!provider)
    {
        hr = E_INVALIDARG;
    }

    if (phr)
    {
        *phr = hr;
    }
}

STDMETHODIMP XySubRenderProviderWrapper::NonDelegatingQueryInterface( REFIID riid, void** ppv )
{
    return
        QI(IXySubRenderProvider)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP XySubRenderProviderWrapper::Connect( IXyOptions *consumer )
{
    HRESULT hr = NOERROR;
    if (consumer)
    {
        hr = m_consumer != consumer ? S_OK : S_FALSE;
        m_consumer = consumer;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

STDMETHODIMP XySubRenderProviderWrapper::RequestFrame( IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now  )
{
    ASSERT(m_consumer);
    double fps;
    CheckPointer(subRenderFrame, E_POINTER);
    *subRenderFrame = NULL;

    HRESULT hr = m_consumer->XyGetDouble(DirectVobSubXyOptions::DOUBLE_FPS, &fps);
    if (FAILED(hr))
    {
        XY_LOG_ERROR("Failed to get fps. "<<XY_LOG_VAR_2_STR(hr));
        return hr;
    }

    CRect output_rect, subtitle_target_rect;
    CSize original_video_size;
    bool use_dst_alpha = false;
    ASSERT(m_consumer);
    hr = m_consumer->XyGetSize(DirectVobSubXyOptions::SIZE_ORIGINAL_VIDEO, &original_video_size);
    ASSERT(SUCCEEDED(hr));
    hr = m_consumer->XyGetBool(DirectVobSubXyOptions::BOOL_SUB_FRAME_USE_DST_ALPHA, &use_dst_alpha);

    if (m_original_video_size!=original_video_size
        || m_use_dst_alpha!=use_dst_alpha)
    {
        if (m_use_dst_alpha==use_dst_alpha)
        {
            XY_LOG_WARN("Original video size changed from "<<m_original_video_size<<" to "<<original_video_size);
        }
        else
        {
            XY_LOG_INFO("'Use dst alpha' option changed from "<<m_use_dst_alpha<<" to "<<use_dst_alpha);
        }
        Invalidate();
        m_original_video_size = original_video_size;
        m_use_dst_alpha = use_dst_alpha;
    }
    if(!m_pSubPic)
    {
        if (!m_allocator)
        {
            ResetAllocator();
        }
    }

    POSITION pos = m_provider->GetStartPosition(now, fps);
    if (!pos)
    {
        return S_FALSE;
    }
    REFERENCE_TIME start = m_provider->GetStart(pos, fps);
    REFERENCE_TIME stop = m_provider->GetStop(pos, fps); //fixme: have to get start stop twice
    if (!(start <= now && now < stop))
    {
        return S_FALSE;
    }

    hr = Render( now, pos, fps );
    if (FAILED(hr))
    {
        return hr;
    }
    if (m_xy_sub_render_frame)
    {
        *subRenderFrame = m_xy_sub_render_frame;
        (*subRenderFrame)->AddRef();
    }

    return hr;
}

HRESULT XySubRenderProviderWrapper::Render( REFERENCE_TIME now, POSITION pos, double fps )
{
    if(m_pSubPic && m_pSubPic->GetStart() <= now && now < m_pSubPic->GetStop())
    {
        return S_OK;
    }
    HRESULT hr = E_FAIL;

    //should always re-alloc one for the old be in used by the consumer
    m_pSubPic = NULL;
    ASSERT(m_allocator);
    if(FAILED(m_allocator->AllocDynamicEx(&m_pSubPic))) {
        XY_LOG_ERROR("Failed to allocate subpic");
        return E_FAIL;
    }
    ASSERT(m_pSubPic);

    if(FAILED(m_provider->Lock())) {
        return hr;
    }

    CMemSubPic * mem_subpic = dynamic_cast<CMemSubPic*>((ISubPicEx *)m_pSubPic);
    ASSERT(mem_subpic);
    SubPicDesc spd;
    if(SUCCEEDED(m_pSubPic->Lock(spd)))
    {
        CAtlList<CRect> rectList;
        DWORD color = 0xFF000000;
        if(SUCCEEDED(m_pSubPic->ClearDirtyRect(color)))
        {
            hr = m_provider->RenderEx(spd, now, fps, rectList);

            if (!m_provider->IsAnimated(pos))
            {
                REFERENCE_TIME start = m_provider->GetStart(pos, fps);
                REFERENCE_TIME stop = m_provider->GetStop(pos, fps);
                XY_LOG_TRACE(XY_LOG_VAR_2_STR(start)<<XY_LOG_VAR_2_STR(stop));
                m_pSubPic->SetStart(start);
                m_pSubPic->SetStop(stop);
            }
            else
            {
                m_pSubPic->SetStart(now);
                m_pSubPic->SetStop(now+1);
            }
        }
        else
        {
            rectList.AddHead(CRect(CPoint(0,0),m_original_video_size));
            XY_LOG_ERROR("Failed to clear subpic!");
        }
        hr = m_pSubPic->UnlockEx(&rectList);
        ASSERT(SUCCEEDED(hr));
        if (FAILED(hr))
        {
            XY_LOG_ERROR("Failed to unlock subpic. "<<XY_LOG_VAR_2_STR(hr));
        }
        CRect dirty_rect;
        hr = m_pSubPic->GetDirtyRect(&dirty_rect);
        ASSERT(SUCCEEDED(hr));
        if (!dirty_rect.IsRectEmpty())
        {
            if (!m_use_dst_alpha)
            {
                hr = mem_subpic->FlipAlphaValue(dirty_rect);//fixme: mem_subpic.type is now MSP_RGBA_F, not MSP_RGBA
                ASSERT(SUCCEEDED(hr));
                if (FAILED(hr))
                {
                    XY_LOG_ERROR("Failed. "<<XY_LOG_VAR_2_STR(hr));
                    return hr;
                }
            }
        }
        else
        {
            hr = S_FALSE;
        }
    }

    m_provider->Unlock();
    if (hr == S_OK)
    {
        CRect video_rect(CPoint(0,0), m_original_video_size);
        m_xy_sub_render_frame = DEBUG_NEW XySubRenderFrameWrapper(mem_subpic, video_rect, video_rect, now, &hr);
    }
    else
    {
        m_xy_sub_render_frame = NULL;
    }
    return hr;
}

HRESULT XySubRenderProviderWrapper::ResetAllocator()
{
    const int MAX_SUBPIC_QUEUE_LENGTH = 1;

    m_allocator = DEBUG_NEW CPooledSubPicAllocator(MSP_RGB32, m_original_video_size, MAX_SUBPIC_QUEUE_LENGTH + 1);
    ASSERT(m_allocator);

    m_allocator->SetCurSize(m_original_video_size);
    m_allocator->SetCurVidRect(CRect(CPoint(0,0),m_original_video_size));
    return S_OK;
}

STDMETHODIMP XySubRenderProviderWrapper::Invalidate( REFERENCE_TIME rtInvalidate /*= -1*/ )
{
    if (m_pSubPic )
    {
        if (m_pSubPic->GetStart()>=rtInvalidate) {
            m_pSubPic = NULL;

            //fix me: important!
            m_xy_sub_render_frame = NULL;
            m_allocator = NULL;
        }
        else if (m_pSubPic->GetStop()>rtInvalidate)
        {
            m_pSubPic->SetStop(rtInvalidate);
        }
    }

    return S_OK;
}

//
// XySubRenderProviderWrapper2
//
XySubRenderProviderWrapper2::XySubRenderProviderWrapper2( ISubPicProviderEx2 *provider
    , HRESULT* phr/*=NULL*/ )
    : CUnknown(NAME("XySubRenderProviderWrapper2"), NULL, phr)
    , m_provider(provider)
    , m_consumer(NULL)
    , m_start(0), m_stop(0)
    , m_fps(0)
    , m_max_bitmap_count2(0)
    , m_use_dst_alpha(0)
    , m_render_to_original_video_size(0)
{
    HRESULT hr = NOERROR;
    if (!provider)
    {
        hr = E_INVALIDARG;
    }

    if (phr)
    {
        *phr = hr;
    }
}

STDMETHODIMP XySubRenderProviderWrapper2::NonDelegatingQueryInterface( REFIID riid, void** ppv )
{
    return
        QI(IXySubRenderProvider)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP XySubRenderProviderWrapper2::Connect( IXyOptions *consumer )
{
    HRESULT hr = NOERROR;
    if (consumer)
    {
        hr = m_consumer != consumer ? S_OK : S_FALSE;
        m_consumer = consumer;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

STDMETHODIMP XySubRenderProviderWrapper2::RequestFrame(IXySubRenderFrame** subRenderFrame, REFERENCE_TIME now)
{
    CheckPointer(subRenderFrame, E_POINTER);
    *subRenderFrame = NULL;
    if (!m_consumer) return E_UNEXPECTED;
    double fps;
    CRect output_rect, subtitle_target_rect;
    CSize layout_size, original_video_size;
    bool use_dst_alpha = false, render_to_original_video_size = false;
    int max_bitmap_count;
    HRESULT hr;
#define READ_OPTION(call) do { hr = (call); if (FAILED(hr)) return hr; } while (0)
    READ_OPTION(m_consumer->XyGetDouble(DirectVobSubXyOptions::DOUBLE_FPS, &fps));
    READ_OPTION(m_consumer->XyGetRect(DirectVobSubXyOptions::RECT_VIDEO_OUTPUT, &output_rect));
    READ_OPTION(m_consumer->XyGetRect(DirectVobSubXyOptions::RECT_SUBTITLE_TARGET, &subtitle_target_rect));
    READ_OPTION(m_consumer->XyGetSize(DirectVobSubXyOptions::SIZE_LAYOUT_WITH, &layout_size));
    READ_OPTION(m_consumer->XyGetInt(DirectVobSubXyOptions::INT_MAX_BITMAP_COUNT2, &max_bitmap_count));
    READ_OPTION(m_consumer->XyGetBool(DirectVobSubXyOptions::BOOL_SUB_FRAME_USE_DST_ALPHA, &use_dst_alpha));
    READ_OPTION(m_consumer->XyGetBool(DirectVobSubXyOptions::BOOL_RENDER_TO_ORIGINAL_VIDEO_SIZE, &render_to_original_video_size));
    if (render_to_original_video_size) {
        READ_OPTION(m_consumer->XyGetSize(DirectVobSubXyOptions::SIZE_ORIGINAL_VIDEO, &original_video_size));
        if (!RenderLimits::Size(original_video_size)) return E_INVALIDARG;
        output_rect = CRect(CPoint(), original_video_size);
        subtitle_target_rect = output_rect;
    }
#undef READ_OPTION
    if (!std::isfinite(fps) || fps <= 0 || max_bitmap_count < 1 ||
        !RenderLimits::Geometry(output_rect, subtitle_target_rect, layout_size)) return E_INVALIDARG;

    const bool changed = m_layout_size != layout_size || m_use_dst_alpha != use_dst_alpha ||
        m_output_rect != output_rect || m_subtitle_target_rect != subtitle_target_rect ||
        m_render_to_original_video_size != render_to_original_video_size ||
        m_fps != fps || m_max_bitmap_count2 != max_bitmap_count;
    if (changed) Invalidate();
    m_layout_size = layout_size;
    m_use_dst_alpha = use_dst_alpha;
    m_output_rect = output_rect;
    m_subtitle_target_rect = subtitle_target_rect;
    m_render_to_original_video_size = render_to_original_video_size;
    m_fps = fps;
    m_max_bitmap_count2 = max_bitmap_count;

    POSITION pos = m_provider->GetStartPosition(now, fps);
    if (!pos)
    {
        XY_LOG_TRACE("No subtitles at "<<XY_LOG_VAR_2_STR(now));
        return S_FALSE;
    }
    REFERENCE_TIME start = m_provider->GetStart(pos, fps);
    REFERENCE_TIME stop = m_provider->GetStop(pos, fps); //fixme: have to get start stop twice
    if (!(start <= now && now < stop))
    {
        XY_LOG_TRACE("No subtitles at "<<XY_LOG_VAR_2_STR(now));
        return S_FALSE;
    }

    hr = Render( now, pos );
    if (FAILED(hr))
    {
        return hr;
    }
    if (m_xy_sub_render_frame)
    {
        *subRenderFrame = m_xy_sub_render_frame;
        (*subRenderFrame)->AddRef();
    }
    return hr;
}

STDMETHODIMP XySubRenderProviderWrapper2::Invalidate( REFERENCE_TIME rtInvalidate /*= -1*/ )
{
    if (m_start>=rtInvalidate)
    {
        m_xy_sub_render_frame = NULL;
        m_start = m_stop = 0;
    }
    else if (m_stop>rtInvalidate)
    {
        m_stop = rtInvalidate;
    }


    return S_OK;
}

HRESULT XySubRenderProviderWrapper2::Render(REFERENCE_TIME now, POSITION pos)
{
    if (m_start <= now && now < m_stop && m_xy_sub_render_frame) return S_FALSE;
    m_xy_sub_render_frame.Release();
    m_start = m_stop = 0;
    HRESULT hr = m_provider->Lock();
    if (FAILED(hr)) return hr;
    struct UnlockProvider {
        ISubPicProviderEx2* provider;
        ~UnlockProvider() { provider->Unlock(); }
    } unlock = {m_provider};
    try {
        const int spd_type = m_use_dst_alpha ? MSP_RGBA : MSP_RGBA_F;
        hr = m_provider->RenderEx(&m_xy_sub_render_frame, spd_type,
            m_output_rect, m_subtitle_target_rect, m_layout_size, now, m_fps);
        if (SUCCEEDED(hr) && m_xy_sub_render_frame) {
            int count = 0;
            hr = m_xy_sub_render_frame->GetBitmapCount(&count);
            if (SUCCEEDED(hr) && count > m_max_bitmap_count2) hr = CombineBitmap(now);
        }
        if (SUCCEEDED(hr)) {
            if (!m_provider->IsAnimated(pos)) {
                m_start = m_provider->GetStart(pos, m_fps);
                m_stop = m_provider->GetStop(pos, m_fps);
            } else {
                m_start = now;
                m_stop = now + 1;
            }
        }
    } catch (const std::bad_alloc&) { hr = E_OUTOFMEMORY; }
      catch (const CAtlException& error) { hr = error.m_hr; }
    if (FAILED(hr)) m_xy_sub_render_frame.Release();
    return hr;
}

HRESULT XySubRenderProviderWrapper2::CombineBitmap(REFERENCE_TIME now)
{
    static LONG combined_id = 0x10000000;
    int count = 0;
    HRESULT hr = m_xy_sub_render_frame->GetBitmapCount(&count);
    if (FAILED(hr) || count <= 1) return hr;
    CRect bounds;
    for (int i = 0; i < count; ++i) {
        POINT pos; SIZE size;
        hr = m_xy_sub_render_frame->GetBitmap(i, NULL, &pos, &size, NULL, NULL);
        if (FAILED(hr)) return hr;
        if (!RenderLimits::Size(size) || pos.x < -RenderLimits::MaxDimension || pos.y < -RenderLimits::MaxDimension ||
            pos.x > RenderLimits::MaxDimension || pos.y > RenderLimits::MaxDimension) return E_INVALIDARG;
        CRect rect(pos, size);
        rect &= m_subtitle_target_rect;
        if (!rect.IsRectEmpty()) bounds |= rect;
    }
    std::unique_ptr<XySubRenderFrame> frame(new XySubRenderFrame());
    frame->m_output_rect = m_output_rect;
    frame->m_clip_rect = m_subtitle_target_rect;
    frame->m_xy_color_space = m_use_dst_alpha ? XY_CS_ARGB : XY_CS_ARGB_F;
    frame->m_left = frame->m_top = 0;
    if (!bounds.IsRectEmpty()) {
        XySubRenderFrame::SharedBitmap bitmap(XyBitmap::CreateBitmap(bounds, XyBitmap::PACK));
        if (!bitmap) return E_OUTOFMEMORY;
        // CreateBitmap clears to inverse-alpha transparent black.
        if (!m_use_dst_alpha) XyBitmap::FlipAlphaValue(bitmap->bits, bitmap->w, bitmap->h, bitmap->pitch);
        SubPicDesc spd = {};
        spd.type = MSP_RGB32; spd.bpp = 32;
        spd.w = bitmap->w; spd.h = bitmap->h; spd.pitch = bitmap->pitch; spd.bits = (BYTE*)bitmap->bits;
        for (int i = 0; i < count; ++i) {
            POINT pos; SIZE size; LPCVOID pixels; int pitch;
            hr = m_xy_sub_render_frame->GetBitmap(i, NULL, &pos, &size, &pixels, &pitch);
            if (FAILED(hr)) return hr;
            if (!pixels || !RenderLimits::Size(size) || pitch < size.cx * 4) return E_INVALIDARG;
            pos.x -= bounds.left; pos.y -= bounds.top;
            XyBitmap::BltPack(spd, pos, size, pixels, pitch);
        }
        bitmap->x -= m_output_rect.left;
        bitmap->y -= m_output_rect.top;
        frame->m_bitmaps.Add(bitmap);
        frame->m_bitmap_ids.Add(InterlockedIncrement(&combined_id));
    }
    m_xy_sub_render_frame = frame.release();
    return S_OK;
}
