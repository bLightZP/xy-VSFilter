// Included after the frame hashing and isolated filter/consumer helpers.
#include "../../src/subtitles/xy_widen_region.h"
#include "../../src/filters/transform/vsfilter/csri.h"
#include <algorithm>

extern int gen_left_arc(int[], int, int);

static unsigned __int64 OutlinePoint(int x, int y)
{
    return (static_cast<unsigned __int64>(0x40000000+y)<<32) | unsigned(0x40000000+x);
}

// Deliberately slow union of every expanded source span. It shares only the
// rasterized ellipse kernel, not the optimized intersection/expiry algorithm.
static tSpanBuffer OutlineReference(const tSpanBuffer& source, int rx, int ry)
{
    std::vector<int> arc(2*ry+1);
    gen_left_arc(arc.data(),rx,ry);
    tSpanBuffer expanded, result;
    for (const auto& span:source) for (int dy=-ry;dy<=ry;++dy) {
        const int y=int(span.first>>32)-0x40000000+dy;
        expanded.push_back(tSpan(OutlinePoint(int(unsigned(span.first))-0x40000000+arc[dy+ry],y),
            OutlinePoint(int(unsigned(span.second))-0x40000000-arc[dy+ry],y)));
    }
    std::sort(expanded.begin(),expanded.end());
    for (const auto& span:expanded) {
        if (!result.empty() && result.back().second>=span.first)
            result.back().second=max(result.back().second,span.second);
        else result.push_back(span);
    }
    return result;
}

static int OutlineContract()
{
    auto widener=WidenRegionCreater::GetDefaultWidenRegionCreater();
    int comparisons=0;
    for (const auto radius : {CSize(0,1),CSize(0,166),CSize(1,1),CSize(8,8),
                             CSize(64,64),CSize(82,82),CSize(166,166),CSize(3,166),CSize(166,3)}) {
        for (int shape=0;shape<4;++shape) {
            // The existing analytic crossings and Bresenham kernel can differ
            // by one scan unit for very eccentric overlapping ellipses. Test
            // those kernels with isolated spans, without imposing new rounding.
            if (radius.cx && radius.cx!=radius.cy && (shape==1 || shape==2)) continue;
            tSpanBuffer source,actual;
            // Single spans, rectangles, separated islands and rows with gaps.
            // These have exact union coverage even with the legacy ellipse rounding.
            for (int y=0;y<(shape==0 ? 1 : 12);++y) {
                const int row=shape==3 ? y*(2*radius.cy+3) : y;
                source.push_back(tSpan(OutlinePoint(0,row),OutlinePoint(20,row)));
                if (shape==2) source.push_back(tSpan(OutlinePoint(2*radius.cx+30,row),OutlinePoint(2*radius.cx+50,row)));
            }
            widener->xy_overlap_region(&actual,source,radius.cx,radius.cy);
            const auto expected=OutlineReference(source,radius.cx,radius.cy);
            if (actual!=expected) fprintf(stderr,"Outline mismatch rx=%ld ry=%ld shape=%d actual=%zu expected=%zu\n",radius.cx,radius.cy,shape,actual.size(),expected.size());
            Require(actual==expected,"Outline differs from independent span union");
            ++comparisons;
        }
    }
    const tSpanBuffer valid={tSpan(OutlinePoint(0,0),OutlinePoint(5,0))};
    const auto rejects=[&](const tSpanBuffer& source,int rx,int ry) {
        tSpanBuffer destination=valid;
        bool rejected=false;
        try { widener->xy_overlap_region(&destination,source,rx,ry); }
        catch (const CAtlException& e) { rejected=e.m_hr==E_INVALIDARG; }
        Require(rejected && destination.empty(),"Invalid outline did not fail atomically");
        widener->xy_overlap_region(&destination,valid,2,2);
        Require(destination==OutlineReference(valid,2,2),"Rejected request poisoned next outline");
    };
    rejects(valid,-1,1); rejects(valid,1,0); rejects(valid,INT_MAX,1); rejects(valid,1,INT_MAX);
    rejects({tSpan(OutlinePoint(5,0),OutlinePoint(0,0))},1,1);
    rejects({tSpan(OutlinePoint(0,0),OutlinePoint(5,1))},1,1);
    rejects({valid[0],valid[0]},1,1);
    rejects({tSpan(OutlinePoint(0,2),OutlinePoint(5,2)),valid[0]},1,1);
    rejects({tSpan(1,5)},1,1); // no room for the vertical radius
    rejects({tSpan((static_cast<unsigned __int64>(INT_MAX-1)<<32)|100,
                   (static_cast<unsigned __int64>(INT_MAX-1)<<32)|105)},1,1);
    tSpanBuffer empty;
    bool rejected=false;
    try { widener->xy_overlap_region(&empty,empty,1,1); }
    catch (const CAtlException& e) { rejected=e.m_hr==E_INVALIDARG; }
    Require(rejected,"Aliased outline was accepted");
    tSpanBuffer destination;
    widener->xy_overlap_region(&destination,empty,1,1);
    Require(destination.empty(),"Empty outline produced spans");
    printf("outline_contract=passed (%d reference unions, invalid inputs and recovery)\n",comparisons);
    return 0;
}

class OutlineHash {
    BCRYPT_ALG_HANDLE algorithm;
public:
    OutlineHash() : algorithm(NULL) {
        Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0,"Cannot open outline hash");
    }
    ~OutlineHash() { BCryptCloseAlgorithmProvider(algorithm,0); }
    template<class Frame> CStringA FrameDigest(Frame* frame, bool visible) {
        int count=0;
        CStringA digest=FrameSha256(frame,algorithm,&count);
        Require(!visible || count>0,"Outline regression silently lost the subtitle");
        return digest;
    }
};

class OutlineConsumer : public QueueContractConsumer {
public:
    CSize output;
    CComPtr<ISubRenderFrame> latest;
    OutlineConsumer() : output(3840,2160) {}
    STDMETHODIMP GetRect(LPCSTR,RECT* value) { *value=RECT{0,0,output.cx,output.cy};return S_OK; }
    STDMETHODIMP GetUlonglong(LPCSTR,ULONGLONG* value) { *value=417083;return S_OK; }
    STDMETHODIMP DeliverFrame(REFERENCE_TIME,REFERENCE_TIME,LPVOID,ISubRenderFrame* frame) {
        latest=frame; ++deliveries; return S_OK;
    }
    STDMETHODIMP Clear(REFERENCE_TIME) { return S_OK; }
};

static int OutlineXyContract(HMODULE module,const wchar_t* fixture)
{
    using namespace DirectVobSubXyOptions;
    typedef HRESULT(STDAPICALLTYPE* GetClass)(REFCLSID,REFIID,void**);
    auto getClass=reinterpret_cast<GetClass>(GetProcAddress(module,"DllGetClassObject"));
    Require(getClass!=NULL,"Missing filter class factory");
    CLSID clsid; CLSIDFromString(L"{2dfcb782-ec20-4a7c-b530-4577adb33f21}",&clsid);
    CComPtr<IClassFactory> factory;
    Require(SUCCEEDED(getClass(clsid,IID_IClassFactory,(void**)&factory)),"Cannot get outline filter factory");
    OutlineHash hash;
    for (int reopen=0;reopen<2;++reopen) {
        CComPtr<IBaseFilter> filter;
        Require(SUCCEEDED(factory->CreateInstance(NULL,IID_IBaseFilter,(void**)&filter)),"Cannot create outline filter");
        CComQIPtr<IXyOptions> options(filter);
        Require(options && SUCCEEDED(options->XySetInt(INT_LOAD_SETTINGS_LEVEL,1)) &&
            SUCCEEDED(options->XySetBool(BOOL_PREFER_LIBASS,false)),"Cannot select native filter");
        CComPtr<IFilterGraph2> graph;
        Require(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"Cannot create outline graph");
        auto consumer=new OutlineConsumer(); CComPtr<IBaseFilter> owner=consumer;
        Require(SUCCEEDED(graph->AddFilter(owner,L"Outline consumer")) &&
            SUCCEEDED(graph->AddFilter(filter,L"XySubFilter")),"Cannot add outline filters");
        Require(SUCCEEDED(options->XySetString(STRING_FILE_NAME,const_cast<LPWSTR>(fixture),(int)wcslen(fixture))),"Cannot open outline fixture");
        RenderLayoutSettings layout={LAYOUT_SIZE_OPT_USER_SPECIFIED,{7680,4320},FALSE};
        Require(SUCCEEDED(options->XySetBin(BIN2_RENDER_LAYOUT,&layout,sizeof(layout))),"Cannot set outline layout");
        Require(SUCCEEDED(filter->Pause()) && consumer->provider,"Cannot connect outline provider");
        std::vector<CComPtr<ISubRenderFrame>> retained;
        std::vector<CStringA> saved;
        for (const auto geometry : {CSize(3840,2160),CSize(7680,4320),CSize(1920,1080),CSize(3840,2160)}) {
            consumer->output=geometry;
            layout.size=geometry; // exercise live layout changes as well as 8K layout/4K output
            if (retained.empty()) layout.size=CSize(7680,4320);
            Require(SUCCEEDED(options->XySetBin(BIN2_RENDER_LAYOUT,&layout,sizeof(layout))),"Cannot change outline layout");
            // First request is the cold crash in v3.2.1.940; subsequent requests
            // alternate both sides of the border transforms and the event end.
            const REFERENCE_TIME times[]={706330625,705500000,705830000,706490000,708499999,708500000,0,706330625};
            CStringA first;
            for (int i=0;i<32;++i) {
                const auto time=times[i%_countof(times)];
                consumer->latest.Release(); const LONG deliveries=consumer->deliveries;
                Require(SUCCEEDED(consumer->provider->RequestFrame(time,time+1,NULL)) &&
                    consumer->deliveries==deliveries+1,"Outline request failed or was not delivered");
                CStringA digest=hash.FrameDigest(consumer->latest.p,time==706330625);
                if (i==0) first=digest;
                if (time==706330625) Require(digest==first,"Seeking changed outline pixels");
                retained.push_back(consumer->latest); saved.push_back(digest);
            }
        }
        // A consumer may retain a whole 128-frame queue through stop/removal.
        Require(SUCCEEDED(filter->Stop()) && SUCCEEDED(consumer->provider->Disconnect()),"Outline teardown failed");
        consumer->provider.Release(); consumer->latest.Release();
        graph->RemoveFilter(filter); graph->RemoveFilter(owner); options.Release(); filter.Release(); graph.Release();
        for (size_t i=0;i<retained.size();++i)
            Require(hash.FrameDigest(retained[i].p,false)==saved[i],"Retained outline frame changed after seek/teardown");
    }
    return 0;
}

static int OutlineVsContract(HMODULE module,const wchar_t* fixture)
{
    auto open=reinterpret_cast<decltype(&csri_open_file)>(GetProcAddress(module,"csri_open_file"));
    auto close=reinterpret_cast<decltype(&csri_close)>(GetProcAddress(module,"csri_close"));
    auto format=reinterpret_cast<decltype(&csri_request_fmt)>(GetProcAddress(module,"csri_request_fmt"));
    auto render=reinterpret_cast<decltype(&csri_render)>(GetProcAddress(module,"csri_render"));
    Require(open && close && format && render,"Missing VSFilter CSRI exports");
    for (int reopen=0;reopen<2;++reopen) {
        csri_inst* instance=open(NULL,CW2A(fixture,CP_UTF8),NULL);
        Require(instance!=NULL,"Cannot open VSFilter outline fixture");
        const csri_fmt fmt={CSRI_F_BGR_,3840,2160};
        Require(format(instance,&fmt)==0,"Cannot set VSFilter outline format");
        std::vector<BYTE> pixels(size_t(fmt.width)*fmt.height*4),first;
        csri_frame frame={}; frame.pixfmt=fmt.pixfmt; frame.planes[0]=pixels.data();frame.strides[0]=fmt.width*4;
        for (double time : {70.6330625,70.55,70.583,70.649,70.849,70.85,0.0,70.6330625}) {
            std::fill(pixels.begin(),pixels.end(),BYTE(0));
            render(instance,&frame,time);
            if (first.empty()) {
                Require(std::find_if(pixels.begin(),pixels.end(),[](BYTE b){return b!=0;})!=pixels.end(),"VSFilter dropped outline");
                first=pixels;
            }
            if (time==70.6330625) Require(pixels==first,"VSFilter seek changed outline pixels");
        }
        close(instance);
    }
    return 0;
}

static int OutlineFilterContract(const wchar_t* dll,const wchar_t* fixture,bool xy)
{
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    IsolatedLayoutRegistry settings;
    HKEY paths=NULL;
    Require(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Gabest\\XySubFilter\\DefTextPathes",0,NULL,0,KEY_ALL_ACCESS,NULL,&paths,NULL)==ERROR_SUCCESS,"Cannot initialize test search path");
    const wchar_t current[]=L".";
    RegSetValueExW(paths,L"Path0",0,REG_SZ,(const BYTE*)current,sizeof(current)); RegCloseKey(paths);
    Require(SUCCEEDED(CoInitializeEx(NULL,COINIT_MULTITHREADED)),"Cannot initialize outline COM");
    HMODULE module=LoadLibraryW(dll); Require(module!=NULL,"Cannot load outline DLL");
    const int result=xy ? OutlineXyContract(module,fixture) : OutlineVsContract(module,fixture);
    FreeLibrary(module); CoUninitialize();
    printf("outline_filter_contract=passed (%s: cold crash, transforms, seeks, reopen/close)\n",xy ? "xy, 128 retained frames" : "vs CSRI");
    return result;
}
