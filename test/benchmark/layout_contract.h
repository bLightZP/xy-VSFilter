// Included after the harness frame-comparison helpers.
#include "../../src/subpic/XySubRenderProviderWrapper.h"
#include "../../src/subpic/ConsumerInvalidation.h"
#include "../../src/subtitles/RenderLimits.h"

class LayoutOptions : public CUnknown, public XyOptionsImpl
{
public:
    DECLARE_IUNKNOWN;
    LayoutOptions() : CUnknown(NAME("layout options"), NULL), XyOptionsImpl(DirectVobSubXyOptions::XyVobFilterOptions) {
        using namespace DirectVobSubXyOptions;
        m_xy_rect_opt[RECT_VIDEO_OUTPUT] = m_xy_rect_opt[RECT_SUBTITLE_TARGET] = CRect(0,0,1920,1080);
        m_xy_size_opt[SIZE_LAYOUT_WITH] = CSize(7680,4320);
        m_xy_size_opt[SIZE_ORIGINAL_VIDEO] = CSize(1920,1080);
        m_xy_double_opt[DOUBLE_FPS] = 24;
        m_xy_int_opt[INT_MAX_BITMAP_COUNT2] = 8;
    }
    void Output(int width, int height) {
        m_xy_rect_opt[DirectVobSubXyOptions::RECT_VIDEO_OUTPUT] =
            m_xy_rect_opt[DirectVobSubXyOptions::RECT_SUBTITLE_TARGET] = CRect(0,0,width,height);
    }
    void Limit(int count) { m_xy_int_opt[DirectVobSubXyOptions::INT_MAX_BITMAP_COUNT2] = count; }
};

class LayoutProvider : public CRenderedTextSubtitle
{
public:
    int calls, locks, count;
    HRESULT failure;
    CRect output;
    CSize layout;
    LayoutProvider(CCritSec* lock) : CRenderedTextSubtitle(lock), calls(0), locks(0), count(2), failure(S_OK) {}
    STDMETHODIMP Lock() { ++locks; return S_OK; }
    STDMETHODIMP Unlock() { --locks; return S_OK; }
    STDMETHODIMP_(POSITION) GetStartPosition(REFERENCE_TIME, double) { return reinterpret_cast<POSITION>(1); }
    STDMETHODIMP_(REFERENCE_TIME) GetStart(POSITION, double) { return 0; }
    STDMETHODIMP_(REFERENCE_TIME) GetStop(POSITION, double) { return 1000000000; }
    STDMETHODIMP_(bool) IsAnimated(POSITION) { return false; }
    STDMETHODIMP RenderEx(IXySubRenderFrame** result, int format, const RECT& video, const RECT& target,
        const SIZE& size, REFERENCE_TIME, double) {
        ++calls; output = video; layout = size; *result = NULL;
        if (FAILED(failure)) return failure;
        std::unique_ptr<XySubRenderFrame> frame(new XySubRenderFrame());
        frame->m_output_rect = video; frame->m_clip_rect = target;
        frame->m_xy_color_space = format == MSP_RGBA ? XY_CS_ARGB : XY_CS_ARGB_F;
        frame->m_left = frame->m_top = 0;
        for (int i=0; i<count; ++i) {
            XySubRenderFrame::SharedBitmap bitmap(XyBitmap::CreateBitmap(CRect(10+i*20,10,12+i*20,12),XyBitmap::PACK));
            if (!bitmap) return E_OUTOFMEMORY;
            for (int y=0;y<2;++y) for(int x=0;x<2;++x)
                reinterpret_cast<DWORD*>(bitmap->plans[0]+y*bitmap->pitch)[x] = format == MSP_RGBA ? 0x7f000040 : 0x80000040;
            frame->m_bitmaps.Add(bitmap); frame->m_bitmap_ids.Add(calls*8+i);
        }
        (*result = frame.release())->AddRef();
        return S_OK;
    }
};

class ClearConsumer : public CUnknown, public ISubRenderConsumer2
{
public:
    ConsumerInvalidation* pending;
    bool extension, reenter;
    int clears;
    DECLARE_IUNKNOWN;
    ClearConsumer(ConsumerInvalidation* queue) : CUnknown(NAME("clear consumer"),NULL), pending(queue), extension(true), reenter(false), clears(0) {}
    STDMETHODIMP NonDelegatingQueryInterface(REFIID iid, void** value) {
        if (iid == __uuidof(ISubRenderConsumer2) && extension) return GetInterface((ISubRenderConsumer2*)this,value);
        if (iid == __uuidof(ISubRenderConsumer)) return GetInterface((ISubRenderConsumer*)this,value);
        return CUnknown::NonDelegatingQueryInterface(iid,value);
    }
#define UNSUPPORTED_OPTION(type, name) \
    STDMETHODIMP Get##name(LPCSTR,type*) { return E_NOTIMPL; } \
    STDMETHODIMP Set##name(LPCSTR,type) { return E_NOTIMPL; }
    UNSUPPORTED_OPTION(bool,Bool)
    UNSUPPORTED_OPTION(int,Int)
    UNSUPPORTED_OPTION(SIZE,Size)
    UNSUPPORTED_OPTION(RECT,Rect)
    UNSUPPORTED_OPTION(ULONGLONG,Ulonglong)
    UNSUPPORTED_OPTION(double,Double)
#undef UNSUPPORTED_OPTION
    STDMETHODIMP GetString(LPCSTR,LPWSTR*,int*) { return E_NOTIMPL; }
    STDMETHODIMP SetString(LPCSTR,LPWSTR,int) { return E_NOTIMPL; }
    STDMETHODIMP GetBin(LPCSTR,LPVOID*,int*) { return E_NOTIMPL; }
    STDMETHODIMP SetBin(LPCSTR,LPVOID,int) { return E_NOTIMPL; }
    STDMETHODIMP GetMerit(ULONG* value) { *value=0;return S_OK; }
    STDMETHODIMP Connect(ISubRenderProvider*) { return S_OK; }
    STDMETHODIMP Disconnect() { return S_OK; }
    STDMETHODIMP DeliverFrame(REFERENCE_TIME,REFERENCE_TIME,LPVOID,ISubRenderFrame*) { return S_OK; }
    static DWORD WINAPI InvalidateOnAnotherThread(void* value) {
        static_cast<ConsumerInvalidation*>(value)->Schedule(); return 0;
    }
    STDMETHODIMP Clear(REFERENCE_TIME time) {
        Require(time==0,"Clear did not use the video timeline reset boundary");
        ++clears;
        if (reenter) {
            reenter=false;
            HANDLE thread=CreateThread(NULL,0,InvalidateOnAnotherThread,pending,0,NULL);
            Require(thread && WaitForSingleObject(thread,2000)==WAIT_OBJECT_0,"Clear held the pending-state lock across the consumer callback");
            CloseHandle(thread);
        }
        return S_OK;
    }
};

static int LayoutContract()
{
    using namespace DirectVobSubXyOptions;
    CCritSec lock;
    LayoutProvider* source = new LayoutProvider(&lock);
    CComPtr<ISubPicProviderEx2> sourceOwner = source;
    CComPtr<IXySubRenderProvider> wrapper = new XySubRenderProviderWrapper2(source,NULL);
    LayoutOptions options;
    Require(SUCCEEDED(wrapper->Connect(&options)),"Cannot connect layout wrapper");
    const auto render = [&]() {
        CComPtr<IXySubRenderFrame> frame;
        Require(SUCCEEDED(wrapper->RequestFrame(&frame,1000000)) && frame,"Wrapper render failed");
        return frame;
    };
    XyAllocationStats stats={};
    XyAllocationStats* previous=xy_set_allocation_stats(&stats);
    CComPtr<IXySubRenderFrame> original=render();
    xy_set_allocation_stats(previous);
    Require(source->output==CRect(0,0,1920,1080) && source->layout==CSize(7680,4320),"Layout was used as raster size");
    Require(stats.requestedBytes<4096,"Noncombined frame allocated a full-canvas pool");
    render(); Require(source->calls==1,"Unchanged static frame was not reused");
    options.Output(3840,2160); render();
    Require(source->calls==2 && source->output.Width()==3840,"Geometry change reused a stale static frame");
    options.XySetBool(BOOL_RENDER_TO_ORIGINAL_VIDEO_SIZE,true); render();
    Require(source->calls==3 && source->output==CRect(0,0,1920,1080) && source->layout==CSize(7680,4320),"Original-size checkbox used custom layout dimensions");
    options.XySetDouble(DOUBLE_FPS,25); render(); Require(source->calls==4,"FPS did not invalidate static output");
    options.Output(7680,4320); options.XySetBool(BOOL_RENDER_TO_ORIGINAL_VIDEO_SIZE,false);
    options.Limit(1);
    std::vector<CComPtr<IXySubRenderFrame>> retained;
    for (int mode=0;mode<2;++mode) {
        options.XySetBool(BOOL_SUB_FRAME_USE_DST_ALPHA,mode!=0);
        for (int i=0;i<64;++i) {
            wrapper->Invalidate();
            stats=XyAllocationStats(); previous=xy_set_allocation_stats(&stats);
            CComPtr<IXySubRenderFrame> frame=render();
            xy_set_allocation_stats(previous);
            Require(stats.requestedBytes<4096,"Combining allocated a full 8K target");
            int count,space,pitch; POINT pos; SIZE size; LPCVOID pixels;
            frame->GetBitmapCount(&count);frame->GetXyColorSpace(&space);
            frame->GetBitmap(0,NULL,&pos,&size,&pixels,&pitch);
            Require(count==1 && pos.x==10 && pos.y==10 && size.cx==22 && size.cy==2,"Combined dirty bounds changed");
            Require(space==(mode ? XY_CS_ARGB : XY_CS_ARGB_F),"Combining changed alpha convention");
            const DWORD* row=static_cast<const DWORD*>(pixels);
            Require(row[0]==(mode ? 0x7f000040u : 0x80000040u) && row[5]==(mode ? 0xff000000u : 0u),"Combined content or transparent gap has wrong alpha");
            retained.push_back(frame);
        }
    }
    for(size_t i=0;i<retained.size();++i) {
        LPCVOID pixels;retained[i]->GetBitmap(0,NULL,NULL,NULL,&pixels,NULL);
        Require(static_cast<const DWORD*>(pixels)[5]==(i>=64 ? 0xff000000u : 0u),"A retained frame was mutated");
    }
    options.Output(16,16);
    CComPtr<IXySubRenderFrame> clipped=render();
    SIZE clippedSize;clipped->GetBitmap(0,NULL,NULL,&clippedSize,NULL,NULL);
    Require(clippedSize.cx==2 && clippedSize.cy==2,"Fully outside bitmap was not clipped during combining");
    options.Output(8,8);
    CComPtr<IXySubRenderFrame> empty=render();int emptyCount;empty->GetBitmapCount(&emptyCount);
    Require(emptyCount==0,"Fully clipped frame did not remain empty");
    options.Output(7680,4320);
    options.Limit(8);render();
    source->failure=E_OUTOFMEMORY;wrapper->Invalidate();
    CComPtr<IXySubRenderFrame> failed;
    Require(wrapper->RequestFrame(&failed,1000000)==E_OUTOFMEMORY && !failed && source->locks==0,"Wrapper masked failure or retained the provider lock");
    source->failure=S_OK;
    xy_set_allocation_failure_countdown(0);
    HRESULT hr=wrapper->RequestFrame(&failed,1000000);
    xy_set_allocation_failure_countdown(-1);
    Require(hr==E_OUTOFMEMORY && !failed && source->locks==0,"Allocation failure escaped wrapper cleanup");
    render();
    Require(!XyBitmap::CreateBitmap(CRect(0,0,INT_MAX,INT_MAX),XyBitmap::PACK),"Overflowing bitmap size was accepted");
    xy_set_allocation_failure_countdown(0);
    XyBitmap* missing=XyBitmap::CreateBitmap(CRect(0,0,16,16),XyBitmap::PLANNA);
    xy_set_allocation_failure_countdown(-1);
    Require(!missing,"Failed planar allocation did not return null");
    Require(!XyBitmap::CreateBitmap(CRect(0,0,32768,32768),XyBitmap::PACK),"32-bit allocation product overflow was accepted");
    Require(!xy_malloc(std::size_t(-1)),"Aligned allocator overflow was accepted");

    Overlay input; input.mOverlayWidth=input.mOverlayHeight=16;input.mOverlayPitch=16;
    input.mWidth=input.mHeight=128;input.mfWideOutlineEmpty=true;
    input.mBody.reset((BYTE*)xy_malloc(256),xy_free);memset(input.mBody.get(),64,256);
    SharedPtrOverlay output(new Overlay());
    Require(Rasterizer::Blur(input,0,3,1,1,output),"Cannot warm Gaussian coefficients");
    bool blurred=false, sawScratchFailure=false;
    for (int fault=0;fault<6;++fault) {
        xy_set_allocation_failure_countdown(fault);
        try {
            blurred=Rasterizer::Blur(input,0,3,1,1,output);
            if (!blurred) sawScratchFailure=true;
        } catch (const std::bad_alloc&) { /* coefficient allocation: COM boundary handles this */ }
        xy_set_allocation_failure_countdown(-1);
    }
    Require(sawScratchFailure,"Gaussian scratch allocation failure was hidden");
    xy_set_allocation_failure_countdown(0);
    blurred=Rasterizer::Blur(input,1,0,1,1,output);
    xy_set_allocation_failure_countdown(-1);
    Require(!blurred,"Edge-blur allocation failure was hidden");
    Require(!Rasterizer::Blur(input,0,3,1920,1080,output),"Runaway blur work was admitted");
    STSStyle blurStyle; blurStyle.fGaussianBlur=3;
    CPolygon word(FwSTSStyle(blurStyle),L"m 0 0 l 4 0 4 4 0 4",0,0,0,1.,1.,0);
    OverlayKey key(word,CPoint(),CPoint());
    SharedPtrOverlay wordResult, unblurred(new Overlay(input));
    bool rejectedBlur=false;
    xy_set_allocation_failure_countdown(0);
    try { word.PaintFromNoneBluredOverlay(unblurred,key,&wordResult); }
    catch (const std::bad_alloc&) { rejectedBlur=true; }
    xy_set_allocation_failure_countdown(-1);
    Require(rejectedBlur && !wordResult,"Failed blur published a partial paint-machine result");
    word.PaintFromNoneBluredOverlay(unblurred,key,&wordResult);
    Require(wordResult && wordResult->mBody,"Blur did not recover after allocation failure");
    const RECT video={0,0,1920,1080}; const SIZE tiny={1,1};
    CRenderedTextSubtitle renderer(&lock);
    Require(renderer.RenderEx(&failed,MSP_RGBA,video,video,tiny,0,24)==E_INVALIDARG && !failed,"Tiny layout did not fail before rendering");

    const CStringA script = "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,20,&H000000FF,&H0000FFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,{\\an7\\pos(20,20)\\bord0\\shad0\\blur1\\p1}m 0 0 l 4 0 4 4 0 4\n";
    Require(renderer.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"layout-contract"),"Cannot open layout fixture");
    renderer.SetLookaheadBudget(0);
    const SIZE dimensions[]={{1920,1080},{7680,4320}};
    for(int backend=0;backend<2;++backend) {
        renderer.SetPreferLibass(backend!=0);
        Require(renderer.IsUsingLibass()==(backend!=0),"Requested test backend is unavailable");
        for(int raster=0;raster<2;++raster) for(int logical=0;logical<2;++logical) {
            const RECT rect={0,0,dimensions[raster].cx,dimensions[raster].cy};
            CComPtr<IXySubRenderFrame> a,b;
            Require(SUCCEEDED(renderer.RenderEx(&a,MSP_RGBA,rect,rect,dimensions[logical],1000000,24)) && a,
                "Backend rejected supported 1080p/8K geometry");
            RECT actual;a->GetOutputRect(&actual);
            Require(EqualRect(&actual,&rect),"Backend changed actual output dimensions");
            Require(SUCCEEDED(renderer.RenderEx(&b,MSP_RGBA,rect,rect,dimensions[logical],1000000,24)) && EqualFrames(a,b),
                "Backend output changed on repeated layout render");
        }
        // Force bitmap allocation despite warm caches, and verify recovery and ownership.
        CacheManager::SetBitmapCacheSize(0);
        const RECT rect={0,0,320,180};const SIZE logical={320,180};
        CComPtr<IXySubRenderFrame> expected,recovered;
        Require(SUCCEEDED(renderer.RenderEx(&expected,MSP_RGBA,rect,rect,logical,1000000,24)) && expected,"Cannot render OOM reference");
        xy_set_allocation_failure_countdown(0);
        hr=renderer.RenderEx(&failed,MSP_RGBA,rect,rect,logical,1000000,24);
        xy_set_allocation_failure_countdown(-1);
        Require(hr==E_OUTOFMEMORY && !failed,"Backend did not propagate bitmap OOM");
        Require(SUCCEEDED(renderer.RenderEx(&recovered,MSP_RGBA,rect,rect,logical,1000000,24)) && EqualFrames(expected,recovered),
            "OOM cached partial output or changed a retained frame");
        CacheManager::SetBitmapCacheSize(256);
    }

    ConsumerInvalidation queue;
    ClearConsumer* consumer=new ClearConsumer(&queue);
    CComPtr<ISubRenderConsumer> consumerOwner=consumer;
    const ULONGLONG before=queue.Generation();
    queue.Schedule();queue.Schedule();consumer->reenter=true;
    Require(SUCCEEDED(queue.Dispatch(consumer)) && consumer->clears==1,"Clear requests were not coalesced");
    Require(SUCCEEDED(queue.Dispatch(consumer)) && consumer->clears==2,"Reentrant invalidation was lost");
    Require(queue.Dispatch(consumer)==S_FALSE,"Unchanged queue was cleared repeatedly");
    queue.Delivered(before);queue.Dispatch(consumer);
    Require(consumer->clears==3,"Delivery racing Clear did not reschedule invalidation");
    consumer->extension=false;queue.Schedule();
    Require(queue.Dispatch(consumer)==S_FALSE && consumer->clears==3,"Legacy consumer was not supported");
    printf("layout_contract=passed (dimensions, static invalidation, bounded combining, alpha, 128 retained frames, allocation failures, reentrant queue clearing)\n");
    return 0;
}

class IsolatedLayoutRegistry
{
    HKEY m_key;
    CStringW m_path;
public:
    IsolatedLayoutRegistry() : m_key(NULL) {
        m_path.Format(L"Software\\xySubFilterLayoutTests\\layout-%lu",GetCurrentProcessId());
        DWORD disposition;
        Require(RegCreateKeyExW(HKEY_CURRENT_USER,m_path,0,NULL,0,KEY_ALL_ACCESS,NULL,&m_key,&disposition)==ERROR_SUCCESS &&
            disposition==REG_CREATED_NEW_KEY,"Cannot create isolated test settings");
        Require(RegOverridePredefKey(HKEY_CURRENT_USER,m_key)==ERROR_SUCCESS,"Cannot isolate test settings");
    }
    ~IsolatedLayoutRegistry() {
        RegOverridePredefKey(HKEY_CURRENT_USER,NULL);
        RegCloseKey(m_key);
        RegDeleteTreeW(HKEY_CURRENT_USER,m_path);
    }
};

class LayoutPageSite : public CUnknown, public IPropertyPageSite
{
public:
    DECLARE_IUNKNOWN;
    LayoutPageSite() : CUnknown(NAME("layout page site"),NULL) {}
    STDMETHODIMP OnStatusChange(DWORD) { return S_OK; }
    STDMETHODIMP GetLocaleID(LCID* value) { *value=GetUserDefaultLCID();return S_OK; }
    STDMETHODIMP GetPageContainer(IUnknown** value) { *value=NULL;return E_NOTIMPL; }
    STDMETHODIMP TranslateAccelerator(MSG*) { return E_NOTIMPL; }
};

static int LayoutFilterContract(const wchar_t* path, bool xy)
{
    using namespace DirectVobSubXyOptions;
    IsolatedLayoutRegistry registry;
    Require(SUCCEEDED(CoInitializeEx(NULL,COINIT_APARTMENTTHREADED)),"Cannot initialize COM");
    const wchar_t* settingsPath=xy ? L"Software\\Gabest\\XySubFilter\\General" : L"Software\\Gabest\\VSFilter\\General";
    HKEY settings;
    Require(RegCreateKeyExW(HKEY_CURRENT_USER,settingsPath,0,NULL,0,KEY_ALL_ACCESS,NULL,&settings,NULL)==ERROR_SUCCESS,"Cannot seed isolated settings");
    DWORD width=1,height=720;
    RegSetValueExW(settings,L"user_specified_render_size_x",0,REG_DWORD,(BYTE*)&width,sizeof(width));
    RegSetValueExW(settings,L"user_specified_render_size_y",0,REG_DWORD,(BYTE*)&height,sizeof(height));
    HMODULE module=LoadLibraryW(path);
    Require(module!=NULL,"Cannot load layout-test DLL");
    typedef HRESULT (STDAPICALLTYPE *GetClass)(REFCLSID,REFIID,void**);
    GetClass getClass=reinterpret_cast<GetClass>(GetProcAddress(module,"DllGetClassObject"));
    Require(getClass!=NULL,"Filter has no class factory");
    {
        CLSID clsid,pageId;
        CLSIDFromString(xy ? L"{2dfcb782-ec20-4a7c-b530-4577adb33f21}" : L"{93A22E7A-5091-45ef-BA61-6DA26156A5D0}",&clsid);
        CLSIDFromString(xy ? L"{E7946C91-1083-4F0E-AC45-5CF6BE7DB4C7}" : L"{69CE757B-E8C0-4B0A-9EA0-CEA284096F98}",&pageId);
        CComPtr<IClassFactory> factory;
        Require(SUCCEEDED(getClass(clsid,IID_IClassFactory,(void**)&factory)),"Cannot get filter class");
        CComPtr<IUnknown> object;
        Require(SUCCEEDED(factory->CreateInstance(NULL,IID_IUnknown,(void**)&object)),"Cannot create filter");
        CComQIPtr<IXyOptions> options(object);
        SIZE size={};options->XyGetSize(SIZE_USER_SPECIFIED_LAYOUT_SIZE,&size);
        Require(size.cx==1280 && size.cy==720,"Unsafe saved layout did not recover to default");
        const SIZE invalid[]={{1,1080},{0,720},{-1,720},{INT_MAX,720},{12801,720}};
        for(int i=0;i<_countof(invalid);++i) {
            Require(options->XySetSize(SIZE_USER_SPECIFIED_LAYOUT_SIZE,invalid[i])==E_INVALIDARG,"API accepted an invalid layout");
            options->XyGetSize(SIZE_USER_SPECIFIED_LAYOUT_SIZE,&size);
            Require(size.cx==1280 && size.cy==720,"Rejected API size mutated live settings");
        }
        RenderLayoutSettings transaction={LAYOUT_SIZE_OPT_USER_SPECIFIED,{7680,4320},TRUE};
        Require(SUCCEEDED(options->XySetBin(BIN2_RENDER_LAYOUT,&transaction,sizeof(transaction))),"8K layout transaction was rejected");
        transaction.size.cx=1;transaction.mode=LAYOUT_SIZE_OPT_ORIGINAL_VIDEO_SIZE;
        Require(options->XySetBin(BIN2_RENDER_LAYOUT,&transaction,sizeof(transaction))==E_INVALIDARG,"Invalid layout transaction succeeded");
        int mode;options->XyGetInt(INT_LAYOUT_SIZE_OPT,&mode);options->XyGetSize(SIZE_USER_SPECIFIED_LAYOUT_SIZE,&size);
        Require(mode==LAYOUT_SIZE_OPT_USER_SPECIFIED && size.cx==7680 && size.cy==4320,"Invalid transaction partially committed");
        CComPtr<IClassFactory> pageFactory;
        Require(SUCCEEDED(getClass(pageId,IID_IClassFactory,(void**)&pageFactory)),"Cannot get More page class");
        CComPtr<IPropertyPage> page;
        Require(SUCCEEDED(pageFactory->CreateInstance(NULL,IID_IPropertyPage,(void**)&page)),"Cannot create More page");
        CComPtr<IPropertyPageSite> site=new LayoutPageSite();
        Require(SUCCEEDED(page->SetPageSite(site)),"Cannot set More page site");
        IUnknown* connected=object;
        Require(SUCCEEDED(page->SetObjects(1,&connected)),"Cannot connect More page");
        PROPPAGEINFO info={sizeof(info)};page->GetPageInfo(&info);
        HWND host=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,info.size.cx+20,info.size.cy+20,NULL,NULL,g_hInst,NULL);
        RECT bounds={0,0,info.size.cx,info.size.cy};
        Require(host && SUCCEEDED(page->Activate(host,&bounds,FALSE)),"Cannot activate More page");
        HWND dialog=FindWindowExW(host,NULL,L"#32770",NULL);
        Require(dialog && GetDlgItem(dialog,IDC_EDIT_LAYOUT_SIZE_X),"Missing layout input controls");
        const wchar_t* edits[]={L"",L"1",L"19",L"192",L"1920",L"1",L"1920"};
        for(int i=0;i<_countof(edits);++i) {
            SetDlgItemTextW(dialog,IDC_EDIT_LAYOUT_SIZE_X,edits[i]);
            options->XyGetSize(SIZE_USER_SPECIFIED_LAYOUT_SIZE,&size);
            DWORD bytes=sizeof(width);RegQueryValueExW(settings,L"user_specified_render_size_x",NULL,NULL,(BYTE*)&width,&bytes);
            Require(size.cx==7680 && width==1,"Typing applied or persisted partial settings");
        }
        SetDlgItemTextW(dialog,IDC_EDIT_LAYOUT_SIZE_X,L"1");
        // An unrelated control notification must not run the full-page apply path.
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_COMBO_SUBPIXEL_POS,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,IDC_COMBO_SUBPIXEL_POS));
        options->XyGetSize(SIZE_USER_SPECIFIED_LAYOUT_SIZE,&size);
        Require(size.cx==7680,"Another control committed unfinished dimensions");
        Require(page->Apply()==E_INVALIDARG,"Invalid Apply was not rejected");
        SetDlgItemTextW(dialog,IDC_EDIT_LAYOUT_SIZE_X,L"1920");
        SetDlgItemTextW(dialog,IDC_EDIT_LAYOUT_SIZE_Y,L"1080");
        Require(SUCCEEDED(page->Apply()),"Complete layout did not apply");
        options->XyGetSize(SIZE_USER_SPECIFIED_LAYOUT_SIZE,&size);
        DWORD bytes=sizeof(width);RegQueryValueExW(settings,L"user_specified_render_size_x",NULL,NULL,(BYTE*)&width,&bytes);
        Require(size.cx==1920 && size.cy==1080 && width==1920,"Explicit Apply did not commit and persist complete dimensions");
        SetDlgItemTextW(dialog,IDC_EDIT_LAYOUT_SIZE_X,L"3840");
        page->Deactivate();page->SetObjects(0,NULL);page->SetPageSite(NULL);DestroyWindow(host);
        options->XyGetSize(SIZE_USER_SPECIFIED_LAYOUT_SIZE,&size);
        Require(size.cx==1920,"Closing without Apply committed staged edits");
        CoTaskMemFree(info.pszTitle);CoTaskMemFree(info.pszDocString);CoTaskMemFree(info.pszHelpFile);
    }
    FreeLibrary(module);RegCloseKey(settings);CoUninitialize();
    printf("layout_filter_contract=passed (isolated registry recovery, API transaction, typing, invalid Apply, valid Apply, cancel)\n");
    return 0;
}
