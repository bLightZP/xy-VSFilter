// Models the MPC Subtitle Helper presenter on the consumer side of the actual DLL.
#include "../../src/filters/transform/vsfilter/MpcSubtitleHelper.h"

class MpcTimingConsumer : public QueueContractConsumer, public IMpcSubtitlePresenter
{
    int delayMs = 0;
    ATL::CEvent fullClear, partialClear;
    REFERENCE_TIME clearBoundary = 0;
    REFERENCE_TIME start = 0, stop = 0;
    LPVOID context = nullptr;
    bool visibleFrame = false;
public:
    MpcTimingConsumer() : QueueContractConsumer(__uuidof(MpcSubtitleHelperFilter)) {
        fullClear.Create(nullptr, FALSE, FALSE, nullptr);
        partialClear.Create(nullptr, FALSE, FALSE, nullptr);
        Require(fullClear.m_h && partialClear.m_h, "Cannot create timing test event");
    }
    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID iid, void** value) override {
        if (iid == __uuidof(IMpcSubtitlePresenter)) return GetInterface(static_cast<IMpcSubtitlePresenter*>(this), value);
        return QueueContractConsumer::NonDelegatingQueryInterface(iid, value);
    }
    STDMETHODIMP DisableSubPicInitialization() override { return E_NOTIMPL; }
    STDMETHODIMP EnablePreviewModeInitialization() override { return E_NOTIMPL; }
    STDMETHODIMP CreateRenderer(IUnknown**) override { return E_NOTIMPL; }
    STDMETHODIMP_(CLSID) GetAPCLSID() override { return CLSID_NULL; }
    STDMETHODIMP_(SIZE) GetVideoSize() override { return SIZE{}; }
    STDMETHODIMP_(SIZE) GetVideoSizeAR() override { return SIZE{}; }
    STDMETHODIMP_(void) SetPosition(RECT, RECT) override {}
    STDMETHODIMP SetRotation(int) override { return E_NOTIMPL; }
    STDMETHODIMP_(int) GetRotation() override { return 0; }
    STDMETHODIMP SetFlip(bool) override { return E_NOTIMPL; }
    STDMETHODIMP_(bool) GetFlip() override { return false; }
    STDMETHODIMP_(bool) Paint(bool) override { return false; }
    STDMETHODIMP_(void) SetTime(REFERENCE_TIME) override {}
    STDMETHODIMP_(void) SetSubtitleDelay(int ms) override { delayMs = ms; }
    STDMETHODIMP_(int) GetSubtitleDelay() override { return delayMs; }

    STDMETHODIMP DeliverFrame(REFERENCE_TIME from, REFERENCE_TIME to, LPVOID token, ISubRenderFrame* frame) override {
        const REFERENCE_TIME offset = REFERENCE_TIME(delayMs) * 10000;
        start = from + offset;
        stop = to + offset;
        context = token;
        int count = 0;
        if (frame && FAILED(frame->GetBitmapCount(&count))) return E_FAIL;
        visibleFrame = count > 0;
        return S_OK;
    }
    STDMETHODIMP Clear(REFERENCE_TIME boundary) override {
        if (boundary > 0) {
            clearBoundary = boundary;
            partialClear.Set();
        } else {
            fullClear.Set();
        }
        return S_OK;
    }
    void SetFilterTiming(IDirectVobSub* timing, int ms, int mul, int div) {
        fullClear.Reset();
        const HRESULT hr = timing->put_SubtitleTiming(ms, mul, div);
        Require(SUCCEEDED(hr), "Cannot set filter timing");
        // A pending full clear subsumes later partial clears. Wait for its dispatch
        // before issuing requests against the new mapping.
        if (hr == S_OK)
            Require(WaitForSingleObject(fullClear, 2000) == WAIT_OBJECT_0, "Timing change did not clear the queue");
    }
    void ExpectBoundary(REFERENCE_TIME expected) {
        Require(WaitForSingleObject(partialClear, 2000) == WAIT_OBJECT_0, "Late packet did not trigger a partial clear");
        Require(clearBoundary == expected,
            "Partial clear used a different timeline from subtitle lookup");
    }
    bool PictureAt(int videoMs) {
        const REFERENCE_TIME from = REFERENCE_TIME(videoMs - delayMs) * 10000;
        const REFERENCE_TIME to = from + 400000; // 25 fps
        context = nullptr;
        Require(SUCCEEDED(provider->RequestFrame(from, to, this)), "MPC timing request failed");
        Require(start == from && stop == to && context == this, "Presenter rejected returned frame timestamps or context");
        return visibleFrame;
    }
};

static void CheckMpcSubtitleTiming(IBaseFilter* filter, QueuePacketSource* source, MpcTimingConsumer* helper)
{
    CComQIPtr<IDirectVobSub> timing(filter);
    Require(timing != nullptr, "Missing filter timing API");
    helper->SetFilterTiming(timing, 0, 1, 1);
    source->pin.Packet(60000000,66000000,0,"Timing");
    Require(helper->PictureAt(6200), "Embedded ASS cue never appeared");

    struct Case { int filterMs, helperMs, mul, div, onsetMs, endMs; };
    const Case cases[] = {
        {0,0,1,1,6000,6600}, {100,100,1,1,6100,6700}, {-100,-100,1,1,5900,6500},
        {0,-100,1,1,5900,6500}, {0,100,1,1,6100,6700}, {100,0,1,1,6100,6700},
        {-100,0,1,1,5900,6500}, {100,-40,1,1,6060,6660}, {0,0,1,1,6000,6600},
        {100,100,3,2,4100,4500}
    };
    for (const Case& test : cases) {
        helper->SetSubtitleDelay(test.helperMs);
        helper->SetFilterTiming(timing, test.filterMs, test.mul, test.div);
        Require(!helper->PictureAt(test.onsetMs-40) && helper->PictureAt(test.onsetMs+40) &&
            helper->PictureAt(test.endMs-40) && !helper->PictureAt(test.endMs+40),
            "Visible cue did not cross both expected timing boundaries");
    }
    source->pin.Packet(63000000,64000000,1,"Late packet");
    helper->ExpectBoundary(41999999); // request clock at 4.2 seconds for a 6.3 second cue at 3/2 speed
    const REFERENCE_TIME earliest = (std::numeric_limits<REFERENCE_TIME>::min)();
    Require(helper->provider->RequestFrame(earliest, earliest+400000, nullptr) == E_INVALIDARG,
        "Positive presenter delay wrapped an unrepresentable delivery timestamp");
    helper->SetSubtitleDelay(-100);
    const REFERENCE_TIME latest = (std::numeric_limits<REFERENCE_TIME>::max)();
    Require(helper->provider->RequestFrame(latest-400000, latest, nullptr) == E_INVALIDARG,
        "Negative presenter delay wrapped an unrepresentable delivery timestamp");
}
