// Actual-DLL queue contract; the consumer deliberately refills inside Clear.
#include "moreuuids.h"
#include "../../src/filters/transform/vsfilter/IDirectVobSub.h"

static int AlphaWrapContract()
{
    const char header[]="[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,24,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,";
    const char drawing[]="}m 0 0 l 40 0 40 40 0 40\n";
    CCritSec lock;const RECT rect={0,0,320,180};const SIZE size={320,180};
    for (int backend=0;backend<2;++backend) {
        for (int spelling=0;spelling<2;++spelling) {
            CStringA script=header;script+="{\\an7\\pos(20,20)\\bord0\\shad0\\p1\\alpha&H00&\\t(0,1000,\\alpha&";
            script+=spelling ? "HF00&)" : "F00&)";script+=drawing;
            CRenderedTextSubtitle animated(&lock);
            Require(animated.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"alpha-wrap"),"Cannot open alpha fixture");
            animated.SetPreferLibass(backend!=0);
            Require(animated.IsUsingLibass()==(backend!=0),"Wrong alpha fixture backend");
            for (int ms : {0,50,100,150,200,250,300,500,750,1000}) {
                CStringA expectedScript=header,tag;
                tag.Format("{\\an7\\pos(20,20)\\bord0\\shad0\\p1\\alpha&H%02X&",(3840*ms/1000)&255);
                expectedScript+=tag;expectedScript+=drawing;
                CRenderedTextSubtitle expected(&lock);
                Require(expected.Open((BYTE*)expectedScript.GetString(),expectedScript.GetLength(),DEFAULT_CHARSET,L"alpha-reference"),"Cannot open alpha reference");
                expected.SetPreferLibass(backend!=0);
                CComPtr<IXySubRenderFrame> a,b;
                Require(SUCCEEDED(animated.RenderEx(&a,MSP_RGBA,rect,rect,size,REFERENCE_TIME(ms)*10000,24)) &&
                    SUCCEEDED(expected.RenderEx(&b,MSP_RGBA,rect,rect,size,REFERENCE_TIME(ms)*10000,24)) && EqualFrames(a,b),
                    "Out-of-range alpha no longer wraps after interpolation");
            }
        }
    }
    printf("alpha_wrap_contract=passed (both backends, both spellings, 40 frame comparisons)\n");
    return 0;
}

static int QueueStateContract()
{
    ConsumerInvalidation q;
    auto c = new ClearConsumer(&q);
    CComPtr<ISubRenderConsumer> owner = c;
    const auto dispatch = [&]() { return q.Dispatch(c, q.Epoch()); };
    ConsumerInvalidation::Delivery early, late;
    q.Begin(early, 100, 100); q.Begin(late, 200, 200);
    q.Packet(200,0,1,1); q.Packet(300,0,1,1);
    Require(dispatch()==S_FALSE,"Future/equal-end packet cleared the queue");
    q.Packet(150,0,1,1); q.Packet(175,0,1,1);
    Require(dispatch()==S_OK && c->boundary==149,"Partial clears lost their earliest boundary");
    q.Finish(early,true);
    Require(dispatch()==S_FALSE,"Unaffected delivery restarted a clear");
    q.Finish(late,true);
    Require(dispatch()==S_OK && c->boundary==149,"Late delivery lost its partial invalidation");
    Require(dispatch()==S_FALSE,"Partial clear repeated without a cause");
    ConsumerInvalidation::Delivery failed;
    q.Begin(failed,300,300); q.Packet(250,0,1,1); dispatch(); q.Finish(failed,false);
    Require(dispatch()==S_FALSE,"Failed delivery restarted a clear");
    ConsumerInvalidation::Delivery old;
    q.Begin(old,400,400); q.Packet(350,0,1,1);
    const auto oldEpoch=q.Epoch(); q.Reset(); q.Finish(old,true);
    q.Schedule();
    Require(q.Dispatch(c,oldEpoch)==S_FALSE,"Old consumer consumed replacement notification");
    Require(dispatch()==S_OK && c->boundary==0,"Replacement consumer lost its clear");
    q.Packet(10,0,1,1);
    Require(dispatch()==S_FALSE,"Reset kept the old requested horizon");
    Require(ConsumerInvalidation::VideoBoundary(30000000,500,3,2)==24999999,"Delay/speed inverse is incorrect");
    Require(ConsumerInvalidation::VideoBoundary(30000000,-500,3,2)==14999999,"Negative delay inverse is incorrect");
    Require(ConsumerInvalidation::VideoBoundary(1,0,3,2)==0,"Fractional boundary was rounded forward");
    Require(ConsumerInvalidation::VideoBoundary(123,0,0,1)==0,"Non-forward timing did not fall back safely");
    Require(ConsumerInvalidation::VideoBoundary((std::numeric_limits<REFERENCE_TIME>::max)(),1,1,2)==0,"Overflow did not fall back safely");
    printf("queue_state_contract=passed (boundaries, races, failed delivery, epochs, timing overflow)\n");
    return 0;
}

class QueueContractConsumer : public CBaseFilter, public ISubRenderConsumer2
{
    CCritSec m_lock;
    struct Slot {
        REFERENCE_TIME start, stop;
        bool filled;
        CComPtr<ISubRenderFrame> frame;
        Slot() : start(0), stop(0), filled(false) {}
    } slots[128];
public:
    DECLARE_IUNKNOWN;
    CComPtr<ISubRenderProvider> provider;
    LONG clears, deliveries, mode, queries, visible;
    REFERENCE_TIME boundary;
    bool extension, refill, bad, failDelivery;
    QueueContractConsumer() : CBaseFilter(NAME("Queue contract"),NULL,&m_lock,CLSID_NULL),
        clears(0),deliveries(0),mode(0),queries(0),visible(0),boundary(-1),extension(true),refill(false),bad(false),failDelivery(false) {}
    int GetPinCount() override { return 0; }
    CBasePin* GetPin(int) override { return NULL; }
    STDMETHODIMP NonDelegatingQueryInterface(REFIID iid,void** value) override {
        if (iid==__uuidof(ISubRenderConsumer2) && extension) return GetInterface((ISubRenderConsumer2*)this,value);
        if (iid==__uuidof(ISubRenderConsumer)) return GetInterface((ISubRenderConsumer*)this,value);
        return CBaseFilter::NonDelegatingQueryInterface(iid,value);
    }
    STDMETHODIMP GetBool(LPCSTR,bool* v) { *v=false;return S_OK; }
    STDMETHODIMP GetInt(LPCSTR,int* v) { *v=0;return S_OK; }
    STDMETHODIMP GetSize(LPCSTR,SIZE* v) { *v=SIZE{1920,1080};return S_OK; }
    STDMETHODIMP GetRect(LPCSTR,RECT* v) { *v=RECT{0,0,mode==5 ? 0 : 1920-(mode==3 && (queries&1))*2,1080};return S_OK; }
    STDMETHODIMP GetUlonglong(LPCSTR,ULONGLONG* v) {
        ++queries; *v=mode==4 ? 0 : mode==2 && (queries&1) ? 333667 : 417083+(mode==1 && (queries&1));return S_OK;
    }
    STDMETHODIMP GetDouble(LPCSTR,double* v) { *v=24;return S_OK; }
    STDMETHODIMP GetString(LPCSTR key,LPWSTR* value,int* count) {
        const wchar_t* s=!strcmp(key,"yuvMatrix") ? L"None" : L"queue-contract";
        *count=(int)wcslen(s);*value=(LPWSTR)LocalAlloc(LMEM_FIXED,(*count+1)*sizeof(wchar_t));
        if (!*value) return E_OUTOFMEMORY;
        wcscpy_s(*value,*count+1,s);return S_OK;
    }
    STDMETHODIMP GetBin(LPCSTR,LPVOID*,int*) { return E_NOTIMPL; }
    STDMETHODIMP SetBool(LPCSTR,bool) { return S_OK; }
    STDMETHODIMP SetInt(LPCSTR,int) { return S_OK; }
    STDMETHODIMP SetSize(LPCSTR,SIZE) { return S_OK; }
    STDMETHODIMP SetRect(LPCSTR,RECT) { return S_OK; }
    STDMETHODIMP SetUlonglong(LPCSTR,ULONGLONG) { return S_OK; }
    STDMETHODIMP SetDouble(LPCSTR,double) { return S_OK; }
    STDMETHODIMP SetString(LPCSTR,LPWSTR,int) { return S_OK; }
    STDMETHODIMP SetBin(LPCSTR,LPVOID,int) { return S_OK; }
    STDMETHODIMP GetMerit(ULONG* v) { *v=1;return S_OK; }
    STDMETHODIMP Connect(ISubRenderProvider* p) { provider=p;return S_OK; }
    STDMETHODIMP Disconnect() { provider.Release();return S_OK; }
    STDMETHODIMP DeliverFrame(REFERENCE_TIME start,REFERENCE_TIME stop,LPVOID context,ISubRenderFrame* frame) {
        CAutoLock lock(&m_lock);
        const INT_PTR index=(INT_PTR)context;
        if (index<0 || index>=128) { bad=true; return E_INVALIDARG; }
        Slot& slot=slots[index];
        if (start!=slot.start || stop!=slot.stop) bad=true;
        ++deliveries;
        if (failDelivery) return E_FAIL;
        int count=0;if (frame && SUCCEEDED(frame->GetBitmapCount(&count)) && count) ++visible;
        slot.filled=true; slot.frame=frame; return S_OK;
    }
    STDMETHODIMP Clear(REFERENCE_TIME time) {
        std::vector<int> missing;
        {
            CAutoLock lock(&m_lock);
            boundary=time; ++clears;
            if (clears>8) { bad=true; return E_FAIL; } // bounded on the broken DLL
            for (int i=0;i<128;++i) if (time==0 || slots[i].stop>time) {
                if (slots[i].filled) missing.push_back(i);
                slots[i].filled=false; slots[i].frame.Release();
            }
        }
        if (refill) for (int i:missing) {
            if (FAILED(provider->RequestFrame(slots[i].start,slots[i].stop,(LPVOID)(INT_PTR)i))) bad=true;
        }
        return S_OK;
    }
    void Empty() {
        CAutoLock lock(&m_lock);
        clears=deliveries=queries=visible=0;boundary=-1;bad=false;
        for (auto& slot:slots) { slot.filled=false;slot.frame.Release(); }
    }
    void Fill(REFERENCE_TIME base=0) {
        for (int i=0;i<128;++i) {
            slots[i].start=base+REFERENCE_TIME(i)*417083; slots[i].stop=slots[i].start+417083;
            Require(SUCCEEDED(provider->RequestFrame(slots[i].start,slots[i].stop,(LPVOID)(INT_PTR)i)),"Queue request failed");
            Sleep(1); // let the DLL worker run, even during paused prefill
        }
    }
    void Check(int expectedClears, int expectedCount=128) {
        Sleep(100);
        CAutoLock lock(&m_lock);
        int count=0;for (const auto& slot:slots) count+=slot.filled;
        printf("queue clears=%ld filled=%d deliveries=%ld boundary=%lld\n",clears,count,deliveries,boundary);fflush(stdout);
        Require(!bad && clears==expectedClears && count==expectedCount,"Queue did not stabilize at the expected depth");
    }
};

class QueuePacketPin : public CBaseOutputPin
{
public:
    QueuePacketPin(CBaseFilter* filter,CCritSec* lock,HRESULT* hr) : CBaseOutputPin(NAME("Packets"),filter,lock,hr,L"Packets") {}
    HRESULT CheckMediaType(const CMediaType*) override { return S_OK; }
    HRESULT DecideBufferSize(IMemAllocator* allocator,ALLOCATOR_PROPERTIES* p) override {
        p->cBuffers=1;p->cbBuffer=4096;ALLOCATOR_PROPERTIES actual={};return allocator->SetProperties(p,&actual);
    }
    void Packet(REFERENCE_TIME start,REFERENCE_TIME stop,int order) {
        CStringA text; text.Format("%d,0,Default,,0,0,0,,{\\pos(960,540)\\t(0,500,\\alpha&HF00&)}Queue",order);
        CComPtr<IMediaSample> sample;
        Require(SUCCEEDED(GetDeliveryBuffer(&sample,&start,&stop,0)),"Cannot allocate packet");
        BYTE* bytes;sample->GetPointer(&bytes);memcpy(bytes,text.GetString(),text.GetLength());
        sample->SetActualDataLength(text.GetLength());sample->SetTime(&start,&stop);
        Require(SUCCEEDED(Deliver(sample)),"Embedded packet failed");
    }
};

class QueuePacketSource : public CBaseFilter
{
    CCritSec m_lock;
public:
    QueuePacketPin pin;
    QueuePacketSource(HRESULT* hr) : CBaseFilter(NAME("Packet source"),NULL,&m_lock,CLSID_NULL),pin(this,&m_lock,hr) {}
    int GetPinCount() override { return 1; }
    CBasePin* GetPin(int i) override { return i==0 ? &pin : NULL; }
};

static int QueueFilterContract(const wchar_t* dll, bool libass)
{
    using namespace DirectVobSubXyOptions;
    IsolatedLayoutRegistry settings;
    Require(SUCCEEDED(CoInitializeEx(NULL,COINIT_MULTITHREADED)),"COM initialization failed");
    HMODULE module=LoadLibraryW(dll);Require(module!=NULL,"DLL load failed");
    typedef HRESULT(STDAPICALLTYPE* GetClass)(REFCLSID,REFIID,void**);
    auto getClass=(GetClass)GetProcAddress(module,"DllGetClassObject");
    Require(getClass!=NULL,"Missing class factory");
    {
        CLSID clsid;CLSIDFromString(L"{2dfcb782-ec20-4a7c-b530-4577adb33f21}",&clsid);
        CComPtr<IClassFactory> factory;
        Require(SUCCEEDED(getClass(clsid,IID_IClassFactory,(void**)&factory)),"Class factory failed");
        CComPtr<IBaseFilter> filter;
        Require(SUCCEEDED(factory->CreateInstance(NULL,IID_IBaseFilter,(void**)&filter)),"Filter creation failed");
        CComQIPtr<IXyOptions> options(filter);
        options->XySetInt(INT_LOAD_SETTINGS_LEVEL,1);
        options->XySetBool(BOOL_PREFER_LIBASS,libass);
        CComPtr<IFilterGraph2> graph;
        Require(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"Graph creation failed");
        auto consumer=new QueueContractConsumer();CComPtr<IBaseFilter> owner=consumer;
        Require(SUCCEEDED(graph->AddFilter(owner,L"Queue consumer")),"Add consumer failed");
        Require(SUCCEEDED(graph->AddFilter(filter,L"XySubFilter")),"Add provider failed");
        HRESULT hr=S_OK;auto source=new QueuePacketSource(&hr);CComPtr<IBaseFilter> sourceOwner=source;
        Require(SUCCEEDED(hr) && SUCCEEDED(graph->AddFilter(sourceOwner,L"Packets")),"Packet source failed");
        CComPtr<IEnumPins> pins;filter->EnumPins(&pins);CComPtr<IPin> input;
        Require(pins && pins->Next(1,&input,NULL)==S_OK,"Missing subtitle input");
        const char header[]="[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 1920\r\nPlayResY: 1080\r\n[V4+ Styles]\r\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\nStyle: Default,Arial,40,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
        CMediaType mt;mt.SetType(&MEDIATYPE_Subtitle);mt.SetSubtype(&MEDIASUBTYPE_ASS);mt.SetFormatType(&FORMAT_SubtitleInfo);
        auto info=(SUBTITLEINFO*)mt.AllocFormatBuffer(sizeof(SUBTITLEINFO)+sizeof(header));
        memset(info,0,mt.cbFormat);info->dwOffset=sizeof(SUBTITLEINFO);strcpy_s(info->IsoLang,"eng");
        memcpy(mt.pbFormat+info->dwOffset,header,sizeof(header));
        Require(SUCCEEDED(graph->ConnectDirect(&source->pin,input,&mt)),"Subtitle input connection failed");
        // Also tear down on an assertion failure; otherwise a live graph can
        // wait for its running allocator while C++ unwinds the test objects.
        struct GraphCleanup {
            IFilterGraph2* graph; IBaseFilter* filter; QueuePacketSource* source;
            QueueContractConsumer* consumer; IPin* input;
            ~GraphCleanup() {
                if (consumer->provider) consumer->provider->Disconnect();
                filter->Stop();source->Stop();graph->Disconnect(input);graph->Disconnect(&source->pin);
                graph->RemoveFilter(filter);graph->RemoveFilter(source);graph->RemoveFilter(consumer);
            }
        } cleanup={graph,filter,source,consumer,input};
        Require(SUCCEEDED(filter->Pause()) && consumer->provider,"Provider did not connect");
        Require(SUCCEEDED(source->Pause()),"Packet allocator failed");
        input->NewSegment(0,1000000000,1);Sleep(100);
        source->pin.Packet(0,200000000,0);Sleep(100);
        for (int mode=0;mode<5;++mode) {
            consumer->Empty();consumer->mode=mode;consumer->Fill();consumer->Check(0);
            Require(consumer->visible>0,"Embedded ASS never produced visible output");
        }
        const LONG delivered=consumer->deliveries;
        consumer->mode=5;
        Require(FAILED(consumer->provider->RequestFrame(0,417083,NULL)),"Invalid render geometry did not fail");
        consumer->Check(0);Require(consumer->deliveries==delivered,"Failed request delivered a frame");
        consumer->mode=0;consumer->failDelivery=true;
        Require(consumer->provider->RequestFrame(0,417083,NULL)==E_FAIL,"Delivery failure was not propagated");
        consumer->Check(0);consumer->failDelivery=false;
        consumer->mode=0;consumer->Empty();consumer->Fill();consumer->refill=true;
        source->pin.Packet(300000000,310000000,1);consumer->Check(0);
        source->pin.Packet(REFERENCE_TIME(128)*417083,60000000,2);consumer->Check(0);
        source->pin.Packet(20000000,30000000,3);consumer->Check(1);
        Require(consumer->boundary==19999999,"Late packet lost video boundary");
        CComQIPtr<IDirectVobSub> settingsApi(filter);
        Require(settingsApi && SUCCEEDED(settingsApi->put_SubtitleTiming(500,3,2)),"Timing change failed");
        consumer->Check(2);
        source->pin.Packet(30000000,40000000,4);consumer->Check(3);
        Require(consumer->boundary==24999999,"Delayed/speed-adjusted packet used subtitle time");
        Require(SUCCEEDED(options->XySetBool(BOOL_RENDER_TO_ORIGINAL_VIDEO_SIZE,true)),"Layout change failed");
        consumer->Check(4);Require(consumer->boundary==0,"Global setting did not clear all frames");
        // Seeking must forget a far-ahead requested horizon before new packets.
        consumer->refill=false;input->NewSegment(0,1000000000,1);consumer->Check(5,0);
        source->pin.Packet(300000000,310000000,5);consumer->Check(5,0);
        consumer->Empty();consumer->extension=false;consumer->Fill();
        options->XySetBool(BOOL_RENDER_TO_ORIGINAL_VIDEO_SIZE,false);consumer->Check(0);
        consumer->extension=true;
    }
    FreeLibrary(module);CoUninitialize();
    printf("queue_filter_contract=passed backend=%s\n",libass ? "libass" : "native");
    return 0;
}
