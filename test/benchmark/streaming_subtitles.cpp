// Reuse the native harness's font loading, exact frame hashes and scheduling.
// This entry point feeds only packets that have arrived at the simulated lead.
#define wmain OriginalNativeBenchmarkMain
#include "render_subtitles.cpp"
#undef wmain
#include <algorithm>

class StreamingRenderer : public CRenderedTextSubtitle
{
    CAtlArray<STSEntry> m_packets;
    std::vector<size_t> m_deliveryOrder;
    size_t m_nextPacket;
    REFERENCE_TIME m_windowStart;
public:
    StreamingRenderer(CCritSec* lock) : CRenderedTextSubtitle(lock), m_nextPacket(0), m_windowStart(0) {}
    void BeginPackets(REFERENCE_TIME windowStart)
    {
        Require(m_mode==TIME,"Packet probe requires a time-based subtitle source");
        m_packets.Copy(m_entries);
        m_deliveryOrder.resize(m_packets.GetCount());
        for(size_t i=0;i<m_deliveryOrder.size();++i) m_deliveryOrder[i]=i;
        std::stable_sort(m_deliveryOrder.begin(),m_deliveryOrder.end(),[&](size_t a,size_t b) {
            return m_packets[a].start<m_packets[b].start;
        });
        m_entries.RemoveAll(); m_segments.RemoveAll();
        OnChanged();
        m_nextPacket=0; m_windowStart=windowStart;
    }
    size_t DeliverUntil(REFERENCE_TIME arrivalCutoff)
    {
        size_t delivered=0;
        while(m_nextPacket<m_deliveryOrder.size())
        {
            const STSEntry& packet=m_packets[m_deliveryOrder[m_nextPacket]];
            if(packet.start>arrivalCutoff) break;
            ++m_nextPacket;
            // Earlier, ended packets cannot affect this independently opened
            // region. All relevant Add/segment work is inside the frame timer.
            if(packet.end<=m_windowStart) continue;
            Add(packet.str,packet.fUnicode,packet.start,packet.end,packet.style,
                packet.actor,packet.effect,packet.marginRect,packet.layer,packet.readorder);
            ++delivered;
        }
        return delivered;
    }
};

int wmain(int argc,wchar_t** argv)
{
    g_hInst=GetModuleHandleW(NULL);
    if(!AfxWinInit(g_hInst,NULL,GetCommandLineW(),0)) return 2;
    try
    {
        Require(argc>=8,"Usage: streaming_subtitles script.ass fonts output.csv lead_ms lookahead_us first_frame last_frame");
        const int lead=_wtoi(argv[4]), allowance=_wtoi(argv[5]), first=_wtoi(argv[6]), last=_wtoi(argv[7]);
        Require(lead>=0 && lead<=10000 && allowance>=0 && allowance<=4000 && first>=0 && last>=first,"Invalid streaming probe arguments");
        bool diagnostics=false,noLayout=false,noDraw=false,noScalar=false;
        for(int i=8;i<argc;++i)
        {
            if(!wcscmp(argv[i],L"--diagnostics")) diagnostics=true;
            else if(!wcscmp(argv[i],L"--no-layout")) noLayout=true;
            else if(!wcscmp(argv[i],L"--no-draw-reuse")) noDraw=true;
            else if(!wcscmp(argv[i],L"--no-scalar-cache")) noScalar=true;
            else Require(false,"Unknown packet probe option");
        }
        ConfigureBenchmark(-1); CacheManager::SetBitmapCacheSize(256); LoadFonts(argv[2]);
        printf("bitmap_cache_items=256\n");
        CCritSec lock; StreamingRenderer renderer(&lock);
        Require(renderer.Open(argv[1],DEFAULT_CHARSET),"Cannot open packet source");
        renderer.SetLookaheadBudget(unsigned(allowance));
        renderer.EnableDiagnostics(diagnostics);
        renderer.SetScalarParameterCacheEnabled(!noScalar);
        if(noLayout) renderer.SetPreparedLayoutCacheLimit(0);
        if(noDraw) renderer.SetDrawReuseCacheLimit(0);
        const auto frameTime=[](int frame) {return (REFERENCE_TIME(frame)*2+1)*1001*10000000/48000;};
        renderer.BeginPackets(frameTime(first));
        BCRYPT_ALG_HANDLE algorithm=NULL;
        Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0,"Cannot open SHA256");
        FILE* csv=NULL;Require(_wfopen_s(&csv,argv[3],L"wb")==0 && csv,"Cannot create packet CSV");
        fprintf(csv,"frame,time_100ns,render_ms,bitmaps,sha256,ingest_ms,frame_total_ms,thread_cpu_ms,packets");
        if(diagnostics) fprintf(csv,",parse_ms,build_draw_ms,composite_ms,tree_ms,merge_ms,group_ms,bitmap_ms,layout_builds,layout_updates,layout_fallbacks,draw_hits,draw_misses,machine_allocations,draw_allocations,overlay_keys,tag_misses,prepared_events,scalar_hits,scalar_misses");
        fprintf(csv,"\n");
        LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
        const RECT rect={0,0,1920,1080};const SIZE size={1920,1080};
        for(int frame=first;frame<=last;++frame)
        {
            const REFERENCE_TIME time=frameTime(frame);
            CRenderedTextSubtitle::PreparedLayoutStats layoutBefore={}; DrawReuseBudget drawBefore;
            CRenderedTextSubtitle::ParsedTagCacheStats tagsBefore={};
            CRenderedTextSubtitle::LookaheadStats aheadBefore={};
            CRenderedTextSubtitle::ScalarParameterStats scalarBefore={};
            if(diagnostics) {layoutBefore=renderer.GetPreparedLayoutStats();drawBefore=renderer.GetDrawReuseStats();tagsBefore=renderer.GetParsedTagCacheStats();aheadBefore=renderer.GetLookaheadStats();scalarBefore=renderer.GetScalarParameterCacheStats();}
            if(diagnostics) renderer.EnableDiagnostics(true);
            const ULONGLONG cpuBefore=ThreadTicks();
            LARGE_INTEGER before,afterIngest,afterRender; QueryPerformanceCounter(&before);
            const size_t delivered=renderer.DeliverUntil(time+REFERENCE_TIME(lead)*10000);
            QueryPerformanceCounter(&afterIngest);
            CComPtr<IXySubRenderFrame> output;
            Require(SUCCEEDED(renderer.RenderEx(&output,MSP_RGBA,rect,rect,size,time,24000.0/1001)),"Packet render failed");
            QueryPerformanceCounter(&afterRender);
            const ULONGLONG cpu=ThreadTicks()-cpuBefore;
            int count;CStringA hash=FrameSha256(output,algorithm,&count);
            fprintf(csv,"%d,%lld,%.6f,%d,%s,%.6f,%.6f,%.6f,%llu",frame,time,
                (afterRender.QuadPart-afterIngest.QuadPart)*1000.0/frequency.QuadPart,count,hash.GetString(),
                (afterIngest.QuadPart-before.QuadPart)*1000.0/frequency.QuadPart,
                (afterRender.QuadPart-before.QuadPart)*1000.0/frequency.QuadPart,cpu/10000.0,
                static_cast<unsigned long long>(delivered));
            if(diagnostics)
            {
                const auto layout=renderer.GetPreparedLayoutStats();const auto draw=renderer.GetDrawReuseStats();
                const auto tags=renderer.GetParsedTagCacheStats();const auto ahead=renderer.GetLookaheadStats();
                const auto scalar=renderer.GetScalarParameterCacheStats();
                const auto& d=renderer.GetDiagnostics();
                for(int phase=RenderDiagnostics::PARSE;phase<=RenderDiagnostics::BITMAP;++phase)
                    fprintf(csv,",%.6f",d.ticks[phase]*1000.0/frequency.QuadPart);
                fprintf(csv,",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
                    layout.builds-layoutBefore.builds,layout.updates-layoutBefore.updates,layout.fallbacks-layoutBefore.fallbacks,
                    draw.hits-drawBefore.hits,draw.misses-drawBefore.misses,draw.machineAllocations-drawBefore.machineAllocations,
                    draw.drawAllocations-drawBefore.drawAllocations,draw.keyAllocations-drawBefore.keyAllocations,
                    tags.misses-tagsBefore.misses,ahead.attempted-aheadBefore.attempted,
                    scalar.hits-scalarBefore.hits,scalar.misses-scalarBefore.misses);
            }
            fprintf(csv,"\n");
        }
        fclose(csv); BCryptCloseAlgorithmProvider(algorithm,0);
        const auto ahead=renderer.GetLookaheadStats();
        printf("packet_lead_ms=%d lookahead_allowance_us=%d attempted=%llu retained=%llu prepare_ms=%.6f largest_unit_ms=%.6f\n",
            lead,allowance,ahead.attempted,ahead.retained,ahead.ticks*1000.0/frequency.QuadPart,ahead.largestUnit*1000.0/frequency.QuadPart);
        printf("scope=packet Add/segmentation plus RenderEx; source read/style setup excluded; full-script source is not visible to native lookahead\n");
        return 0;
    }
    catch(const std::exception& error) {fprintf(stderr,"%s\n",error.what());return 1;}
}
