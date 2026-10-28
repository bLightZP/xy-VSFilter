// Offline validation for the shared RTS renderer used by both filter paths.
#include "stdafx.h"
#include "xy_bitmap.h"
#include "xy_overlay_paint_machine.h"
#include "subpixel_position_controler.h"
#include "draw_item.h"
#include "xy_clipper_paint_machine.h"
#include "xy_malloc.h"
#include "../../src/filters/transform/vsfilter/IDirectVobSubXy.h"
#include "../../src/filters/transform/vsfilter/resource.h"
#include <bcrypt.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <set>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <locale.h>
#include <float.h>

HINSTANCE g_hInst = NULL;
CWinApp benchmarkApp;

static void Require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

static int TagStorageContract()
{
    typedef CRenderedTextSubtitle RTS;
    struct LegacyTag
    {
        RTS::AssCmdType type;
        CInlineList<CStringW> params;
        CAtlList<LegacyTag> nested;
        RTS::AssTag::Number number;
        boost::scoped_array<RTS::AssTag::Number> extra;
    };
    struct LegacyNode { LegacyNode* next; LegacyNode* previous; LegacyTag tag; };
    Require(sizeof(RTS::AssTag)<sizeof(LegacyTag),"Compact tags did not reduce their header");
    Require(sizeof(RTS::AssTagParams)<sizeof(CInlineList<CStringW>),"Compact parameters did not reduce their header");
    Require(sizeof(RTS::AssTagList)==sizeof(void*),"Empty nested tags are not lazy");
    RTS::AssTagParams params;
    std::vector<CStringW> expected;
    unsigned random=0x28106553;
    for(unsigned i=0;i<6000;++i)
    {
        random=random*1664525+1013904223;
        switch((random>>16)%5)
        {
        case 0:
            if(!expected.empty())
            {
                const size_t count=random%(expected.size()+1);
                params.Truncate(count);expected.resize(count);
                break;
            }
        case 1:
        case 2:
            {
                CStringW value;value.Format(L"owned %u \x03a9",i);
                params.Add(value);expected.push_back(value);
                value.SetAt(0,L'X');
            }
            break;
        case 3:
            if(!expected.empty())
            {
                const size_t index=random%expected.size();
                CStringW copy(expected[index]);
                params.Add(params[index]);expected.push_back(copy);
            }
            break;
        default:params.Reserve(params.GetCount()+random%23);break;
        }
        Require(params.GetCount()==expected.size(),"Parameter growth/truncation changed count");
        Require(params.GetCapacity()>=max(size_t(1),params.GetCount()),"Parameter capacity is too small");
        for(size_t n=0;n<expected.size();++n) Require(params[n]==expected[n],"Parameter storage changed ownership/order/aliasing");
    }
    params.Truncate(0);
    params.Add(L"one");
    params.Add(params[0]);
    Require(params.GetCount()==2 && params[1]==L"one","Refilled parameters lost aliased first value");
    unsigned compactGrowth=0,legacyGrowth=0;
    for(size_t count=2;count<=7;++count)
    {
        RTS::AssTagParams compact;
        CInlineList<CStringW> legacy;
        for(size_t i=0;i<count;++i)
        {
            const size_t before=compact.GetCapacity(),old=legacy.GetCapacity();
            compact.Add(L"argument");legacy.AddTail(L"argument");
            compactGrowth+=before!=compact.GetCapacity();legacyGrowth+=old!=legacy.GetCapacity();
        }
    }
    Require(compactGrowth<legacyGrowth,"Common parameter arities did not reduce spill growth");
    boost::shared_ptr<RTS::AssTagList> tags=boost::make_shared<RTS::AssTagList>();
    Require(tags->IsEmpty() && !tags->GetStorageBytes() && !tags->GetBlockCount(),"Empty tag list allocated storage");
    std::vector<POSITION> positions;
    for(int i=0;i<5000;++i)
    {
        POSITION position=tags->AddTail();positions.push_back(position);
        RTS::AssTag& tag=tags->GetAt(position);tag.cmdType=RTS::CMD_b;
        tag.firstNumber.real=i;
        CStringW value;value.Format(L"value %d",i);tag.strParams.Add(value);
        Require(!tag.embeded.GetStorageBytes(),"Scalar tag allocated a nested list");
        if(i%23==0)
        {
            RTS::AssTagList* child=&tag.embeded;
            for(int depth=0;depth<5;++depth)
            {
                RTS::AssTag& nested=child->GetAt(child->AddTail());
                nested.cmdType=RTS::CMD_t;nested.firstNumber.real=depth;
                nested.strParams.Add(value);child=&nested.embeded;
            }
        }
        if(i%37==0) {tag.AllocateNumbers(7);for(size_t j=1;j<7;++j)tag.GetNumber(j).real=i+j;}
    }
    Require(tags->GetCapacity()>=5000 && tags->GetCapacity()<5016,"Packed tag capacity grew without a bound");
    Require(tags->GetBlockCount()==1+(tags->GetCapacity()-8)/16,"Tag block accounting changed");
    RTS::SharedPtrConstAssTagList held=tags;tags.reset();
    POSITION position=held->GetHeadPosition();int index=0;
    while(position)
    {
        const RTS::AssTag& tag=held->GetNext(position);
        Require(&tag==&held->GetAt(positions[index]),"Tag addresses moved during growth");
        CStringW value;value.Format(L"value %d",index);
        Require(tag.cmdType==RTS::CMD_b && tag.firstNumber.real==index && tag.strParams[0]==value,"Packed tags lost data or order");
        if(index%37==0) for(size_t j=1;j<7;++j)Require(tag.GetNumber(j).real==index+j,"Packed tag lost numeric spill");
        ++index;
    }
    Require(index==5000 && held->GetStorageBytes()>held->GetCapacity()*sizeof(RTS::AssTag),"Packed tag memory accounting is incomplete");
    printf("tag_storage_contract=passed tag_bytes=%zu legacy_tag_bytes=%zu node_bytes=%zu legacy_node_bytes=%zu params_bytes=%zu legacy_params_bytes=%zu empty_list_bytes=%zu common_spill_growths=%u legacy_spill_growths=%u (6000 parameter operations, alias/grow/truncate/reserve/ownership, 5000 stable tags, nested programs, numeric spills, retained ownership)\n",
        sizeof(RTS::AssTag),sizeof(LegacyTag),sizeof(RTS::AssTag),sizeof(LegacyNode),sizeof(RTS::AssTagParams),sizeof(CInlineList<CStringW>),sizeof(RTS::AssTagList),compactGrowth,legacyGrowth);
    return 0;
}

static int AssCommandContract()
{
    typedef CRenderedTextSubtitle RTS;
    CCritSec lock;
    RTS context(&lock);
    unsigned checks = 0;
    auto check = [&](const CStringW& command) {
        RTS::AssCmdType legacy = RTS::CMD_COUNT;
        int legacyLength = min(RTS::MAX_CMD_LENGTH, command.GetLength());
        for (; legacyLength >= RTS::MIN_CMD_LENGTH; --legacyLength)
            if (RTS::m_cmdMap.Lookup(command.Left(legacyLength), legacy)) break;
        if (legacyLength < RTS::MIN_CMD_LENGTH) { legacy = RTS::CMD_COUNT; legacyLength = 0; }
        int length = -1;
        const RTS::AssCmdType actual = RTS::LookupAssCommand(command, command.GetLength(), length);
        Require(actual == legacy && length == legacyLength, "ASS command lookup changed its longest case-sensitive prefix");
        ++checks;
    };
    check(L"");
    const WCHAR* suffixes[] = {L"",L"0",L"1.25",L"-4",L"+3",L"unknown",L" ",L"\t",L"&H80&",L"(1,2)",L"\\frz20",L"\x3042",L"Arial"};
    POSITION pos = RTS::m_cmdMap.GetStartPosition();
    while (pos) {
        const auto* command = RTS::m_cmdMap.GetNext(pos);
        for (int n=0; n<=command->m_key.GetLength(); ++n) {
            for (size_t j=0; j<_countof(suffixes); ++j) {
                CStringW text = command->m_key.Left(n)+suffixes[j];
                check(text);
                text.MakeUpper(); check(text);
            }
        }
    }
    for (int a=1; a<128; ++a) for (int b=1; b<128; ++b) {
        const WCHAR chars[] = {static_cast<WCHAR>(a),static_cast<WCHAR>(b),0};
        check(chars);
    }
    unsigned random = 0x21900;
    for (int i=0; i<10000; ++i) {
        CStringW text;
        const int count = i%17;
        for (int j=0; j<count; ++j) {
            random = 1664525*random+1013904223;
            text += static_cast<WCHAR>((random>>16)%2 ? 1+(random%127) : 0x100+(random%0x7000));
        }
        check(text);
    }
    RTS::AssTagParams params;
    Require(params.IsEmpty() && params.GetCapacity()==1, "Empty tag parameters allocated spill storage");
    CStringW source(L"owned parameter");
    params.Add(source);
    source.SetAt(0,L'X');
    Require(params.GetCapacity()==1 && params[0]==L"owned parameter", "Inline tag parameter lost ownership or allocated");
    for (int i=0; i<32; ++i) {
        CStringW value; value.Format(L"argument %d",i);
        params.Add(value);
    }
    Require(params.GetCount()==33, "Tag parameter spill lost arguments");
    for (int i=0; i<32; ++i) {
        CStringW value; value.Format(L"argument %d",i);
        Require(params[i+1]==value, "Tag parameter spill changed argument order");
    }
    params.Truncate(0);
    params.Add(L"fresh");
    Require(params.GetCount()==1 && params[0]==L"fresh", "Tag parameter refill retained stale values");
    printf("ass_command_contract=passed (%u legacy-prefix comparisons; parameter inline/spill/order/ownership)\n",checks);
    return 0;
}

static int HashContract()
{
    STSStyle positive, negative;
    positive.outlineWidthX = positive.outlineWidthY = positive.shadowDepthX = positive.shadowDepthY =
        positive.fontSpacing = positive.fBlur = positive.fGaussianBlur = positive.fontSize =
        positive.fontScaleX = positive.fontScaleY = positive.fontAngleX = positive.fontAngleY =
        positive.fontAngleZ = positive.fontShiftX = positive.fontShiftY = 0.0;
    negative = positive;
    negative.outlineWidthX = negative.outlineWidthY = negative.shadowDepthX = negative.shadowDepthY =
        negative.fontSpacing = negative.fBlur = negative.fGaussianBlur = negative.fontSize =
        negative.fontScaleX = negative.fontScaleY = negative.fontAngleX = negative.fontAngleY =
        negative.fontAngleZ = negative.fontShiftX = negative.fontShiftY = -0.0;
    bool zeroEqual = positive == negative && hash_value(positive) == hash_value(negative) &&
        FwSTSStyle(positive) == FwSTSStyle(negative);
    double STSStyle::* numericFields[] = {&STSStyle::outlineWidthX, &STSStyle::outlineWidthY,
        &STSStyle::shadowDepthX, &STSStyle::shadowDepthY, &STSStyle::fontSpacing,
        &STSStyle::fBlur, &STSStyle::fGaussianBlur, &STSStyle::fontSize,
        &STSStyle::fontScaleX, &STSStyle::fontScaleY, &STSStyle::fontAngleX,
        &STSStyle::fontAngleY, &STSStyle::fontAngleZ, &STSStyle::fontShiftX, &STSStyle::fontShiftY};
    for (unsigned i = 0; i < _countof(numericFields); ++i) {
        negative = positive;
        negative.*numericFields[i] = -0.0;
        zeroEqual = zeroEqual && positive == negative && hash_value(positive) == hash_value(negative) &&
            FwSTSStyle(positive) == FwSTSStyle(negative);
    }
    std::set<size_t> shadows, borders, alphas, blurs;
    for (int i = 0; i < 1000; ++i) {
        STSStyle style;
        style.shadowDepthX = i * 0.125;
        shadows.insert(hash_value(style));
        style = STSStyle();
        style.outlineWidthY = i * 0.125;
        borders.insert(hash_value(style));
        style = STSStyle();
        style.alpha[0] = i & 255;
        style.alpha[3] = (i >> 8) & 255;
        alphas.insert(hash_value(style));
        style = STSStyle();
        style.fGaussianBlur = i * 0.125;
        blurs.insert(hash_value(style));
    }
    printf("signed_zero_equal=%d shadow_hashes=%u border_hashes=%u alpha_hashes=%u blur_hashes=%u\n",
        zeroEqual, unsigned(shadows.size()), unsigned(borders.size()), unsigned(alphas.size()), unsigned(blurs.size()));
    return zeroEqual && shadows.size() > 990 && borders.size() > 990 && alphas.size() > 990 && blurs.size() > 990 ? 0 : 1;
}

static int InternContract()
{
    CStringW input(L"intern lifetime test");
    XyFwStringW first(input);
    XyFwStringW hit(input);
    XyFwStringW owned(DEBUG_NEW CStringW(input));
    Require(first.GetId() == hit.GetId() && first.GetId() == owned.GetId(), "Intern IDs changed on a cache hit");
    Require(&first.Get() == &hit.Get() && &first.Get() == &owned.Get(), "Intern hit did not share owned storage");
    input = L"mutated";
    Require(first.Get() == L"intern lifetime test", "Intern retained a borrowed input");
    XyFwStringW::GetCacher()->RemoveAll();
    Require(first.Get() == L"intern lifetime test", "Eviction invalidated a live value");
    XyFwStringW afterEviction(CStringW(L"intern lifetime test"));
    Require(first.GetId() != afterEviction.GetId() && first.Get() == afterEviction.Get(), "Eviction reused a stale ID");
    printf("intern_contract=passed (identity, ownership, mutation, eviction)\n");
    return 0;
}

class GeometryPolygon : public CPolygon
{
public:
    GeometryPolygon(const FwSTSStyle& style, const CStringW& text, double x, double y, int baseline)
        : CPolygon(style, text, 0, 0, 0, x, y, baseline) {}
    bool ReadPath(PathData* path) { return CreatePath(path); }
};

static int GeometryContract()
{
    PathDataMruCache* cache = CacheManager::GetPathDataMruCache();
    cache->RemoveAll();
    const CStringW text(L"m -10.5 -2.25 l 20.75 -2 20 15 b 10 20 0 20 -10.5 15 c");
    STSStyle style;
    GeometryPolygon seed(FwSTSStyle(style), text, 1.25, 0.75, 0);
    SharedPtrPathData raw(new PathData());
    Require(seed.ReadPath(raw.get()), "Cannot read seed geometry");
    PathDataCacheKey key(seed);
    key.UpdateHashValue();
    Require(key.HasExactPolygonScale(1.25, 0.75) && !key.HasExactPolygonScale(1.2500001, 0.75), "Polygon scales were compared tolerantly");
    style.fontName = L"Tahoma";
    style.fontSize += 7;
    style.fontSpacing = 3;
    style.fontWeight = 700;
    style.fItalic = style.fUnderline = style.fStrikeOut = true;
    style.charSet = SYMBOL_CHARSET;
    style.fontScaleX = 137;
    style.fontScaleY = 83;
    GeometryPolygon cold(FwSTSStyle(style), text, 1.25, 0.75, -3);
    PathDataCacheKey fontChanged(cold);
    fontChanged.UpdateHashValue();
    Require(fontChanged == key && fontChanged.GetHashValue() == key.GetHashValue(), "Polygon key depends on font glyph fields");
    CText text1(FwSTSStyle(STSStyle()), L"test", 0, 0, 0);
    CText text2(FwSTSStyle(style), L"test", 0, 0, 0);
    Require(!(PathDataCacheKey(text1) == PathDataCacheKey(text2)), "Text key ignored font fields");
    PathData expected;
    Require(cold.ReadPath(&expected), "Cannot read cold geometry");
    cache->UpdateCache(key, raw);
    const unsigned __int64 hits = cache->GetCacheHitCount();
    GeometryPolygon warm(FwSTSStyle(style), text, 1.25, 0.75, -3);
    PathData actual;
    Require(warm.ReadPath(&actual) && expected == actual && cold.m_width == warm.m_width &&
        cold.m_ascent == warm.m_ascent && cold.m_descent == warm.m_descent, "Warm geometry changed vertices or metrics");
    Require(cache->GetCacheHitCount() > hits, "Geometry reuse was not exercised");
    cache->RemoveAll();
    PathData afterEviction;
    Require(warm.ReadPath(&afterEviction) && expected == afterEviction, "Geometry retained a borrowed cache buffer");
    GeometryPolygon zeroCold(FwSTSStyle(style), text, 0, 0, -3);
    PathData zeroExpected;
    Require(zeroCold.ReadPath(&zeroExpected), "Cannot read degenerate geometry");
    PathDataCacheKey zeroKey(zeroCold);
    zeroKey.UpdateHashValue();
    // Simulate a text entry with the same string ID and the text-key zero scales.
    cache->UpdateCache(zeroKey, raw);
    GeometryPolygon zeroWarm(FwSTSStyle(style), text, 0, 0, -3);
    PathData zeroActual;
    Require(zeroWarm.ReadPath(&zeroActual) && zeroExpected == zeroActual, "Degenerate polygon reused a text path");
    cache->RemoveAll();
    printf("geometry_contract=passed (reuse, exact scales, metrics, ownership, degenerate scales)\n");
    return 0;
}

static int InlineListContract()
{
    CInlineList<boost::shared_ptr<int> > list;
    std::vector<boost::weak_ptr<int> > lifetimes;
    Require(list.GetCount() == 0, "New list is not empty");
    for (int i = 0; i < 64; ++i) {
        boost::shared_ptr<int> value(new int(i));
        lifetimes.push_back(value);
        list.AddTail(value);
    }
    Require(list.GetCount() == 64, "Spilled list lost elements");
    for (int i = 0; i < 64; ++i) Require(*list[i] == i, "Spilled list changed order");
    list[0] = list[63];
    list.Truncate(1);
    for (int i = 0; i < 63; ++i) Require(lifetimes[i].expired(), "Truncation retained an unused value");
    Require(!lifetimes[63].expired() && *list[0] == 63, "Truncation invalidated a retained value");
    list.Truncate(0);
    Require(lifetimes[63].expired(), "Clearing retained the inline value");
    list.AddTail(boost::shared_ptr<int>(new int(7)));
    Require(list.GetCount() == 1 && *list[0] == 7, "Refilling reused stale spill elements");
    CInlineList<boost::shared_ptr<int>> other;
    for (int i=0; i<8; ++i) other.AddTail(boost::shared_ptr<int>(new int(i)));
    list.Swap(other);
    Require(list.GetCount()==8 && *list[7]==7 && other.GetCount()==1 && *other[0]==7,
        "Swapping inline/spilled lists changed values or counts");
    list.Swap(list);
    Require(list.GetCount()==8 && *list[0]==0 && *list[7]==7, "Self swap changed a list");

    std::vector<boost::weak_ptr<DrawItem>> drawLifetimes;
    {
        CompositeDrawItemList draws;
        Require(draws.GetCount() == 0 && !draws.GetHeadPosition(), "New draw list is not empty");
        std::vector<POSITION> positions;
        for (int i=0; i<257; ++i) {
            POSITION added = draws.AddTail();
            positions.push_back(added);
            CompositeDrawItem& item = draws.GetAt(added);
            Require(!item.shadow && !item.outline && !item.body, "New draw slot retained layers");
            item.body.reset(new DrawItem());
            item.body->xsub = i;
            if (i%2) item.shadow = item.outline = item.body;
            drawLifetimes.push_back(item.body);
            if (!i) Require(draws.GetCapacity() == 1, "Single event allocated spill storage");
        }
        Require(draws.GetCount() == 257, "Draw list lost spilled items");
        POSITION pos = draws.GetHeadPosition();
        for (int i=0; i<257; ++i) {
            Require(pos == positions[i], "Draw list positions changed after growth");
            CompositeDrawItem& item = draws.GetNext(pos);
            Require(item.body->xsub == i && draws.GetAt(positions[i]).body == item.body,
                "Draw list growth changed positions, order or ownership");
            Require(i%2 ? item.shadow == item.body && item.outline == item.body : !item.shadow && !item.outline,
                "Draw list growth changed layer ownership");
        }
        Require(!pos, "Draw list iteration passed its end");
        for (size_t i=0; i<drawLifetimes.size(); ++i)
            Require(!drawLifetimes[i].expired(), "Draw list released a live draw");
    }
    for (size_t i=0; i<drawLifetimes.size(); ++i)
        Require(drawLifetimes[i].expired(), "Destroyed draw list retained a layer");

    CSubtitle subtitle;
    subtitle.m_scrAlignment = 5;
    subtitle.m_wrapStyle = 2;
    SharedPtrCWord word(new CPolygon(FwSTSStyle(), L"m 0 0 l 20 0 20 20 0 20", 0, 0, 0, 1, 1, 0));
    boost::weak_ptr<CWord> lifetime(word);
    subtitle.m_words.AddTail(word);
    word.reset();
    subtitle.MakeLines(CSize(320*8, 180*8), CRect(0,0,0,0));
    POSITION position = subtitle.GetHeadLinePosition();
    Require(position != NULL, "Single-word subtitle has no line");
    CLine* line = subtitle.GetNextLine(position);
    Require(position == NULL && line->GetWordCount() == 1, "Single-line iteration changed");
    subtitle.Empty();
    Require(lifetime.expired(), "Empty subtitle retained a word or line");
    subtitle.Empty(); // ownership remains valid across repeated clearing
    Require(subtitle.GetHeadLinePosition() == NULL, "Empty subtitle retained a line position");
    printf("inline_list_contract=passed (spill, order, truncation, refill, ownership, line iteration, draw positions/layers)\n");
    return 0;
}

static int AlphaBufferContract()
{
    const size_t before = g_xy_malloc_used_size;
    {
        AlphaMaskBuffer reusable;
        BYTE* first = reusable.GetBuffer(64);
        memset(first, 37, 64);
        Require(reusable.GetBuffer(32) == first && first[31] == 37, "Scratch buffer did not retain reusable storage");
        reusable.GetBuffer(2048);
        Require(reusable.GetCapacity() == 2048 && !(reinterpret_cast<size_t>(reusable.GetBuffer(64)) & 15), "Scratch growth lost capacity or alignment");
        SharedPtrOverlay overlay(new Overlay());
        overlay->mOverlayWidth = overlay->mOverlayHeight = 8;
        overlay->mOverlayPitch = 16;
        overlay->mBody.reset(static_cast<BYTE*>(xy_malloc(128)), xy_free);
        overlay->mBorder.reset(static_cast<BYTE*>(xy_malloc(128)), xy_free);
        GrayImage2 mask;
        mask.left_top = CPoint(1,1);
        mask.size = CSize(6,6);
        mask.pitch = 16;
        mask.data.reset(static_cast<BYTE*>(xy_malloc(128)), xy_free);
        for (int i = 0; i < 128; ++i) {
            overlay->mBody.get()[i] = i % 65;
            overlay->mBorder.get()[i] = min(64, i % 65 + 15);
            mask.data.get()[i] = (i * 7) % 65;
        }
        const CRect clips[] = { CRect(0,0,8,8), CRect(2,1,7,6), CRect(30,30,40,40) };
        for (int i = 0; i < 144; ++i) {
            AlphaMaskBuffer fresh;
            const bool body = (i/3)%2 != 0, border = (i/6)%2 != 0;
            const GrayImage2* clipper = (i/12)%2 ? &mask : NULL;
            const DWORD colors[] = {(i/72)%2 ? 0 : 0x80000000, (i/24)%2 ? 0 : 0xffffffff, 0xc0000000, 3, 0x60000000, 0x00ffffff};
            const int xsub = ((i/48)%3-1)*8;
            CRect aRect, bRect;
            BYTE* a = Rasterizer::CompositeAlphaMask(overlay, clips[i%3], clipper, xsub, 0, colors, body, border, &aRect, fresh);
            BYTE* b = Rasterizer::CompositeAlphaMask(overlay, clips[i%3], clipper, xsub, 0, colors, body, border, &bRect, reusable);
            Require(aRect == bRect && (a == NULL) == (b == NULL), "Scratch reuse changed mask bounds");
            if (a && !aRect.IsRectEmpty()) {
                const int baseX = (xsub+4)>>3;
                for (int y = aRect.top; y < aRect.bottom; ++y) {
                    const int offset = y*overlay->mOverlayPitch + aRect.left-baseX;
                    Require(!memcmp(a+offset, b+offset, aRect.Width()), "Scratch reuse changed mask pixels");
                }
            }
        }
    }
    Require(g_xy_malloc_used_size == before, "Scratch or mask buffers leaked aligned storage");
    printf("alpha_buffer_contract=passed (reuse, growth, alignment, masks, clips, colors, release)\n");
    return 0;
}


static int FusedAlphaContract()
{
    const size_t before=g_xy_malloc_used_size;
    {
        SharedPtrOverlay overlay(new Overlay());
        overlay->mOffsetX=-19; overlay->mOffsetY=11;
        overlay->mOverlayWidth=65*65; overlay->mOverlayHeight=3;
        overlay->mOverlayPitch=(overlay->mOverlayWidth+15)&~15;
        const size_t bytes=overlay->mOverlayPitch*overlay->mOverlayHeight;
        overlay->mBody.reset(static_cast<BYTE*>(xy_malloc(bytes)),xy_free);
        overlay->mBorder.reset(static_cast<BYTE*>(xy_malloc(bytes)),xy_free);
        GrayImage2 mask; mask.left_top=CPoint(-10,-10); mask.size=CSize(65*65+30,30);
        mask.pitch=(mask.size.cx+15)&~15;
        mask.data.reset(static_cast<BYTE*>(xy_malloc(mask.pitch*mask.size.cy)),xy_free);
        for(size_t i=0;i<bytes;++i) { overlay->mBody.get()[i]=i%65; overlay->mBorder.get()[i]=(i/65)%65; }
        for(int y=0;y<mask.size.cy;++y) for(int x=0;x<mask.pitch;++x) mask.data.get()[y*mask.pitch+x]=(x/65+y)%65;
        const CRect bitmapRect(-10,-10,65*65+20,20);
        boost::scoped_ptr<XyBitmap> wanted(XyBitmap::CreateBitmap(bitmapRect,XyBitmap::PACK));
        boost::scoped_ptr<XyBitmap> actual(XyBitmap::CreateBitmap(bitmapRect,XyBitmap::PACK));
        const auto compare=[&](const CRect& clip,const GrayImage2* clipper,int xsub,int ysub,unsigned opacity,bool body,bool border) {
            memset(wanted->bits,173,wanted->pitch*wanted->h); memset(actual->bits,173,actual->pitch*actual->h);
            DWORD colors[6]={DWORD((opacity<<24)|0x37bdf0),0xffffffff,0,0,0,0};
            AlphaMaskBuffer scratch; CRect a,b;
            BYTE* alpha=Rasterizer::CompositeAlphaMask(overlay,clip,clipper,xsub,ysub,colors,body,border,&a,scratch);
            if(alpha) Rasterizer::Draw(wanted.get(),overlay,a,alpha,xsub,ysub,colors,body,border);
            Require(Rasterizer::DrawSingleColorFused(actual.get(),overlay,clip,clipper,xsub,ysub,colors,body,border,&b),"Packed single-color path was rejected");
            Require(a==b && !memcmp(wanted->bits,actual->bits,wanted->pitch*wanted->h),"Fused blending changed exact bytes or dirty bounds");
        };
        // Every opacity and all 65x65 body/border combinations; vector and tail.
        for(unsigned opacity=0;opacity<=255;++opacity) for(int kind=0;kind<6;++kind)
            compare(bitmapRect,kind>=3 ? &mask : NULL,5,2,opacity,kind%3!=2,kind%3!=0);
        for(int width=1;width<=65;++width) for(int left=-4;left<=17;left+=3) for(int kind=0;kind<6;++kind)
            compare(CRect(left,-3,left+width,9),kind>=3 ? &mask : NULL,-7,13,(width*13)%256,kind%3!=2,kind%3!=0);
        compare(CRect(0,15,20,19),NULL,0,0,255,true,false);
        compare(bitmapRect,NULL,0,0,255,false,false);
        boost::scoped_ptr<XyBitmap> planar(XyBitmap::CreateBitmap(CRect(0,0,20,20),XyBitmap::PLANNA));
        DWORD single[6]={0x80112233,0xffffffff,0,0,0,0},multi[6]={0x80112233,4,0xffabcdef,0xffffffff,0,0}; CRect dirty;
        Require(!Rasterizer::DrawSingleColorFused(planar.get(),overlay,bitmapRect,NULL,0,0,single,true,false,&dirty) &&
            !Rasterizer::DrawSingleColorFused(actual.get(),overlay,bitmapRect,NULL,0,0,multi,true,false,&dirty),"Unsupported format/colors bypassed legacy rendering");
    }
    Require(g_xy_malloc_used_size==before,"Fused kernel contract leaked image storage");
    printf("fused_alpha_contract=passed (every opacity, coverage pairs, masks, border subtraction, widths/tails, clipping/offsets, arbitrary destination, format/color fallbacks)\n");
    return 0;
}

static int ZeroAlphaContract()
{
    SharedPtrOverlay overlay(new Overlay());
    overlay->mOverlayWidth = overlay->mOverlayHeight = 8;
    overlay->mOverlayPitch = 16;
    overlay->mBody.reset(static_cast<BYTE*>(xy_malloc(128)), xy_free);
    overlay->mBorder.reset(static_cast<BYTE*>(xy_malloc(128)), xy_free);
    memset(overlay->mBody.get(), 64, 128);
    memset(overlay->mBorder.get(), 64, 128);
    std::vector<BYTE> zero(128,0);
    const DWORD colors[] = {0x0012ef90,0xffffffff,0,0,0,0};
    for (int layout = 0; layout < 2; ++layout) {
        boost::shared_ptr<XyBitmap> bitmap(XyBitmap::CreateBitmap(CRect(0,0,8,8),
            layout ? XyBitmap::PLANNA : XyBitmap::PACK));
        const size_t bytes = bitmap->pitch * bitmap->h * (layout ? 4 : 1);
        for (size_t i = 0; i < bytes; ++i) static_cast<BYTE*>(bitmap->bits)[i] = (i*37+19)&255;
        const std::vector<BYTE> before(static_cast<BYTE*>(bitmap->bits),static_cast<BYTE*>(bitmap->bits)+bytes);
        for (int border = 0; border < 2; ++border) {
            Rasterizer::Draw(bitmap.get(),overlay,CRect(1,1,7,6),zero.data(),0,0,colors,true,border != 0);
            Require(!memcmp(before.data(),bitmap->bits,bytes), "Zero-alpha draw changed background pixels");
            if (!layout) {
                Rasterizer::AdditionDraw(bitmap.get(),overlay,CRect(1,1,7,6),zero.data(),0,0,colors,true,border != 0);
                Require(!memcmp(before.data(),bitmap->bits,bytes), "Zero-alpha additive draw changed background pixels");
            }
        }
    }
    printf("zero_alpha_contract=passed (packed, planar, additive, clipping, background preservation)\n");
    return 0;
}

static int EffectContract()
{
    CSubtitle subtitle;
    std::set<Effect*> addresses;
    for (int i = 0; i < EF_NUMBEROFEFFECTS; ++i) {
        Effect* effect = subtitle.CreateEffect(static_cast<eftype>(i));
        Require(effect && effect->type == i, "Effect has the wrong type");
        for (int j = 0; j < 8; ++j) Require(effect->param[j] == 0, "New effect retained parameters");
        for (int j = 0; j < 4; ++j) Require(effect->t[j] == 0, "New effect retained timing");
        effect->param[0] = i+10;
        addresses.insert(effect);
    }
    Require(addresses.size() == EF_NUMBEROFEFFECTS, "Effects overlap in storage");
    for (int i = 0; i < EF_NUMBEROFEFFECTS; ++i)
        Require(subtitle.CreateEffect(static_cast<eftype>(i))->param[0] == i+10, "Effect lookup reset a live value");
    subtitle.Empty();
    subtitle.Empty();
    Effect* reused = subtitle.CreateEffect(EF_MOVE);
    Require(reused->param[0] == 0 && reused->t[0] == 0, "Reused effect retained old state");
    printf("effect_contract=passed (types, isolation, lookup, clear, reuse, ownership)\n");
    return 0;
}

static int ReconstructionPoolContract()
{
    CSubtitlePool pool;
    std::vector<CSubtitle*> events;
    boost::weak_ptr<CWord> released;
    for (size_t i = 0; i < pool.GetCapacity()+100; ++i) {
        CSubtitle* event = pool.Acquire();
        event->CreateEffect(EF_MOVE)->param[0] = 99;
        event->m_fAnimated2 = true;
        event->m_clipInverse = true;
        STSStyle style;
        SharedPtrCWord word = boost::make_shared<CText>(FwSTSStyle(style), L"word", 0, 0, 0);
        if (!i) released = word;
        event->m_words.AddTail(word);
        event->m_scrAlignment = 2;
        event->m_wrapStyle = 2;
        event->MakeLines(CSize(1000,1000), CRect(0,0,0,0));
        events.push_back(event);
    }
    for (size_t i = 0; i < events.size(); ++i) pool.Recycle(events[i]);
    Require(released.expired(), "Pool retained word ownership");
    Require(pool.GetCount() == pool.GetCapacity(), "Pool did not enforce its item limit");
    Require(pool.GetRetainedBytes() < 1024*1024, "Pool exceeded its storage bound");
    events.clear();
    for (size_t i = 0; i < pool.GetCapacity(); ++i) {
        CSubtitle* event = pool.Acquire();
        Require(!event->GetHeadLinePosition() && !event->m_words.GetCount(), "Pool retained lines or words");
        Require(!event->m_pClipper && !event->m_fAnimated2 && !event->m_clipInverse, "Pool retained event state");
        for (int j = 0; j < EF_NUMBEROFEFFECTS; ++j) Require(!event->m_effects[j], "Pool retained effects");
        events.push_back(event);
    }
    Require(pool.GetHits() == pool.GetCapacity(), "Pool did not reuse storage");
    for (size_t i = 0; i < events.size(); ++i) pool.Recycle(events[i]);
    pool.Clear();
    pool.Clear();
    Require(pool.GetCount() == 0, "Pool clear retained storage");
    printf("reconstruction_pool_contract=passed (bound, reuse, reset, ownership, clear)\n");
    return 0;
}

static int LayerKeyContract()
{
    for (int border = 0; border < 2; ++border) {
        STSStyle style;
        style.borderStyle = border;
        style.fontAngleZ = 15;
        SharedPtrCWord word = boost::make_shared<CPolygon>(FwSTSStyle(style), L"m 0 0 l 20 0 20 20 0 20", 0, 0, 0, 1., 1., 0);
        SharedPtrOverlayPaintMachine shadow, outline, body;
        CWordPaintMachine::CreatePaintMachines(word, CPoint(16,24), CPoint(-8,8), CPoint(0,0), CPoint(100,100), &shadow, &outline, &body);
        Require(shadow->GetHashKey() == outline->GetHashKey() && shadow == outline, "Matching outline/shadow geometry was not shared");
        Require((body->GetHashKey() == outline->GetHashKey()) == !border && (body == outline) == !border, "Opaque-box/body geometry isolation changed");
        CWordPaintMachine::CreatePaintMachines(word, CPoint(16,24), CPoint(-7,8), CPoint(0,0), CPoint(100,100), &shadow, &outline, &body);
        Require(shadow->GetHashKey() != outline->GetHashKey() && shadow != outline, "Different fractional positions shared geometry");
        if (!border) Require(shadow->GetHashKey() == body->GetHashKey(), "Matching body/shadow keys were not shared");
        boost::weak_ptr<CWord> lifetime(word);
        word.reset();
        Require(!lifetime.expired(), "Paint machine did not retain its word");
        shadow.reset(); outline.reset(); body.reset();
        Require(lifetime.expired(), "Layer sharing retained a word after release");
    }
    printf("layer_key_contract=passed (fractions, negative positions, opaque box, ownership)\n");
    return 0;
}

static int GroupKeyContract()
{
    CCritSec lock;
    CRenderedTextSubtitle context(&lock);
    XyFwGroupedDrawItemsHashKey::GetCacher()->RemoveAll();
    boost::shared_ptr<XyFwGroupedDrawItemsHashKey> retained;
    boost::weak_ptr<CWord> wordLifetime;
    boost::weak_ptr<DrawItemHashKey> childLifetime;
    {
        SharedPtrCWord word = boost::make_shared<CPolygon>(FwSTSStyle(), L"m 0 0 l 20 0 20 20 0 20", 0, 0, 0, 1., 1., 0);
        wordLifetime = word;
        SharedPtrOverlayPaintMachine body;
        CWordPaintMachine::CreatePaintMachines(word,CPoint(),CPoint(),CPoint(),CPoint(),NULL,NULL,&body);
        SharedPtrCClipperPaintMachine clipper = boost::make_shared<CClipperPaintMachine>(SharedPtrCClipper());
        DWORD colors[6] = {0x80aabbcc,0xffffffff,0,0,0,0};
        GroupedDrawItems group;
        group.clip_rect = CRect(0,0,100,100);
        for (int i = 0; i < 8; ++i)
            group.draw_item_list.AddTail(DrawItem::CreateDrawItem(body,group.clip_rect,clipper,i*8,0,colors,true,false));
        childLifetime = group.draw_item_list[0]->GetHashKey();
        GroupedDrawItemsHashKey probe;
        group.CreateHashKey(&probe);
        retained = boost::make_shared<XyFwGroupedDrawItemsHashKey>(probe);
        XyFwGroupedDrawItemsHashKey hit(probe);
        XyFwGroupedDrawItemsHashKey owned(new GroupedDrawItemsHashKey(probe));
        Require(retained->GetId() == hit.GetId() && hit.GetId() == owned.GetId(), "Borrowed and owned group probes changed identity");
        Require(&retained->Get() != &probe, "Group interning retained a stack probe");
        GroupedDrawItemsHashKey original(probe);
        ++group.clip_rect.right;
        group.CreateHashKey(&probe);
        Require(XyFwGroupedDrawItemsHashKey(probe).GetId() != retained->GetId(), "Group key ignored clipping");
        --group.clip_rect.right;
        std::swap(group.draw_item_list[0],group.draw_item_list[1]);
        group.CreateHashKey(&probe);
        Require(XyFwGroupedDrawItemsHashKey(probe).GetId() != retained->GetId(), "Group key ignored drawing order");
        group.draw_item_list.Truncate(0);
        group.CreateHashKey(&probe);
        Require(!(probe == original) && retained->Get() == original, "Group mutation changed an owned key");
        XyFwGroupedDrawItemsHashKey::GetCacher()->RemoveAll();
        Require(retained->Get() == original, "Eviction invalidated an owned group key");
    }
    XyFwGroupedDrawItemsHashKey::GetCacher()->RemoveAll();
    Require(wordLifetime.expired() && !childLifetime.expired(), "Group keys retained their parent word or lost their child");
    retained.reset();
    Require(childLifetime.expired(), "Group key release retained a child key");
    printf("group_key_contract=passed (borrowed probes, spill, identity, order, clipping, eviction, lifetime)\n");
    return 0;
}

static int GroupViewContract()
{
    CCritSec lock;
    CRenderedTextSubtitle context(&lock);
    typedef XyFwGroupedDrawItemsHashKey Flyweight;
    const size_t previous = CacheManager::GetBitmapMruCache()->GetMaxItemNum();
    const size_t previousKeys = Flyweight::GetCacher()->GetMaxItemNum();
    CacheManager::SetBitmapCacheSize(3);
    Flyweight::GetCacher()->RemoveAll();
    CacheManager::GetBitmapMruCache()->RemoveAll();
    XySubRenderFrameCreater* creator = XySubRenderFrameCreater::GetDefaultCreater();
    const CRect bounds(0,0,100,100);
    Require(SUCCEEDED(creator->SetColorSpace(XY_CS_ARGB)) &&
        SUCCEEDED(creator->SetOutputRect(bounds)) && SUCCEEDED(creator->SetClipRect(bounds)),
        "Cannot configure group-view rendering");
    Flyweight::SharedConstV retained;
    boost::weak_ptr<CWord> wordLifetime;
    boost::weak_ptr<DrawItemHashKey> childLifetime;
    SharedPtrXyBitmap first;
    {
        SharedPtrCWord word = boost::make_shared<CPolygon>(FwSTSStyle(),L"m 0 0 l 20 0 20 20 0 20",0,0,0,1.,1.,0);
        wordLifetime = word;
        SharedPtrOverlayPaintMachine body;
        CWordPaintMachine::CreatePaintMachines(word,CPoint(),CPoint(),CPoint(),CPoint(),NULL,NULL,&body);
        SharedPtrCClipperPaintMachine clipper = boost::make_shared<CClipperPaintMachine>(SharedPtrCClipper());
        const DWORD colors[6] = {0x80aabbcc,0xffffffff,0,0,0,0};
        GroupedDrawItems group;
        group.clip_rect = bounds;
        for (int i=0; i<12; ++i)
            group.draw_item_list.AddTail(DrawItem::CreateDrawItem(body,bounds,clipper,i*8,0,colors,true,false));
        childLifetime = group.draw_item_list[0]->GetHashKey();
        GroupedDrawItemsHashKey snapshot;
        group.CreateHashKey(&snapshot);
        Require(!snapshot.IsBorrowed(), "Owning group-key API returned a view");
        AlphaMaskBuffer scratch;
        int firstId, hitId, changedId;
        group.Draw(&first,&firstId,scratch); // view miss must materialize ownership
        const Flyweight::SharedConstV probe(Flyweight::SharedConstV(),&snapshot);
        POSITION position = Flyweight::GetCacher()->Lookup(probe);
        Require(position!=NULL,"Group-view miss was not interned");
        retained = Flyweight::GetCacher()->GetKeyAt(position);
        Require(!retained->IsBorrowed() && *retained==snapshot && retained->GetHashValue()==snapshot.GetHashValue(),
            "Group interning stored a borrowed source or changed key semantics");
        GroupedDrawItemsHashKey assigned;
        assigned = *retained;
        GroupedDrawItemsHashKey copied(*retained);
        Require(!assigned.IsBorrowed() && !copied.IsBorrowed() && assigned==snapshot && copied==snapshot,
            "Group-key copying or assignment lost ownership");
        SharedPtrXyBitmap hit;
        group.Draw(&hit,&hitId,scratch);
        Require(firstId==hitId && first==hit,"Borrowed lookup failed to reuse bitmap identity/storage");
        Require(first && first->type==XyBitmap::PACK && first->w>0 && first->h>0,
            "Group view did not produce a packed bitmap");
        std::vector<BYTE> original(static_cast<size_t>(first->w)*first->h*4);
        for (int y=0;y<first->h;++y)
            memcpy(&original[static_cast<size_t>(y)*first->w*4],
                static_cast<const BYTE*>(first->bits)+y*first->pitch,first->w*4);
        for (int count=0; count<=1; ++count) {
            GroupedDrawItems shortGroup;
            shortGroup.clip_rect=bounds;
            if (count) shortGroup.draw_item_list.AddTail(group.draw_item_list[0]);
            GroupedDrawItemsHashKey expected;
            shortGroup.CreateHashKey(&expected);
            SharedPtrXyBitmap a,b;
            int aId,bId;
            shortGroup.Draw(&a,&aId,scratch); shortGroup.Draw(&b,&bId,scratch);
            const Flyweight::SharedConstV smallProbe(Flyweight::SharedConstV(),&expected);
            POSITION entry=Flyweight::GetCacher()->Lookup(smallProbe);
            Require(aId==bId && a==b && entry && !Flyweight::GetCacher()->GetKeyAt(entry)->IsBorrowed(),
                "Empty/inline group view lost ownership or hit identity");
        }
        std::swap(group.draw_item_list[0],group.draw_item_list[11]);
        group.Draw(&hit,&changedId,scratch);
        Require(changedId!=firstId && *retained==snapshot && assigned==snapshot,
            "Reordering a source group mutated an owned key or reused its identity");
        ++group.clip_rect.right;
        int clippedId;
        group.Draw(&hit,&clippedId,scratch);
        Require(clippedId!=changedId && *retained==snapshot,
            "Changing clipping mutated an owned key or reused its identity");
        CacheManager::SetBitmapCacheSize(0);
        int bypassId, nextBypassId;
        group.Draw(&hit,&bypassId,scratch);
        group.Draw(&hit,&nextBypassId,scratch);
        Require(bypassId!=nextBypassId && !Flyweight::GetCacher()->GetCurItemNum(),
            "Disabled group-view interning retained a view or reused an identity");
        for (int y=0;y<first->h;++y)
            Require(!memcmp(&original[static_cast<size_t>(y)*first->w*4],
                static_cast<const BYTE*>(first->bits)+y*first->pitch,first->w*4),
                "Source mutation or eviction modified a retained bitmap");
    }
    Require(wordLifetime.expired() && !childLifetime.expired(),"Retained group view owns a word or lost a child key");
    retained.reset();
    Require(childLifetime.expired(),"Released group key retained a child");
    CacheManager::GetBitmapMruCache()->SetMaxItemNum(previous);
    Flyweight::GetCacher()->SetMaxItemNum(previousKeys);
    printf("group_view_contract=passed (miss ownership, hits, copies, mutation, eviction, zero bypass, retained pixels)\n");
    return 0;
}

static int KaraokeHashContract()
{
    CCritSec lock;
    CRenderedTextSubtitle context(&lock);
    SharedPtrCWord word = boost::make_shared<CText>(FwSTSStyle(),L"karaoke",1,0,10000);
    SharedPtrOverlayPaintMachine body;
    CWordPaintMachine::CreatePaintMachines(word,CPoint(),CPoint(),CPoint(),CPoint(),NULL,NULL,&body);
    SharedPtrCClipperPaintMachine clipper = boost::make_shared<CClipperPaintMachine>(SharedPtrCClipper());
    DWORD colors[6] = {0xffaabbcc,0,0xff112233,0,0,0};
    SharedPtrDrawItem item = DrawItem::CreateDrawItem(body,CRect(0,0,1920,1080),clipper,0,0,colors,true,false);
    DrawItemHashKey original(*item);
    original.UpdateHashValue();
    std::set<ULONG> hashes;
    for (int field = 0; field < 6; ++field) {
        DrawItem changed = *item;
        changed.switchpts[field] ^= 1;
        DrawItemHashKey key(changed);
        Require(key.UpdateHashValue() != original.GetHashValue() && !(key == original), "Switch word missing from draw hash");
    }
    for (DWORD split = 0; split < 1000; ++split) {
        DrawItem changed = *item;
        changed.switchpts[3] = split;
        DrawItemHashKey key(changed);
        hashes.insert(key.UpdateHashValue());
        DrawItemHashKey equal(changed);
        Require(equal.UpdateHashValue() == key.GetHashValue() && equal == key, "Equal karaoke keys have different hashes");
    }
    Require(hashes.size() == 1000, "Progressing karaoke hashes collapsed");
    printf("karaoke_hash_contract=passed (six switch words, 1000 distinct positions, equal-key consistency)\n");
    return 0;
}

static int CacheCapacityContract()
{
    EnhancedXyMru<int,int> cache(0);
    bool added = false;
    Require(cache.UpdateCache(1,2) == NULL && cache.AddHeadIfNotExists(1,2,&added) == NULL && added,
        "Disabled MRU returned a position");
    Require(cache.GetCurItemNum() == 0 && cache.Lookup(1) == NULL && cache.GetCacheHitCount() == 0,
        "Disabled MRU retained an entry or reported a hit");
    cache.SetMaxItemNum(2);
    cache.UpdateCache(1,2); cache.UpdateCache(2,3);
    Require(cache.Lookup(1) != NULL, "MRU did not enable after bypass");
    cache.UpdateCache(cache.Lookup(1));
    cache.SetMaxItemNum(1);
    Require(cache.Lookup(1) != NULL && cache.Lookup(2) == NULL, "Shrink discarded the most recent entry");
    cache.SetMaxItemNum(0);
    Require(cache.GetCurItemNum() == 0 && cache.Lookup(1) == NULL, "Disabling MRU did not release entries");

    const std::size_t previous = CacheManager::GetBitmapMruCache()->GetMaxItemNum();
    const std::size_t previousKeys = XyFwGroupedDrawItemsHashKey::GetCacher()->GetMaxItemNum();
    const std::size_t capacities[] = {0,4,1,0,8};
    GroupedDrawItemsHashKey key;
    GroupedDrawItems group;
    group.clip_rect = CRect(0,0,32,32); group.CreateHashKey(&key);
    boost::shared_ptr<XyFwGroupedDrawItemsHashKey> held;
    for (std::size_t items : capacities) {
        CacheManager::SetBitmapCacheSize(items);
        Require(CacheManager::GetBitmapMruCache()->GetMaxItemNum() == items &&
            XyFwGroupedDrawItemsHashKey::GetCacher()->GetMaxItemNum() == items, "Coupled capacities diverged");
        held = boost::make_shared<XyFwGroupedDrawItemsHashKey>(key);
        XyFwGroupedDrawItemsHashKey hit(key);
        XyFwGroupedDrawItemsHashKey owned(new GroupedDrawItemsHashKey(key));
        Require(items ? held->GetId() == hit.GetId() && hit.GetId() == owned.GetId() :
            held->GetId() != hit.GetId() && hit.GetId() != owned.GetId() && held->GetId() != owned.GetId(),
            "Interning identity does not match enabled/disabled policy");
        Require(held->GetId() != XyFwGroupedDrawItemsHashKey::INVALID_ID && held->Get() == key, "Bypass lost owned key");
        if (!items) Require(XyFwGroupedDrawItemsHashKey::GetCacher()->GetCurItemNum() == 0, "Disabled interning retained a key");
    }
    CacheManager::SetBitmapCacheSize(0);
    Require(held->Get() == key, "Disabling cache invalidated a retained key");
    CacheManager::GetBitmapMruCache()->SetMaxItemNum(previous);
    XyFwGroupedDrawItemsHashKey::GetCacher()->SetMaxItemNum(previousKeys);
    printf("cache_capacity_contract=passed (zero bypass, enable, shrink, disable, identity, owned lifetime)\n");
    return 0;
}

static int WordKeyCacheContract()
{
    CCritSec lock;
    CRenderedTextSubtitle context(&lock);
    boost::weak_ptr<CWord> lifetime;
    SharedPtrDrawItemHashKey held;
    {
        STSStyle style;
        SharedPtrCWord word = boost::make_shared<CPolygon>(FwSTSStyle(style), L"m 0 0 l 20 0 20 20 0 20", 0, 0, 0, 1., 1., 0);
        lifetime = word;
        SharedPtrOverlayPaintMachine body;
        SharedPtrOverlayKey keys[4];
        const auto machine = [&](int fraction) {
            CWordPaintMachine::CreatePaintMachines(word,CPoint(),CPoint(),CPoint(fraction,0),CPoint(100,100),NULL,NULL,&body);
        };
        for (int i = 0; i < 3; ++i) { machine(i); keys[i] = body->GetHashKey(); }
        machine(0);
        Require(body->GetHashKey() == keys[0], "Word geometry key was not reused");
        machine(3); keys[3] = body->GetHashKey();
        machine(0);
        Require(body->GetHashKey() != keys[0], "Word geometry cache did not evict its oldest slot");
        style.fontAngleZ = 17;
        word->m_style = FwSTSStyle(style);
        SharedPtrOverlayKey old = body->GetHashKey();
        machine(0);
        Require(body->GetHashKey() != old, "Changed word style reused an old geometry key");
        OverlayKey expected(*word,CPoint(0,0),CPoint(100,100));
        expected.UpdateHashValue();
        Require(*body->GetHashKey() == expected, "Geometry cache changed the key value");
        SharedPtrCClipperPaintMachine clipper = boost::make_shared<CClipperPaintMachine>(SharedPtrCClipper());
        DWORD colors[6] = {0x80aabbcc,0xffffffff,0,0,0,0};
        SharedPtrDrawItem a = DrawItem::CreateDrawItem(body,CRect(0,0,100,100),clipper,0,0,colors,true,false);
        SharedPtrDrawItem b = DrawItem::CreateDrawItem(body,CRect(0,0,100,100),clipper,0,0,colors,true,false);
        Require(a->GetHashKey() == b->GetHashKey(), "Word draw key was not reused");
        held = a->GetHashKey();
        SharedPtrOverlayPaintMachine previousBody = body;
        style.fGaussianBlur = 0.0000001;
        word->m_style = FwSTSStyle(style);
        machine(0);
        Require(body->GetHashKey()->GetHashValue() != previousBody->GetHashKey()->GetHashValue(), "Near-equal test did not change the geometry hash");
        SharedPtrDrawItem nearEqual = DrawItem::CreateDrawItem(body,CRect(0,0,100,100),clipper,0,0,colors,true,false);
        Require(nearEqual->GetHashKey() != held, "Near-equal geometry changed draw-key hash semantics");
        body = previousBody;
        DrawItem changed = *a;
        for (int i = 0; i < 6; ++i) {
            changed = *a; changed.switchpts[i] ^= 1;
            Require(!held->MatchesDrawItem(changed), "Draw key ignored a color/switch change");
        }
        changed = *a; ++changed.xsub; Require(!held->MatchesDrawItem(changed), "Draw key ignored X");
        changed = *a; ++changed.ysub; Require(!held->MatchesDrawItem(changed), "Draw key ignored Y");
        changed = *a; ++changed.clip_rect.left; Require(!held->MatchesDrawItem(changed), "Draw key ignored clipping");
        changed = *a; changed.fBody = false; Require(!held->MatchesDrawItem(changed), "Draw key ignored body flag");
        changed = *a; changed.fBorder = true; Require(!held->MatchesDrawItem(changed), "Draw key ignored border flag");
        changed = *a;
        changed.clipper = boost::make_shared<CClipperPaintMachine>(boost::make_shared<CClipper>(L"m 0 0 l 20 0 20 20 0 20",CSizeCoor2(100,100),1.,1.,true));
        Require(!held->MatchesDrawItem(changed), "Draw key ignored a vector clipper");
        for (int i = 1; i < 5; ++i) DrawItem::CreateDrawItem(body,CRect(0,0,100,100),clipper,i,0,colors,true,false);
        Require(held->MatchesDrawItem(*a), "Eviction mutated a retained draw key");
        SharedPtrCWord text = boost::make_shared<CText>(FwSTSStyle(),L"a",0,0,0);
        CWordPaintMachine::CreatePaintMachines(text,CPoint(),CPoint(),CPoint(),CPoint(),NULL,NULL,&body);
        SharedPtrOverlayKey beforeAppend = body->GetHashKey();
        Require(text->Append(boost::make_shared<CText>(FwSTSStyle(),L"b",0,0,0)), "Text append failed");
        CWordPaintMachine::CreatePaintMachines(text,CPoint(),CPoint(),CPoint(),CPoint(),NULL,NULL,&body);
        Require(body->GetHashKey() != beforeAppend, "Changed word text reused an old geometry key");
    }
    Require(lifetime.expired(), "Word key retention formed an ownership cycle");
    printf("word_key_cache_contract=passed (reuse, bound, eviction, style, text, colors, positions, clips, flags, ownership)\n");
    return 0;
}

static void LoadFonts(const wchar_t* folder)
{
    CString pattern = CString(folder) + L"\\*";
    WIN32_FIND_DATAW item;
    HANDLE find = FindFirstFileW(pattern, &item);
    Require(find != INVALID_HANDLE_VALUE, "Cannot read font directory");
    do {
        CString name(item.cFileName);
        CString ext = name.Right(4);
        if (!ext.CompareNoCase(L".ttf") || !ext.CompareNoCase(L".otf")) {
            CString path = CString(folder) + L"\\" + name;
            Require(AddFontResourceExW(path, FR_PRIVATE, NULL) > 0, "Cannot load an attached font");
        }
    } while (FindNextFileW(find, &item));
    FindClose(find);
}

static bool EqualFrames(IXySubRenderFrame* a, IXySubRenderFrame* b)
{
    if (!a || !b) return a == b;
    int na, nb, ca, cb;
    Require(SUCCEEDED(a->GetBitmapCount(&na)) && SUCCEEDED(b->GetBitmapCount(&nb)), "Cannot read frame count");
    Require(SUCCEEDED(a->GetXyColorSpace(&ca)) && SUCCEEDED(b->GetXyColorSpace(&cb)), "Cannot read colorspace");
    if (na != nb || ca != cb) return false;
    for (int i = 0; i < na; ++i) {
        POINT pa, pb;
        SIZE sa, sb;
        LPCVOID ba, bb;
        int pitcha, pitchb;
        Require(SUCCEEDED(a->GetBitmap(i, NULL, &pa, &sa, &ba, &pitcha)) &&
            SUCCEEDED(b->GetBitmap(i, NULL, &pb, &sb, &bb, &pitchb)), "Cannot read test bitmap");
        if (pa.x != pb.x || pa.y != pb.y || sa.cx != sb.cx || sa.cy != sb.cy) return false;
        if (ca == XY_CS_AYUV_PLANAR) {
            XyPlannerFormatExtra planesA, planesB;
            Require(SUCCEEDED(a->GetBitmapExtra(i,&planesA)) && SUCCEEDED(b->GetBitmapExtra(i,&planesB)), "Cannot read test planes");
            for (int c=0;c<4;++c) for (int y=0;y<sa.cy;++y)
                if (memcmp((const BYTE*)planesA.plans[c]+y*pitcha,(const BYTE*)planesB.plans[c]+y*pitchb,sa.cx)) return false;
        } else for (int y = 0; y < sa.cy; ++y)
            if (memcmp((const BYTE*)ba + y * pitcha, (const BYTE*)bb + y * pitchb, sa.cx * 4)) return false;
    }
    return true;
}

static int FontLayoutContract()
{
    struct SourceRenderer : CRenderedTextSubtitle {
        SourceRenderer(CCritSec* lock) : CRenderedTextSubtitle(lock) {}
        void Source(const CStringW& text, bool unicode) {
            Deinit();
            m_entries[0].str = text;
            m_entries[0].fUnicode = unicode;
        }
        void Geometry(EPARCompensationType type, double factor) {
            Deinit(); m_ePARCompensationType=type; m_dPARCompensation=factor;
        }
    };
    const char script[] =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,23,&H00FFFFFF,&H000000FF,&H00000000,&H40000000,0,0,0,0,100,100,0,0,1,2,1,5,12,17,9,0\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,Placeholder\n";
    CCritSec lock;
    SourceRenderer unicode(&lock), ansi(&lock), reference(&lock);
    SourceRenderer* renderers[] = {&unicode,&ansi,&reference};
    for (size_t i=0; i<_countof(renderers); ++i)
        Require(renderers[i]->Open((BYTE*)script,sizeof(script)-1,DEFAULT_CHARSET,L"font-layout"),"Cannot open font/layout contract");
    const CStringW text(L"{\\q0\\r\\t(200,200,\\1c&H00FF00&)}caf\x00e9");
    // CP1252 stores this character as byte E9; the reference is real Unicode.
    unicode.Source(text,true); ansi.Source(text,false); reference.Source(text,true);
    Require(unicode.IsEntryUnicode(0) && !ansi.IsEntryUnicode(0),"Source contract did not exercise both encodings");
    reference.SetReconstructionCacheLimit(0);
    reference.SetParsedTagCacheLimit(0);
    STSStyle forced; forced.charSet=ANSI_CHARSET; forced.fontName=L"Times New Roman";
    forced.fontSize=19; forced.fontScaleX=forced.fontScaleY=110;
    forced.marginRect=CRect(7,11,13,17); forced.scrAlignment=8;
    forced.outlineWidthX=0.3; forced.outlineWidthY=2.7;
    const RECT rect={0,0,320,180}; const SIZE size={320,180};
    const int times[]={0,99,199,200,201,500,700,999};
    for (int mode=0; mode<4; ++mode) for (int scale=0; scale<2; ++scale) for (int force=0; force<2; ++force) {
        for (size_t i=0; i<_countof(renderers); ++i) {
            renderers[i]->SetDefaultStyle(forced);
            renderers[i]->SetForceDefaultStyle(force!=0);
            renderers[i]->Geometry(static_cast<CSimpleTextSubtitle::EPARCompensationType>(mode),scale ? 1.25 : 0.75);
        }
        for (size_t i=0; i<_countof(times); ++i) {
            CComPtr<IXySubRenderFrame> a,b,c;
            Require(SUCCEEDED(unicode.RenderEx(&a,MSP_RGBA,rect,rect,size,REFERENCE_TIME(times[i])*10000,24)) &&
                SUCCEEDED(ansi.RenderEx(&b,MSP_RGBA,rect,rect,size,REFERENCE_TIME(times[i])*10000,24)) &&
                SUCCEEDED(reference.RenderEx(&c,MSP_RGBA,rect,rect,size,REFERENCE_TIME(times[i])*10000,24)),"Font/layout contract render failed");
            Require(EqualFrames(a,b) && EqualFrames(a,c),"Source encoding, style copying or cached single-word layout changed pixels");
        }
        Require(*unicode.GetStyle(0)==*ansi.GetStyle(0) && *unicode.GetStyle(0)==*reference.GetStyle(0),"Rendering mutated the selected source style");
    }
    Require(unicode.GetReconstructionCacheHits()>0 && ansi.GetReconstructionCacheHits()>0,"Font/layout contract did not exercise cached hits");
    unicode.Source(L"{\\q3\\r}Changed",true); reference.Source(L"{\\q3\\r}Changed",true);
    CComPtr<IXySubRenderFrame> edited,expected;
    Require(SUCCEEDED(unicode.RenderEx(&edited,MSP_RGBA,rect,rect,size,5000000,24)) &&
        SUCCEEDED(reference.RenderEx(&expected,MSP_RGBA,rect,rect,size,5000000,24)) && EqualFrames(edited,expected),"Source replacement retained borrowed text");
    printf("font_layout_contract=passed (Unicode/ANSI ownership, cache hits, PAR modes, forced styles, reset, source edit)\n");
    return 0;
}

static int PreparedLayoutContract()
{
    const char header[] =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,12,12,12,1\n"
        "Style: Box,Tahoma,18,&H00A0D0FF,&H0000FF00,&H00334455,&H40000000,0,0,0,0,100,100,0,7,3,2,1,7,8,8,8,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    const char* cases[] = {
        "{\\t(0,2000,\\1c&H80A0F0&\\alpha&H90&)}one",
        "{\\q0\\t(0,2000,\\c&H10FF90&)}  several words wrap across the available width \\Nsecond line\\nsoft break\\hNBSP  ",
        "{\\q1\\t(0,2000,2,\\alpha&HFF&)}multi word layout for balanced wrapping with another sentence",
        "{\\q2\\t(0,2000,\\2c&H10F020&)}hard\\nsoft\\Nlines",
        "{\\q3\\k25\\t(0,2000,\\1a&H70&)}karaoke words with fixed boundaries",
        "{\\rBox\\t(0,1200,\\3c&HFF0090&)\\t(300,2000,\\alpha&H60&)}opaque box multiple lines\\Nand text",
        "{\\pos(60,80)\\move(30,50,200,110)\\fad(200,400)\\t(0,2000,\\c&HFF0000&)}position and fade",
        "{\\clip(10,10,280,160)\\t(0,2000,\\clip(20,30,240,130)\\alpha&H80&)}moving rectangular clip",
        "{\\iclip(m 60 40 l 200 40 200 130 60 130)\\t(0,2000,\\alpha&H90&)}inverse vector clip",
        "{\\org(70,70)\\fr15\\blur1.5\\t(0,2000,\\c&H80FF10&)}static geometry",
        "{\\p1\\pos(80,90)\\t(0,2000,\\c&HFF0080&\\3a&H80&)}m 0 0 l 30 0 30 15 0 15",
        "{\\t(0,2000,\\alpha&H90&)} \\N \\N ",
        "{\\pos(160,90)\\org(100,75)\\t(0,2000,\\blur2.5\\be1.25\\xshad-3.5\\yshad2\\frx15\\fry-10\\frz30\\fax.1\\fay-.05)}raster changes only\\Nmultiple words",
        "{\\p1\\pos(160,90)\\t(0,2000,\\blur1.5\\shad3\\frz70\\fax.2)}m 0 0 l 40 0 40 20 0 20",
        // Layout-changing, compaction and mixed-style fallback controls.
        "{\\t(0,2000,\\fs32\\alpha&H80&)}changing font metrics",
        "{\\t(0,2000,\\bord6)}changing borders",
        "{\\t(0,2000,\\alpha&H90&)}one{\\c&HFF0080&}two",
        "{\\t(0,2000,\\alpha&H90&)}<b>HTML</b> text",
        "{\\t(0,2000,\\frz50)}layout-independent rotation"
    };
    unsigned __int64 hits = 0, fallbacks = 0, updates = 0;
    const RECT rect = {0,0,320,180}; const SIZE size = {320,180};
    for (const char* text : cases)
    {
        CStringA script = CStringA(header) + "Dialogue: 0,0:00:00.00,0:00:03.00,Default,,0,0,0,," + text + "\n";
        CCritSec lock;
        CRenderedTextSubtitle cached(&lock), reference(&lock);
        reference.SetPreparedLayoutCacheLimit(0);
        reference.SetReconstructionCacheLimit(0);
        Require(cached.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"prepared") &&
            reference.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"legacy"), "Cannot open layout contract");
        std::vector<CComPtr<IXySubRenderFrame>> retained, expected;
        const auto compare = [&](REFERENCE_TIME time) {
            CComPtr<IXySubRenderFrame> a,b;
            const HRESULT actual = cached.RenderEx(&a,MSP_RGBA,rect,rect,size,time,24);
            const HRESULT wanted = reference.RenderEx(&b,MSP_RGBA,rect,rect,size,time,24);
            if (FAILED(actual) || FAILED(wanted) || !EqualFrames(a,b))
            {
                fprintf(stderr,"prepared_layout_mismatch time_ms=%lld case=%s\n",time/10000,text);
                Require(false,"Prepared layout changed pixels");
            }
            retained.push_back(a); expected.push_back(b);
            for (size_t i=0; i<retained.size(); ++i)
                Require(EqualFrames(retained[i],expected[i]), "Prepared layout mutated retained frames");
        };
        for (int ms : {125,250,500,800,1250,1750,2100,2400}) compare(ms*10000LL);
        STSStyle style; cached.GetDefaultStyle(style); style.fontSize += 3;
        cached.SetDefaultStyle(style); reference.SetDefaultStyle(style);
        cached.SetForceDefaultStyle(true); reference.SetForceDefaultStyle(true);
        compare(24500000);
        cached.SetStr(0,L"{\\q2\\t(0,2000,\\alpha&H90&)}replacement source\\Nline",true);
        reference.SetStr(0,L"{\\q2\\t(0,2000,\\alpha&H90&)}replacement source\\Nline",true);
        compare(25000000); compare(25500000);
        compare(5000000); // Backward seek discards prior event plans.
        compare(35000000); // Exact expiry must release all retained plans.
        Require(cached.GetPreparedLayoutStats().bytes==0 && cached.GetPreparedLayoutStats().entries==0, "Expired layout retained");
        hits += cached.GetPreparedLayoutStats().hits;
        updates += cached.GetPreparedLayoutStats().updates;
        fallbacks += cached.GetPreparedLayoutStats().fallbacks;
        cached.SetPreparedLayoutCacheLimit(1);
        compare(9000000); compare(10000000);
        Require(cached.GetPreparedLayoutStats().bytes<=1, "Layout admission exceeded byte budget");
    }
    Require(hits>30 && updates>30 && fallbacks>0,"Layout contract did not exercise prepared replay and fallback");
    printf("prepared_layout_contract=passed (uniform text/drawings, wrap modes, opacity/color, clips, karaoke, box, geometry fallback, source/style edits, seek/expiry, retained frames, byte budget)\n");
    return 0;
}


class MaintenanceRenderer : public CRenderedTextSubtitle
{
public:
    MaintenanceRenderer(CCritSec* lock) : CRenderedTextSubtitle(lock) {}
    void ChangeOrder(int i, int layer, int order) { m_entries[i].layer=layer; m_entries[i].readorder=order; }
};

static int MaintenanceContract()
{
    const char script[] =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 160\nPlayResY: 90\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,{\\pos(70,50)\\t(0,1900,\\alpha&H80&)}first\n"
        "Dialogue: 1,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\pos(70,50)}second\n";
    CCritSec lock;
    MaintenanceRenderer cached(&lock), reference(&lock);
    reference.SetMaintenanceCacheEnabled(false);
    Require(cached.Open((BYTE*)script,sizeof(script)-1,DEFAULT_CHARSET,L"maintenance") &&
        reference.Open((BYTE*)script,sizeof(script)-1,DEFAULT_CHARSET,L"reference"),"Cannot open maintenance fixture");
    const RECT rect={0,0,160,90}; const SIZE size={160,90};
    const auto compare=[&](int ms,double fps=24) {
        CComPtr<IXySubRenderFrame> a,b;
        Require(SUCCEEDED(cached.RenderEx(&a,MSP_RGBA,rect,rect,size,ms*10000LL,fps)) &&
            SUCCEEDED(reference.RenderEx(&b,MSP_RGBA,rect,rect,size,ms*10000LL,fps)) && EqualFrames(a,b),
            "Ordering/deadline cache changed pixels");
    };
    for(int ms : {1,50,100,150,500,1000}) compare(ms);
    auto a=cached.GetMaintenanceStats(), b=reference.GetMaintenanceStats();
    Require(a.orderHits>=5 && a.orderBuilds==1 && a.parsedScans<b.parsedScans && a.subtitleScans<b.subtitleScans,
        "Maintenance fast paths were not exercised");
    cached.ChangeOrder(0,2,40); reference.ChangeOrder(0,2,40); compare(1100);
    cached.ChangeOrder(0,1,1); reference.ChangeOrder(0,1,1); compare(1150); // Equal keys preserve legacy qsort input.
    const auto add=[&](MaintenanceRenderer& renderer) {
        renderer.Add(L"{\\pos(70,50)\\t(0,400,\\c&H00FF00&)}inserted",true,12000000,17000000,_T("Default"),_T(""),_T(""),CRect(),3,0);
    };
    add(cached); add(reference); // Add does not call OnChanged: same-segment streaming insertion.
    for(int ms : {1250,1300,1699,1700,1800,1999,2000,2500,3000}) compare(ms);
    Require(cached.GetParsedTagCacheStats().entries==0 && cached.GetReconstructionCacheCount()==0,"Deadline expiry retained ended events");
    compare(800); compare(900,30); // Reverse seek and FPS changes.
    cached.Sort(); reference.Sort(); compare(1000);
    cached.ConvertToFrameBased(24); reference.ConvertToFrameBased(24);
    compare(1000,24); compare(1100,30); compare(1200,15); compare(3100,24);
    printf("maintenance_contract=passed (reuse, ordering edits/ties, streaming append, deadlines, seek, invalidation, frame-based FPS)\n");
    return 0;
}

static int ClipperRetentionContract()
{
    CCritSec lock; CRenderedTextSubtitle context(&lock);
    auto cache=CacheManager::GetClipperAlphaMaskMruCache();
    cache->RemoveAll(true);
    auto clip=boost::make_shared<CClipper>(L"m 0 0 l 50 0 50 40 0 40",CSize(160,90),1.,1.,false);
    boost::weak_ptr<GrayImage2> weak;
    {
        CClipperPaintMachine machine(clip);
        SharedPtrGrayImage2 mask; machine.Paint(&mask); weak=mask;
        Require(mask && cache->GetQueryCount()==1,"Initial mask lookup was not recorded");
        const GrayImage2* first=mask.get(); mask.reset();
        machine.Paint(&mask);
        Require(mask.get()==first && cache->GetQueryCount()==1,"Repeated layer paint repeated the mask lookup");
        mask.reset(); cache->RemoveAll();
        Require(weak.expired(),"Clipper machine retained an evicted mask");
        machine.Paint(&mask);
        Require(mask && cache->GetQueryCount()==2,"Evicted mask was not rebuilt");
    }
    cache->RemoveAll(); Require(weak.expired(),"Mask ownership leaked");
    printf("clipper_retention_contract=passed (one lookup, weak ownership, eviction, regeneration)\n");
    return 0;
}

static int LazyInternContract()
{
    XyFwStringW::GetCacher()->RemoveAll(true);
    XyFwStringW a(CStringW(L"lazy owned key")), b(CStringW(L"lazy owned key"));
    Require(a.GetId()==b.GetId() && XyFwStringW::GetCacher()->GetQueryCount()==2 &&
        XyFwStringW::GetCacher()->GetCacheHitCount()==1,"Interning performed duplicate miss probes");
    EnhancedXyMru<int,int> cache(1); int copies=0; bool added;
    POSITION pos=cache.FindOrAddOwned(7,11,[&]() { ++copies; return 7; },&added);
    Require(pos && added && copies==1 && cache.GetAt(pos)==11,"Lazy factory failed to insert");
    pos=cache.FindOrAddOwned(7,99,[&]() { ++copies; return 7; },&added);
    Require(pos && !added && copies==1 && cache.GetAt(pos)==11,"Lazy factory ran on a hit or replaced its value");
    cache.SetMaxItemNum(0);
    Require(!cache.FindOrAddOwned(8,22,[&]() { ++copies; return 8; },&added) && added && copies==1,
        "Disabled interning retained or copied a probe");
    cache.SetMaxItemNum(1);
    pos=cache.FindOrAddOwned(8,22,[]() { return 8; },&added);
    Require(pos && added && cache.GetAt(pos)==22,"Lazy cache failed after capacity change");
    printf("lazy_intern_contract=passed (single probe, copy on miss only, ownership, zero/runtime capacity)\n");
    return 0;
}


static int LookaheadContract()
{
    const char script[]=
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,18,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,1,1,2,10,10,10,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\t(0,1500,\\alpha&H90&\\blur2)}animated words\\Nsecond line\n"
        "Dialogue: 1,0:00:01.30,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(100,80)\\t(0,1700,\\frz50)}m 0 0 l 40 0 40 20 0 20\n"
        "Dialogue: 0,0:00:01.80,0:00:04.00,Default,,0,0,0,,{\\t(0,1000,\\fs32)}metric-changing fallback\n"
        "Dialogue: 2,0:00:01.80,0:00:04.00,Default,,0,0,0,,static source with fresh style\n";
    CCritSec lock; CRenderedTextSubtitle cached(&lock),reference(&lock);
    reference.SetLookaheadBudget(0);
    Require(cached.Open((BYTE*)script,sizeof(script)-1,DEFAULT_CHARSET,L"lookahead") &&
        reference.Open((BYTE*)script,sizeof(script)-1,DEFAULT_CHARSET,L"reference"),"Cannot open lookahead fixture");
    const RECT rect={0,0,320,180};const SIZE size={320,180};
    std::vector<CComPtr<IXySubRenderFrame>> retained,wanted;
    const auto compare=[&](int ms) {
        CComPtr<IXySubRenderFrame> a,b;
        Require(SUCCEEDED(cached.RenderEx(&a,MSP_RGBA,rect,rect,size,ms*10000LL,24)) &&
            SUCCEEDED(reference.RenderEx(&b,MSP_RGBA,rect,rect,size,ms*10000LL,24)) && EqualFrames(a,b),"Lookahead changed exact frames");
        retained.push_back(a);wanted.push_back(b);
        for(size_t i=0;i<retained.size();++i) Require(EqualFrames(retained[i],wanted[i]),"Lookahead mutated an earlier frame");
    };
    compare(100);compare(400);compare(800);
    Require(cached.GetLookaheadStats().attempted>=3 && cached.GetLookaheadStats().retained>=2,
        "Lookahead failed to prepare guarded events");
    STSStyle style; cached.GetDefaultStyle(style); style.fontSize+=3; style.colors[0]=0x00a0f010;
    cached.SetDefaultStyle(style);reference.SetDefaultStyle(style);cached.SetForceDefaultStyle(true);reference.SetForceDefaultStyle(true);
    for(int ms : {999,1000,1001,1100,1299,1300,1400}) compare(ms);
    cached.SetStr(2,L"{\\t(0,1500,\\c&HFF9020&)}edited future words",true);
    reference.SetStr(2,L"{\\t(0,1500,\\c&HFF9020&)}edited future words",true);
    for(auto renderer : {&cached,&reference}) renderer->Add(L"{\\pos(200,90)\\t(0,400,\\alpha&H80&)}inserted",true,16000000,22000000,_T("Default"),_T(""),_T(""),CRect(),3,0);
    for(int ms : {1450,1599,1600,1700,1799,1800,2000,2200,2999,3000,3500,4000}) compare(ms);
    Require(cached.GetParsedTagCacheStats().entries==0 && cached.GetPreparedLayoutStats().entries==0 && cached.GetReconstructionCacheCount()==0,
        "Lookahead retained ended event plans");
    compare(200);compare(1001);compare(2000); // Backward seek re-prepares from current source/style.
    // Existing static cache entries survive reverse seeks. Preparation must
    // not discard them and silently change legacy behavior under style edits.
    compare(900);
    style.fontSize+=5; style.colors[0]=0x004080f0;
    cached.SetDefaultStyle(style);reference.SetDefaultStyle(style);
    compare(1800);compare(2000);
    cached.SetLookaheadBudget(0);compare(2200);compare(4000);
    printf("lookahead_contract=passed (timed blank-frame preparation, guarded text/polygons, style/source edits before activation, static/metric fallback, insertion, seek, expiry, retained frames)\n");
    return 0;
}



static int DrawReuseContract()
{
    const CStringA header="[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00102030,&H70000000,0,0,0,0,100,100,0,0,1,2,1,7,0,0,0,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    const char* cases[]={
        "{\\pos(22.25,40.125)\\p1}m 0 0 l 40 0 40 20 0 20",
        "{\\pos(40,40)\\clip(15,20,80,70)\\1a&HFE&\\bord0}single",
        "{\\pos(80,40)\\p2\\pbo-2\\t(1000,1000,\\4a&HFF&\\p2\\t(1900,1900,\\4a&H00&))}m 0 0 b 15 0 30 20 50 25",
        "{\\pos(20,40)\\t(0,2000,\\frz30\\blur2\\1c&H80FF00&)}appearance",
        "{\\pos(80,50)\\org(40,20)\\frz15\\fax.2}origin",
        "{\\pos(40,40)\\bord3\\shad2}layers",
        "{\\pos(20,40)\\k30}karaoke",
        "{\\move(20,40,100,70,0,2000)}moving",
        "{\\pos(40,40)\\fad(200,400)}fading",
        "{\\pos(40,40)\\iclip(20,30,60,60)}inverse",
        "{\\pos(40,40)\\clip(m 20 20 l 80 20 80 80 20 80)}vector"
    };
    const RECT rect={0,0,320,180}; const SIZE size={320,180};
    unsigned __int64 hits=0;
    const int formats[]={MSP_RGBA,MSP_RGBA_F,MSP_AYUV,MSP_AYUV_PLANAR,MSP_XY_AUYV};
    for (const char* text:cases)
    {
        CStringA script=header+"Dialogue: 0,0:00:00.00,0:00:03.00,Default,,0,0,0,,"+text+"\n";
        // Cross the additive-selection threshold while retaining the first event.
        for(int i=0;i<130;++i) script+="Dialogue: 1,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(40,40)\\p1\\bord0\\shad0\\1a&H80&}m 0 0 l 10 0 10 10 0 10\n";
        CCritSec lock;CRenderedTextSubtitle cached(&lock),reference(&lock);
        reference.SetDrawReuseCacheLimit(0);
        Require(cached.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"draw-reuse") && reference.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"draw-reference"),"Cannot open draw reuse contract");
        std::vector<CComPtr<IXySubRenderFrame>> retained,expected;
        int previousFormat=-1;
        const auto compare=[&](int ms,int format) {
            // Explicitly flush between format negotiations: the existing global
            // bitmap key does not describe a pixel format.
            if (previousFormat!=format) { CacheManager::GetBitmapMruCache()->RemoveAll(); previousFormat=format; }
            CComPtr<IXySubRenderFrame> a,b;
            Require(SUCCEEDED(cached.RenderEx(&a,format,rect,rect,size,ms*10000LL,24)) && SUCCEEDED(reference.RenderEx(&b,format,rect,rect,size,ms*10000LL,24)) && EqualFrames(a,b),"Draw reuse changed exact output");
            retained.push_back(a);expected.push_back(b);
            for(size_t i=0;i<retained.size();++i) Require(EqualFrames(retained[i],expected[i]),"Draw reuse mutated retained frames");
        };
        for(int format:formats) for(int ms:{100,200,500,999,1000,1001,1200,1500,1899,1900,1901,1999,2000,2100,2500}) compare(ms,format);
        for(int level=0;level<SubpixelPositionControler::MAX_COUNT;++level)
        {
            SubpixelPositionControler::GetGlobalControler().SetSubpixelLevel(static_cast<SubpixelPositionControler::SUBPIXEL_LEVEL>(level));
            compare(2520,MSP_RGBA);compare(2530,MSP_RGBA);
        }
        SubpixelPositionControler::GetGlobalControler().SetSubpixelLevel(SubpixelPositionControler::EIGHT_X_EIGHT);
        hits+=cached.GetDrawReuseStats().hits;
        cached.SetDrawReuseCacheLimit(1);compare(2600,MSP_RGBA);compare(2650,MSP_RGBA);
        Require(cached.GetDrawReuseStats().bytes==0,"Disabled draw reuse retained metadata");
        cached.SetDrawReuseCacheLimit(32*1024*1024);
        cached.SetStr(0,L"{\\pos(90,70)\\p1\\1c&HFF8000&}m 0 0 l 20 0 20 15 0 15",true);
        reference.SetStr(0,L"{\\pos(90,70)\\p1\\1c&HFF8000&}m 0 0 l 20 0 20 15 0 15",true);
        compare(2700,MSP_RGBA);compare(2750,MSP_RGBA);compare(500,MSP_RGBA);compare(3100,MSP_RGBA);
        Require(cached.GetDrawReuseStats().bytes==0,"Expired draw metadata retained");
    }
    Require(hits>100,"Draw reuse contract did not exercise descriptor hits");
    // A retained layer wrapper must not pin an image after the normal MRUs evict it.
    CCritSec ownerLock; CRenderedTextSubtitle owner(&ownerLock);
    SharedPtrCWord word=boost::make_shared<CPolygon>(FwSTSStyle(),L"m 0 0 l 20 0 20 20 0 20",0,0,0,1.,1.,0);
    SharedPtrOverlayPaintMachine body;CWordPaintMachine::CreatePaintMachines(word,CPoint(),CPoint(),CPoint(),CPoint(),NULL,NULL,&body);
    boost::weak_ptr<Overlay> weak;
    {SharedPtrOverlay image;body->Paint(&image);Require(image.get()!=NULL,"Weak owner did not paint");weak=image;body->EnableWeakRetention();}
    CacheManager::GetOverlayMruCache()->RemoveAll();CacheManager::GetSubpixelVarianceCache()->RemoveAll();CacheManager::GetOverlayNoBlurMruCache()->RemoveAll();
    Require(weak.expired(),"Retained draw wrapper pins raster images outside MRUs");
    {
        FrameOverlayPin frame; frame.Set(body);
        {SharedPtrOverlay image;body->Paint(&image);Require(image.get()!=NULL,"Evicted weak overlay did not repaint");weak=image;}
        CacheManager::GetOverlayMruCache()->RemoveAll();CacheManager::GetOverlayNoBlurMruCache()->RemoveAll();
        Require(!weak.expired(),"Current frame lost its raster pin");
        FrameOverlayPin copied(frame);frame.Set(SharedPtrOverlayPaintMachine());
        Require(!weak.expired(),"Copied frame pin lost its raster owner");
    }
    Require(weak.expired(),"Frame-scoped raster pin escaped composition");
    printf("draw_reuse_contract=passed (exact guards, all output formats/subpixel levels, moving/appearance/clip/karaoke fallback, 128-item blend threshold, source edits, expiry/seek, budget, retained frames, weak raster ownership)\n");
    return 0;
}

static int ScalarParameterContract()
{
    const CStringA header="[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    CStringA script=header;
    for(int i=0;i<1200;++i) {CStringA line;line.Format("Dialogue: 0,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\an7\\bord0\\blur0\\shad.01\\1a&HFE&\\p1\\frz%d\\1c&H%06X&\\pos(%d,%d)}m 0 0 l 4 0 4 4 0 4\n",i-600,i*137,(i*11)%300,(i*7)%160);script+=line;}
    script+="Dialogue: 1,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\pos(70,80)\\fs\\alpha\\c\\b\\rDefault\\fs(20)\\t(1000,1000,\\4a&H00&\\p1\\t(1900,1900,\\4a&HFF&))}reset\n";
    for(const char* number:{"-0","+1","65535","65536","1e-1","0.125"})
    {
        CStringA line;line.Format("Dialogue: 2,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\an7\\bord0\\blur0\\shad0\\p1\\frz%s\\pos(90,90)}m 0 0 l 3 0 3 3 0 3\n",number);script+=line;
    }
    CCritSec lock; CRenderedTextSubtitle cached(&lock),reference(&lock);
    reference.SetScalarParameterCacheEnabled(false);
    Require(cached.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"scalar-cache") && reference.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"scalar-original"),"Cannot open scalar contract");
    const RECT rect={0,0,320,180};const SIZE size={320,180};
    std::vector<CComPtr<IXySubRenderFrame>> retained,expected;
    const auto compare=[&](int ms) {
        CComPtr<IXySubRenderFrame>a,b;
        CacheManager::GetAssTagListMruCache()->RemoveAll();
        Require(SUCCEEDED(cached.RenderEx(&a,MSP_RGBA,rect,rect,size,ms*10000LL,24)),"Cached scalar render failed");
        CacheManager::GetAssTagListMruCache()->RemoveAll();
        Require(SUCCEEDED(reference.RenderEx(&b,MSP_RGBA,rect,rect,size,ms*10000LL,24)) && EqualFrames(a,b),"Scalar cache changed exact rendering");
        retained.push_back(a);expected.push_back(b);
        for(size_t i=0;i<retained.size();++i) Require(EqualFrames(retained[i],expected[i]),"Scalar cache mutated retained frames");
        const auto stats=cached.GetScalarParameterCacheStats();
        Require(stats.entries<=512 && stats.bytes<=512*1024,"Scalar cache exceeded its bounded storage");
    };
    compare(100);compare(500);compare(1000);compare(1900);compare(2100);
    auto stats=cached.GetScalarParameterCacheStats();
    Require(stats.hits>1000 && stats.misses>512,"Scalar contract did not exercise shared values and eviction/collisions");
    Require(reference.GetScalarParameterCacheStats().entries==0,"Disabled scalar cache retained values");
    STSStyle style;cached.GetDefaultStyle(style);style.fontSize=27;style.colors[0]=RGB(70,180,220);
    cached.SetDefaultStyle(style);reference.SetDefaultStyle(style);cached.SetForceDefaultStyle(true);reference.SetForceDefaultStyle(true);
    compare(2200);
    struct EnvironmentScope
    {
        CStringA locale;
        unsigned control;
        int threadLocale;
        EnvironmentScope() : locale(setlocale(LC_ALL,NULL)),control(0),threadLocale(_configthreadlocale(_ENABLE_PER_THREAD_LOCALE)) { _controlfp_s(&control,0,0); }
        ~EnvironmentScope() {
            unsigned ignored;_controlfp_s(&ignored,control,_MCW_RC);
#ifdef _M_IX86
            _controlfp_s(&ignored,control,_MCW_PC);
#endif
            setlocale(LC_ALL,locale);_configthreadlocale(threadLocale);
        }
    } environment;
    const auto fresh=[&]() {cached.Deinit();reference.Deinit();compare(2250);};
    Require(setlocale(LC_ALL,"C")!=NULL,"Cannot select C locale");fresh();
    if(setlocale(LC_NUMERIC,"German_Germany.1252")) {fresh();Require(setlocale(LC_ALL,"C")!=NULL,"Cannot restore C locale");fresh();}
    unsigned ignored;Require(!_controlfp_s(&ignored,_RC_UP,_MCW_RC),"Cannot select rounding control");fresh();
    Require(!_controlfp_s(&ignored,environment.control,_MCW_RC),"Cannot restore rounding control");fresh();
#ifdef _M_IX86
    Require(!_controlfp_s(&ignored,_PC_24,_MCW_PC),"Cannot select 24-bit precision");fresh();
    Require(!_controlfp_s(&ignored,_PC_53,_MCW_PC),"Cannot select 53-bit precision");fresh();
    Require(!_controlfp_s(&ignored,environment.control,_MCW_PC),"Cannot restore precision control");fresh();
#endif
    cached.SetScalarParameterCacheEnabled(false);cached.Deinit();reference.Deinit();compare(2300);
    Require(cached.GetScalarParameterCacheStats().entries==0,"Scalar disable retained storage");
    cached.SetScalarParameterCacheEnabled(true);cached.Deinit();compare(2350);compare(500);compare(3100);
    printf("scalar_parameter_contract=passed (shared values, 1200 unique blocks, collisions/eviction, empty/reset/parenthesized/malformed/nested tags, styles, locale/rounding, disable, seek/expiry, retained frames)\n");
    return 0;
}

static int PaintBundleContract()
{
    CCritSec lock; CRenderedTextSubtitle renderer(&lock);
    for(int flags=1;flags<8;++flags)
    {
        boost::weak_ptr<CWord> lifetime;
        SharedPtrOverlayPaintMachine body,outline,shadow;
        {
            STSStyle style;style.outlineWidthX=style.outlineWidthY=2;
            SharedPtrCWord word=boost::make_shared<CPolygon>(FwSTSStyle(style),L"m 0 0 l 20 0 20 20 0 20",0,0,0,1.,1.,0);
            lifetime=word;
            CWordPaintMachine::CreatePaintMachines(word,CPoint(2,7),CPoint(3,1),CPoint(0,0),CPoint(40,50),
                flags&1 ? &shadow : NULL,flags&2 ? &outline : NULL,flags&4 ? &body : NULL);
        }
        Require(!lifetime.expired(),"Paint bundle lost its word");
        const SharedPtrOverlayPaintMachine layers[]={body,outline,shadow};
        for(const auto& a:layers) if(a) for(const auto& b:layers) if(b)
            Require(!a.owner_before(b) && !b.owner_before(a),"Layer machines do not share their allocation owner");
        for(auto layer:layers) if(layer) { SharedPtrOverlay overlay;layer->Paint(&overlay);Require(overlay.get()!=NULL,"Paint bundle failed after factory owner released"); }
        body.reset();outline.reset();shadow.reset();
        Require(!lifetime.expired(),"Copied layer aliases lost their word");
        // The copied array owns the allocation until the iteration ends.
    }
    boost::weak_ptr<CWord> released;
    {
        SharedPtrCWord word=boost::make_shared<CPolygon>(FwSTSStyle(),L"m 0 0 l 10 0 10 10",0,0,0,1.,1.,0);released=word;
        SharedPtrOverlayPaintMachine body;
        CWordPaintMachine::CreatePaintMachines(word,CPoint(),CPoint(),CPoint(),CPoint(),NULL,NULL,&body);
        word.reset();body.reset();
    }
    Require(released.expired(),"Paint bundle created an ownership cycle");
    printf("paint_bundle_contract=passed (all requested layer combinations, shared allocation owner, alias lifetime, post-factory painting, release without cycles)\n");
    return 0;
}


class LegacyPolygonScanner : public CPolygon
{
    bool GetPOINT(const WCHAR*& cursor,POINT& point)
    {
        WCHAR* end;const WCHAR* first=cursor;
        point.x=wcstod(cursor,&end)*64;cursor=end;if(end==first) return false;
        first=cursor;point.y=wcstod(cursor,&end)*64;cursor=end;return end>first;
    }
    bool ParseLegacy(const CStringW& input)
    {
        // Test-only transcription of the frozen baseline scanner, not a new
        // ASS parser. The production scanner uses the same command switch.
    CPoint p;
    int j, lastsplinestart = -1, firstmoveto = -1, lastmoveto = -1;
    CStringW str = input;
    str.Replace(L"m", L"*m");
    str.Replace(L"n", L"*n");
    str.Replace(L"l", L"*l");
    str.Replace(L"b", L"*b");
    str.Replace(L"s", L"*s");
    str.Replace(L"p", L"*p");
    str.Replace(L"c", L"*c");
    int k = 0;
    for(CStringW s = str.Tokenize(L"*", k); !s.IsEmpty(); s = str.Tokenize(L"*", k))
    {
        WCHAR c = s[0];
        s.TrimLeft(L"mnlbspc ");
        const WCHAR* cursor = s.GetString();
        switch(c)
        {
        case 'm':
            lastmoveto = m_pathTypesOrg.GetCount();
            if(firstmoveto == -1)
                firstmoveto = lastmoveto;
            while(GetPOINT(cursor, p)) {
                m_pathTypesOrg.Add(PT_MOVETO);
                m_pathPointsOrg.Add(p);
            }
            break;
        case 'n':
            while(GetPOINT(cursor, p)) {
                m_pathTypesOrg.Add(PT_MOVETONC);
                m_pathPointsOrg.Add(p);
            }
            break;
        case 'l':
            if (m_pathPointsOrg.GetCount() < 1) {
                break;
            }
            while(GetPOINT(cursor, p)) {
                m_pathTypesOrg.Add(PT_LINETO);
                m_pathPointsOrg.Add(p);
            }
            break;
        case 'b':
            j = m_pathTypesOrg.GetCount();
            if (j < 1) {
                break;
            }
            while(GetPOINT(cursor, p)) {
                m_pathTypesOrg.Add(PT_BEZIERTO);
                m_pathPointsOrg.Add(p);
                j++;
            }
            j = m_pathTypesOrg.GetCount() - ((m_pathTypesOrg.GetCount()-j)%3);
            m_pathTypesOrg.SetCount(j);
            m_pathPointsOrg.SetCount(j);
            break;
        case 's':
            if (m_pathPointsOrg.GetCount() < 1) {
                break;
            }
            {
                j = lastsplinestart = m_pathTypesOrg.GetCount();
                int i = 3;
                while(i-- && GetPOINT(cursor, p)) {
                    m_pathTypesOrg.Add(PT_BSPLINETO);
                    m_pathPointsOrg.Add(p);
                    j++;
                }
                if(m_pathTypesOrg.GetCount()-lastsplinestart < 3) {
                    m_pathTypesOrg.SetCount(lastsplinestart);
                    m_pathPointsOrg.SetCount(lastsplinestart);
                    lastsplinestart = -1;
                }
            }
            // no break here
        case 'p':
            if (m_pathPointsOrg.GetCount() < 3) {
                break;
            }
            while(GetPOINT(cursor, p)) {
                m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
                m_pathPointsOrg.Add(p);
            }
            break;
        case 'c':
            if(lastsplinestart > 0)
            {
                m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
                m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
                m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
                p = m_pathPointsOrg[lastsplinestart-1]; // we need p for temp storage, because operator [] will return a reference to CPoint and Add() may reallocate its internal buffer (this is true for MFC 7.0 but not for 6.0, hehe)
                m_pathPointsOrg.Add(p);
                p = m_pathPointsOrg[lastsplinestart];
                m_pathPointsOrg.Add(p);
                p = m_pathPointsOrg[lastsplinestart+1];
                m_pathPointsOrg.Add(p);
                lastsplinestart = -1;
            }
            break;
        default:
            break;
        }
    }
    if(lastmoveto == -1 || firstmoveto > 0)
    {
        m_pathTypesOrg.RemoveAll();
        m_pathPointsOrg.RemoveAll();
        return(false);
    }
    for(size_t i=0;i<m_pathPointsOrg.GetCount();++i) {
        m_pathPointsOrg[i].x=int(m_scalex*m_pathPointsOrg[i].x);
        m_pathPointsOrg[i].y=int(m_scaley*m_pathPointsOrg[i].y);
    }
    return true;
}

public:
    LegacyPolygonScanner(const CStringW& text,double x,double y) : CPolygon(FwSTSStyle(),L"",0,0,0,x,y,0)
    { ParseLegacy(text); }
    bool ReadPath(PathData* path) { return CreatePath(path); }
};

static int PolygonScannerContract()
{
    unsigned seed=0x341527u;
    const auto random=[&]() { seed=1664525u*seed+1013904223u;return seed; };
    const wchar_t* numbers[]={L"0",L"-1.25",L"+2.5",L"1e-2",L"0x10",L".5",L"-0",L"3E+1",L"0x1p+2",L"bad",L"1,5"};
    const wchar_t* commands[]={L" m ",L"n",L" l\t",L"b ",L"s ",L" p ",L"c",L"*",L"M",L"??",L"  "};
    for(int test=0;test<2000;++test)
    {
        CStringW text= test%3 ? L"m 0 0 l 12.5 0 12 9 " : L"prefix*";
        const int chunks=test<1990 ? 1+random()%16 : 1000;
        for(int i=0;i<chunks;++i)
        {
            text+=commands[random()%_countof(commands)];
            for(unsigned n=random()%9;n;--n) {text+=numbers[random()%_countof(numbers)];text+=L" ";}
        }
        CacheManager::GetPathDataMruCache()->RemoveAll();
        const double x=test%2 ? 1.25 : 0.,y=test%3 ? .75 : -1.;
        GeometryPolygon actual(FwSTSStyle(),text,x,y,0);
        LegacyPolygonScanner legacy(text,x,y);PathData a,b;
        const bool haveA=actual.ReadPath(&a),haveB=legacy.ReadPath(&b);
        Require(haveA==haveB && (!haveA || a==b),"Polygon span scanner changed legacy vertices/types");
    }
    printf("polygon_scanner_contract=passed (2000 baseline differentials, command splitting, malformed/hex/exponents, scales, long spills)\n");
    return 0;
}

static int ConstantDrawingContract()
{
    const char header[]="[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    const char* cases[]={
        "{\\pos(80,80)\\t(50,50,\\4a&H80&\\p1\\t(970,970,\\4a&H00&)\\clip(0,0,320,90)}m 0 0 l 15 0 15 15 0 15",
        "{\\pos(80,80)\\t(0,800,\\p2\\pbo3\\alpha&H90&)}m 0 0 l 30 0 30 20 0 20",
        "{\\p1\\t(20,20,\\p0\\pbo-2)}drawing mode becomes text",
        "{\\t(40,40,\\p1\\pbo2)\\t(900,900,\\p2\\pbo-1)}m 0 0 l 20 0 20 20 0 20",
        "{\\p1\\t(0,800,\\fscx150)}m 0 0 l 20 0 20 20 0 20"};
    unsigned __int64 hits=0;
    for(const auto text:cases)
    {
        CStringA script=CStringA(header)+"Dialogue: 0,0:00:01.00,0:00:04.00,Default,,0,0,0,,"+text+"\n";
        CCritSec lock;CRenderedTextSubtitle actual(&lock),legacy(&lock);
        legacy.SetPreparedLayoutCacheLimit(0);legacy.SetReconstructionCacheLimit(0);legacy.SetLookaheadBudget(0);
        Require(actual.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"constant-p") && legacy.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"reference"),"Cannot open constant drawing fixture");
        const RECT rect={0,0,320,180};const SIZE size={320,180};
        for(int ms : {100,500,999,1000,1001,1019,1020,1039,1040,1049,1050,1051,1800,1899,1900,1969,1970,1971,2200,3000,4100,500,1050})
        {
            CComPtr<IXySubRenderFrame> a,b;
            Require(SUCCEEDED(actual.RenderEx(&a,MSP_RGBA,rect,rect,size,ms*10000LL,24)) && SUCCEEDED(legacy.RenderEx(&b,MSP_RGBA,rect,rect,size,ms*10000LL,24)) && EqualFrames(a,b),"Constant drawing/baseline preparation changed pixels");
        }
        hits+=actual.GetPreparedLayoutStats().updates;
    }
    Require(hits>10,"Constant drawing mode replay was not exercised");
    printf("constant_drawing_contract=passed (nested/malformed transforms, steps +/-1ms, p0/p1/p2, baseline, fallback, seek)\n");return 0;
}

static int StaticDrawingContract()
{
    const char script[]="[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\p1\\an7\\pos(30,40)\\fscx130\\pbo2}m 0 0 l 30 0 30 20 0 20\n";
    CCritSec lock;CRenderedTextSubtitle actual(&lock),legacy(&lock);legacy.SetLookaheadBudget(0);
    Require(actual.Open((BYTE*)script,sizeof(script)-1,DEFAULT_CHARSET,L"static") && legacy.Open((BYTE*)script,sizeof(script)-1,DEFAULT_CHARSET,L"reference"),"Cannot open static drawing fixture");
    const RECT rect={0,0,320,180};const SIZE size={320,180};
    const auto compare=[&](int ms) {
        CComPtr<IXySubRenderFrame>a,b;
        Require(SUCCEEDED(actual.RenderEx(&a,MSP_RGBA,rect,rect,size,ms*10000LL,24)) && SUCCEEDED(legacy.RenderEx(&b,MSP_RGBA,rect,rect,size,ms*10000LL,24)) && EqualFrames(a,b),"Staged static drawing changed exact output");
    };
    compare(100);compare(400);
    Require(actual.GetReconstructionCacheCount()>0 && actual.GetReconstructionCacheBytes()<512*1024,"Static preparation not retained or not bounded");
    STSStyle style;actual.GetDefaultStyle(style);style.fontScaleX=115;style.colors[0]=0x0080f0ff;
    actual.SetDefaultStyle(style);legacy.SetDefaultStyle(style);actual.SetForceDefaultStyle(true);legacy.SetForceDefaultStyle(true);
    actual.SetStr(0,L"{\\p2\\an7\\pos(50,40)\\pbo-3}m 0 0 l 80 0 80 40 0 40",true);
    legacy.SetStr(0,L"{\\p2\\an7\\pos(50,40)\\pbo-3}m 0 0 l 80 0 80 40 0 40",true);
    compare(1000);compare(1100);Require(actual.GetReconstructionCacheCount()==0,"Activated static retains its staged charge");
    compare(500);compare(1000);compare(3100);
    Require(actual.GetReconstructionCacheBytes()==0,"Expired static preparation retained bytes");
    printf("static_drawing_contract=passed (guarded staging, source/style/scale/baseline changes, activation accounting, seek, expiry)\n");return 0;
}

static int GroupingContract()
{
    const char header[] =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

    CStringA script(header);
    for (int i = 0; i < 160; ++i) {
        CStringA line;
        const int x = 8 + (i%14)*20, y = 8 + (i/14)*12;
        line.Format("Dialogue: %d,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\an7\\pos(%d,%d)\\bord1\\shad1\\1a&H60&\\1c&H%06X&\\clip(%d,%d,%d,%d)\\p1}m 0 0 l 24 0 24 16 0 16\n",
            i%3,x,y,(i*731)&0xffffff,x-2,y-2,x+20,y+14);
        script += line;
    }
    script += "Dialogue: 3,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\an7\\pos(100,0)\\bord0\\shad0\\p1}m 0 0 l 100 0 100 180 0 180\n";
    for (int i=0;i<12;++i) {
        CStringA line;
        line.Format("Dialogue: 3,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\an7\\pos(90,%d)\\bord0\\shad0\\p1}m 0 0 l %d 0 %d 7 0 7\n",i*14,125+i*3,125+i*3);
        script += line;
    }
    script += "Dialogue: 4,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\an7\\pos(-200,-200)\\p1}m 0 0 l 2 0 2 2 0 2\n";
    script += "Dialogue: 4,0:00:00.00,0:00:03.00,Default,,0,0,0,,{\\pos(160,90)\\iclip(120,60,200,120)}inverse clip\n";
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock), reference(&lock);
    Require(renderer.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"grouping") &&
        reference.Open((BYTE*)script.GetString(),script.GetLength(),DEFAULT_CHARSET,L"grouping-reference"), "Cannot open grouping contract");
    renderer.EnableDiagnostics(true);
    const RECT rect = {0,0,320,180}; const SIZE size = {320,180};
    CComPtr<IXySubRenderFrame> a,b;
    Require(SUCCEEDED(renderer.RenderEx(&a,MSP_RGBA,rect,rect,size,5000000,24)) &&
        SUCCEEDED(reference.RenderEx(&b,MSP_RGBA,rect,rect,size,5000000,24)), "Grouping contract render failed");
    Require(EqualFrames(a,b), "Diagnostics changed grouped pixels");
    const RenderDiagnostics& stats=renderer.GetDiagnostics();
    Require(stats.frames==1 && stats.inputItems>128 && stats.groups>0 && stats.drawItems>128, "Grouping diagnostics did not cover a large composite");
    Require(stats.memberReferences>=stats.uniqueMembers && stats.uniqueMembers>0, "Grouping membership counters disagree");
    Require(stats.drawVectorGrowths==0, "Final draw storage grew during append despite reservation");
    Require(stats.ticks[RenderDiagnostics::COMPOSITE]>0 && stats.ticks[RenderDiagnostics::GROUP]>0, "Grouping timers did not run");
    renderer.EnableDiagnostics(false);
    CComPtr<IXySubRenderFrame> c;
    Require(SUCCEEDED(renderer.RenderEx(&c,MSP_RGBA,rect,rect,size,5000000,24)) && EqualFrames(a,c) && EqualFrames(a,b), "Disabling diagnostics or retaining frames changed pixels");
    Require(renderer.GetDiagnostics().frames==0, "Disabled diagnostics continued collecting");
    printf("grouping_contract=passed (large tree, clips, overlaps, reservation, diagnostics, retained frames)\n");
    return 0;
}

static int CacheContract()
{
    const char script[] =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,{\\pos(160,100)\\t(0,1500,\\fs40\\blur1)}Cache{\\b1} test {\\unknown7}x{broken\n";
    CCritSec lock;
    CRenderedTextSubtitle cached(&lock), reference(&lock);
    Require(cached.Open((BYTE*)script, sizeof(script)-1, DEFAULT_CHARSET, L"cache-test") &&
        reference.Open((BYTE*)script, sizeof(script)-1, DEFAULT_CHARSET, L"reference-test"), "Cannot open test ASS");
    reference.SetParsedTagCacheLimit(0);
    const RECT rect = {0, 0, 320, 180};
    const SIZE size = {320, 180};
    const auto compare = [&](REFERENCE_TIME time) {
        CComPtr<IXySubRenderFrame> a, b;
        Require(SUCCEEDED(cached.RenderEx(&a, MSP_RGBA, rect, rect, size, time, 24)), "Cached test render failed");
        Require(SUCCEEDED(reference.RenderEx(&b, MSP_RGBA, rect, rect, size, time, 24)), "Reference test render failed");
        Require(EqualFrames(a, b), "Cache changed pixels");
        return a;
    };
    compare(50000);
    compare(5000000);
    compare(10000000);
    compare(15000000);
    Require(cached.GetParsedTagCacheStats().hits > 0 && cached.GetParsedTagCacheStats().entries > 0, "No active syntax retained");
    compare(21000000);
    Require(cached.GetParsedTagCacheStats().entries == 0, "Expired syntax retained");
    CComPtr<IXySubRenderFrame> before = compare(7500000); // backward seek
    cached.SetStr(0, L"{\\pos(160,100)\\t(0,1500,\\fs40\\blur1)}Changed", true);
    reference.SetStr(0, L"{\\pos(160,100)\\t(0,1500,\\fs40\\blur1)}Changed", true);
    CComPtr<IXySubRenderFrame> after = compare(7500000);
    Require(!EqualFrames(before, after), "Text mutation did not alter the test image");
    cached.SetParsedTagCacheLimit(1);
    compare(5000000);
    Require(cached.GetParsedTagCacheStats().bytes == 0 && cached.GetParsedTagCacheStats().entries == 0, "Syntax budget exceeded");
    cached.SetParsedTagCacheLimit(16 * 1024 * 1024);
    compare(5000000);
    cached.Sort();
    reference.Sort();
    Require(cached.GetParsedTagCacheStats().entries == 0, "Changed stream retained syntax");
    compare(10000000);
    printf("cache_contract=passed (animation, expiry, seek, edit, malformed tags, budget, invalidation)\n");
    return 0;
}

static CStringA FrameSha256(IXySubRenderFrame* frame, BCRYPT_ALG_HANDLE algorithm, int* count)
{
    BCRYPT_HASH_HANDLE hash = NULL;
    Require(BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) >= 0, "Cannot create SHA256");
    *count = 0;
    if (frame) {
        Require(SUCCEEDED(frame->GetBitmapCount(count)), "Cannot query bitmap count");
        for (int i = 0; i < *count; ++i) {
            POINT position;
            SIZE bitmapSize;
            LPCVOID pixels;
            int pitch;
            Require(SUCCEEDED(frame->GetBitmap(i, NULL, &position, &bitmapSize, &pixels, &pitch)), "Cannot get bitmap");
            Require(BCryptHashData(hash, reinterpret_cast<PUCHAR>(&position), sizeof(position), 0) >= 0, "Cannot hash position");
            Require(BCryptHashData(hash, reinterpret_cast<PUCHAR>(&bitmapSize), sizeof(bitmapSize), 0) >= 0, "Cannot hash size");
            for (int y = 0; y < bitmapSize.cy; ++y)
                Require(BCryptHashData(hash, (PUCHAR)pixels + y * pitch, bitmapSize.cx * 4, 0) >= 0, "Cannot hash bitmap");
        }
    }
    BYTE digest[32];
    Require(BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0, "Cannot finish SHA256");
    BCryptDestroyHash(hash);
    char text[65];
    for (int i = 0; i < 32; ++i) sprintf_s(text + i * 2, sizeof(text) - i * 2, "%02x", digest[i]);
    return CStringA(text);
}

static int ReconstructionCacheContract()
{
    const char header[] =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,7,0,0,0,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    const char shapes[] =
        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\pos(200,100)\\t(200,200,\\1c&H00FF00&)\\t(700,700,\\fs30)}Text\n"
        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\pos(30,30)\\p1\\t(200,200,\\1a&H80&\\t(700,700,\\4a&HFF&))}m 0 0 l 30 0 30 30 0 30\n"
        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\move(80,30,100,60)\\fad(100,100)\\p1\\t(200,800,\\fscx180)}m 0 0 l 30 0 30 30 0 30\n"
        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\pos(130,30)\\p1\\t(700,200,\\bord5)\\t(0,0,\\frz30)}m 0 0 l 30 0 30 30 0 30\n";
    CStringA script = CStringA(header) + shapes;
    CCritSec lock;
    CRenderedTextSubtitle cached(&lock), reference(&lock);
    Require(cached.Open((BYTE*)script.GetString(), script.GetLength(), DEFAULT_CHARSET, L"reuse-test") &&
        reference.Open((BYTE*)script.GetString(), script.GetLength(), DEFAULT_CHARSET, L"reuse-reference"), "Cannot open reconstruction fixture");
    reference.SetReconstructionCacheLimit(0);
    reference.SetParsedTagCacheLimit(0);
    const RECT rect = {0,0,320,180};
    const SIZE size = {320,180};
    const auto compare = [&](int ms) {
        CComPtr<IXySubRenderFrame> a,b;
        Require(SUCCEEDED(cached.RenderEx(&a,MSP_RGBA,rect,rect,size,REFERENCE_TIME(ms)*10000,24)) &&
            SUCCEEDED(reference.RenderEx(&b,MSP_RGBA,rect,rect,size,REFERENCE_TIME(ms)*10000,24)), "Reconstruction fixture render failed");
        Require(EqualFrames(a,b), "Reconstruction reuse changed pixels");
    };
    const int times[] = {0,50,100,199,200,201,500,699,700,701,799,800,801,999,1001,100,199,200,700};
    for (size_t i = 0; i < _countof(times); ++i) compare(times[i]);
    Require(cached.GetReconstructionCacheHits() > 0, "Reconstruction fixture did not exercise reuse");
    const CStringW changed(L"{\\pos(30,30)\\p1\\t(200,200,\\1a&H00&)}m 0 0 l 50 0 50 50 0 50");
    cached.SetStr(0,changed,true); reference.SetStr(0,changed,true);
    compare(700);
    cached.SetReconstructionCacheLimit(1);
    compare(100);
    compare(150);
    Require(cached.GetReconstructionCacheCount() <= 1, "Tiny reconstruction limit exceeded");
    Require(cached.GetReconstructionCacheBytes() <= 32*1024*1024, "Reconstruction byte budget exceeded");
    compare(1001);
    Require(cached.GetReconstructionCacheCount() == 0 && cached.GetReconstructionCacheBytes() == 0, "Expired reconstruction retained");
    cached.Sort(); reference.Sort(); compare(100);
    cached.Deinit();
    Require(cached.GetReconstructionCacheCount() == 0, "Deinit retained reconstruction state");
    printf("reconstruction_cache_contract=passed (step edges, nested transforms, continuous animation, movement, fade, seek, edit, bound, expiry, invalidation)\n");
    return 0;
}

static int Fixtures(const wchar_t* output)
{
    const char* fixtures[] = {
        "{\\an5\\pos(160,90)\\org(160,90)\\clip(0.25,0.5,319.75,179.25)\\fad(100,100)\\fe1\\fnArial\\b700\\i1\\u1\\s0\\fscx98.5\\fscy102.25\\fsp1.125\\fs+1.5\\frx1\\fry2\\frz3\\fax.01\\fay-.02\\bord2\\shad2\\xbord1.2\\ybord.5\\xshad-1.5\\yshad.25\\be.5\\blur.75\\1c&H90ABFF&\\2c&HABCDEF&\\3c&H010203&\\4c&H345678&\\alpha&H30&\\t(10.5,1500.7,\\fs33.3\\1a&H10&)}Numeric",
        "{\\move(40.5,70.25,250.75,120.5,100.5,1600.75)\\fade(255,0,128,0,200,1400,1900)\\t(0,1600,1.5,\\fscx120\\fr15)}Move",
        "{\\move(50,90,250,90)\\t(1.25,\\bord4\\blur2)\\t(\\xshad3)}Accel",
        "{\\pos(160,90)\\clip(m 0 0 l 300 0 300 170 0 170)\\t(0,1500,\\fs40)}Vector",
        "{\\pos(160,90)\\iclip(1,m 0 0 l 20 0 20 20 0 20)\\t(0,1500,\\fs40)}Inverse",
        "{\\an5\\pos(160,90)\\k12.5}abc{\\kf24.75}def{\\ko15}x{\\kt5.5}q",
        "{\\an5\\pos(160,90)\\q2\\fs23.75junk\\fr-0\\blur\\bord\\t(0,1500,\\fs+1.25)}Prefix {\\unknown1}x{broken",
        "{\\an5\\pos(160,90)\\pbo-3\\p1\\t(0,1500,\\xshad-2)}m 0 0 l 25 0 25 25 0 25{\\p0\\rDefault}Shape",
        "{\\an5\\pos(160,90)\\fsc\\i0\\s1\\u0\\b0\\c&H12ABEF&\\1a&H11&\\2a&H22&\\3a&H33&\\4a&H44&\\t(0,1500,\\fay.2\\fax-.1)}Reset"
    };
    const char header[] =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,";
    FILE* csv = NULL;
    Require(_wfopen_s(&csv, output, L"wb") == 0 && csv, "Cannot create fixture CSV");
    fprintf(csv, "fixture,time_100ns,bitmaps,sha256\n");
    BCRYPT_ALG_HANDLE algorithm = NULL;
    Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0, "Cannot open SHA256");
    const RECT rect = {0, 0, 320, 180};
    const SIZE size = {320, 180};
    CCritSec lock;
    for (unsigned i = 0; i < _countof(fixtures); ++i) {
        CStringA script = CStringA(header) + fixtures[i] + "\n";
        CRenderedTextSubtitle renderer(&lock);
        Require(renderer.Open((BYTE*)script.GetString(), script.GetLength(), DEFAULT_CHARSET, L"fixture"), "Cannot open fixture");
        for (int step = 0; step < 6; ++step) {
            const REFERENCE_TIME time = 50000 + step * 3500000;
            CComPtr<IXySubRenderFrame> frame;
            Require(SUCCEEDED(renderer.RenderEx(&frame, MSP_RGBA, rect, rect, size, time, 24)), "Fixture render failed");
            int count;
            CStringA hash = FrameSha256(frame, algorithm, &count);
            fprintf(csv, "%u,%lld,%d,%s\n", i, time, count, hash.GetString());
        }
    }
    fclose(csv);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    printf("fixture_frames=54\n");
    return 0;
}

template<class Cache> static void PrintCache(const char* name, Cache* cache)
{
    printf("cache_%s_queries=%llu hits=%llu items=%u\n", name,
        static_cast<unsigned long long>(cache->GetQueryCount()),
        static_cast<unsigned long long>(cache->GetCacheHitCount()), unsigned(cache->GetCurItemNum()));
}

class NativeAllocationProbe
{
    XyAllocationStats* m_previous;
public:
    explicit NativeAllocationProbe(XyAllocationStats* stats) : m_previous(xy_set_allocation_stats(stats)) {}
    ~NativeAllocationProbe() { xy_set_allocation_stats(m_previous); }
};

static int AllocationProbeContract()
{
    const size_t initial=g_xy_malloc_used_size;
    XyAllocationStats stats = {}; stats.peakUsed=initial;
    {
        NativeAllocationProbe probe(&stats);
        void* a=xy_malloc(10); void* b=xy_malloc(30);
        Require(a && b,"Allocation probe test could not allocate");
        a=xy_realloc(a,20); Require(a!=NULL,"Allocation probe realloc failed");
        xy_free(b); xy_free(a);
    }
    Require(stats.allocCalls==3 && stats.freeCalls==3 && stats.requestedBytes==60 && stats.failures==0,
        "Allocation probe counts differ from observed operations");
    Require(stats.peakUsed==initial+60 && g_xy_malloc_used_size==initial,"Allocation probe peak/release accounting changed");
    XyAllocationStats outer = {},inner = {};
    {
        NativeAllocationProbe a(&outer);
        { NativeAllocationProbe b(&inner); void* p=xy_malloc(8); xy_free(p); }
        void* p=xy_malloc(16); xy_free(p);
    }
    Require(outer.allocCalls==1 && inner.allocCalls==1 && outer.requestedBytes==16 && inner.requestedBytes==8,
        "Nested allocation probes did not restore ownership");
    printf("allocation_probe_contract=passed (calls, bytes, peak, release, nested restoration)\n");
    return 0;
}

static int LibassContract()
{
    const CStringA header = "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\nYCbCr Matrix: None\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,20,&H000000FF,&H0000FFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    const CStringA drawing = "{\\an7\\pos(20,20)\\bord0\\shad0\\p1\\t(0,1000,\\1c&H00FF00&)}m 0 0 l 60 0 60 40 0 40";
    const CStringA script = header + "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,," + drawing + "\n";
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    Require(renderer.Open((BYTE*)script.GetString(), script.GetLength(), DEFAULT_CHARSET, L"libass-contract"), "Cannot open libass contract");
    Require(!renderer.IsUsingLibass(), "libass is enabled by default");
    const RECT rect = {0,0,320,180}; const SIZE size = {320,180};
    const auto render = [&](REFERENCE_TIME time, int format = MSP_RGBA) {
        CComPtr<IXySubRenderFrame> frame;
        Require(SUCCEEDED(renderer.RenderEx(&frame, format, rect, rect, size, time, 24)) && frame, "libass contract render failed");
        return frame;
    };
    CComPtr<IXySubRenderFrame> native = render(1000000);
    renderer.SetPreferLibass(true);
    Require(renderer.IsUsingLibass(), "ASS input was not captured for libass");
    CComPtr<IXySubRenderFrame> first = render(1000000), retained = render(1000000), later = render(8000000);
    int count = 0; first->GetBitmapCount(&count);
    Require(count > 0 && EqualFrames(first, retained) && !EqualFrames(first, later), "libass animation or repeat changed unexpectedly");
    CComPtr<IXySubRenderFrame> sought = render(1000000);
    Require(EqualFrames(first, sought) && EqualFrames(first, retained), "Seeking or rendering mutated a retained libass frame");
    renderer.SetPreferLibass(false);
    Require(EqualFrames(native, render(1000000)), "Switching back to native changed pixels");
    renderer.SetPreferLibass(true);
    LibassRenderer* backend = renderer.GetLibassRenderer();
    backend->SetSource(header, true);
    const CStringA packet = "0,0,Default,,0,0,0,," + drawing;
    renderer.AddLibassPacket((const BYTE*)packet.GetString(), packet.GetLength(), 0, 20000000);
    Require(EqualFrames(first, render(1000000)), "Embedded ASS differs from the same external ASS");
    renderer.FlushLibass();
    CComPtr<IXySubRenderFrame> empty = render(1000000);
    empty->GetBitmapCount(&count);
    Require(count == 0, "Segment flush retained an embedded libass event");
    renderer.AddLibassPacket((const BYTE*)packet.GetString(), packet.GetLength(), 0, 20000000);
    Require(EqualFrames(first, render(1000000)), "Re-sending after flush lost an event");
    const int formats[] = {MSP_RGBA_F, MSP_AYUV, MSP_AYUV_PLANAR, MSP_XY_AUYV};
    for (int i = 0; i < _countof(formats); ++i) {
        CComPtr<IXySubRenderFrame> a = render(1000000, formats[i]), b = render(1000000, formats[i]);
        Require(EqualFrames(a,b), "libass output format is unstable");
    }
    Require(EqualFrames(first, retained), "Format change mutated retained libass pixels");
    backend->SetSource(script, false);
    STSStyle* authored = renderer.GetStyle(0);
    Require(authored != NULL, "Missing authored style");
    STSStyle saved = *authored;
    authored->colors[0] = RGB(0,0,255);
    renderer.Deinit();
    Require(!EqualFrames(first,render(1000000)), "Style editor changes did not reach libass");
    *authored = saved;
    renderer.Deinit();
    Require(EqualFrames(first,render(1000000)), "Restoring authored styles changed libass output");
    printf("libass_contract=passed (optional selection, animation, seeking, ownership, embedded events, flush, output formats)\n");
    return 0;
}

static int FilterContract(const wchar_t* path, bool xy)
{
    Require(SUCCEEDED(CoInitializeEx(NULL,COINIT_APARTMENTTHREADED)), "Cannot initialize COM");
    HMODULE module=LoadLibraryW(path);
    Require(module!=NULL,"Cannot load filter DLL");
    typedef HRESULT (STDAPICALLTYPE *GetClass)(REFCLSID,REFIID,void**);
    GetClass getClass=reinterpret_cast<GetClass>(GetProcAddress(module,"DllGetClassObject"));
    Require(getClass!=NULL,"Filter has no class factory");
    {
        CLSID clsid,pageId;
        CLSIDFromString(xy ? L"{2dfcb782-ec20-4a7c-b530-4577adb33f21}" : L"{93A22E7A-5091-45ef-BA61-6DA26156A5D0}",&clsid);
        CLSIDFromString(xy ? L"{4514EFD4-E09B-4995-9668-143F12994FE7}" : L"{60765CF5-01C2-4ee7-A44B-C791CF25FEA0}",&pageId);
        CComPtr<IClassFactory> factory;
        Require(SUCCEEDED(getClass(clsid,IID_IClassFactory,(void**)&factory)),"Cannot get filter class");
        CComPtr<IUnknown> object;
        Require(SUCCEEDED(factory->CreateInstance(NULL,IID_IUnknown,(void**)&object)),"Cannot create filter");
        CComQIPtr<IXyOptions> options(object);
        Require(options!=NULL,"Missing filter options");
        bool original=false,value=false;
        const unsigned field=DirectVobSubXyOptions::BOOL_PREFER_LIBASS;
        Require(SUCCEEDED(options->XyGetBool(field,&original)),"Cannot read libass preference");
        Require(SUCCEEDED(options->XySetBool(field,!original)) && SUCCEEDED(options->XyGetBool(field,&value)) && value!=original,"Cannot change preference");
        Require(SUCCEEDED(options->XySetBool(field,original)),"Cannot restore preference");
        CComPtr<IClassFactory> pageFactory;
        Require(SUCCEEDED(getClass(pageId,IID_IClassFactory,(void**)&pageFactory)),"Cannot get Main page class");
        CComPtr<IPropertyPage> page;
        Require(SUCCEEDED(pageFactory->CreateInstance(NULL,IID_IPropertyPage,(void**)&page)),"Cannot create Main page");
        IUnknown* connected=object;
        Require(SUCCEEDED(page->SetObjects(1,&connected)),"Cannot connect Main page");
        PROPPAGEINFO info={sizeof(info)};
        Require(SUCCEEDED(page->GetPageInfo(&info)),"Cannot measure Main page");
        HWND host=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,info.size.cx+20,info.size.cy+20,NULL,NULL,g_hInst,NULL);
        Require(host!=NULL,"Cannot create hidden property host");
        RECT bounds={0,0,info.size.cx,info.size.cy};
        Require(SUCCEEDED(page->Activate(host,&bounds,FALSE)),"Cannot activate Main page");
        HWND dialog=FindWindowExW(host,NULL,L"#32770",NULL);
        HWND checkbox=GetDlgItem(dialog,IDC_PREFER_LIBASS), loading=GetDlgItem(dialog,IDC_LOADCOMBO);
        wchar_t label[80]={};
        GetWindowTextW(checkbox,label,_countof(label));
        Require(checkbox && loading && !wcscmp(label,L"Prefer libass rendering"),"Missing Main checkbox or Loading controls");
        Require((SendMessageW(checkbox,BM_GETCHECK,0,0)==BST_CHECKED)==original,"Main checkbox does not reflect the option");
        RECT checkRect,loadRect,client;
        GetWindowRect(checkbox,&checkRect); GetWindowRect(loading,&loadRect); GetClientRect(dialog,&client);
        POINT bottom={checkRect.right,checkRect.bottom}; ScreenToClient(dialog,&bottom);
        Require(checkRect.top>loadRect.bottom && bottom.x<=client.right && bottom.y<=client.bottom,"Main controls overlap or extend outside the page");
        page->Deactivate(); page->SetObjects(0,NULL); DestroyWindow(host);
        CoTaskMemFree(info.pszTitle); CoTaskMemFree(info.pszDocString); CoTaskMemFree(info.pszHelpFile);
    }
    FreeLibrary(module); CoUninitialize();
    printf("filter_contract=passed (COM option round-trip and hidden Main page layout; no registry writes)\n");
    return 0;
}

struct BenchmarkSettings
{
    bool active, hashes, libass;
    int first, last, mode; // 0 complete render, 1 parse/reconstruction, 2 composition replay
    BenchmarkSettings() : active(false), hashes(true), libass(false), first(32128), last(34555), mode(0) {}
};
static BenchmarkSettings benchmark;

static unsigned __int64 ThreadTicks()
{
    FILETIME created, ended, kernel, user;
    Require(GetThreadTimes(GetCurrentThread(), &created, &ended, &kernel, &user) != 0, "Cannot read thread CPU time");
    ULARGE_INTEGER k, u;
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return k.QuadPart + u.QuadPart;
}

static void ConfigureBenchmark(int cpu)
{
    DWORD_PTR allowed, system;
    Require(GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system) != 0, "Cannot read CPU affinity");
    if (cpu < 0) for (int i = 0; i < sizeof(DWORD_PTR)*8; ++i) if (allowed & (DWORD_PTR(1) << i)) cpu = i;
    Require(cpu >= 0 && cpu < sizeof(DWORD_PTR)*8 && (allowed & (DWORD_PTR(1) << cpu)), "Selected CPU is unavailable in this process group");
    DWORD_PTR mask = DWORD_PTR(1) << cpu;
    Require(SetProcessAffinityMask(GetCurrentProcess(), mask) != 0, "Cannot restrict benchmark to one logical CPU");
    Require(SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS) != 0, "Cannot set Above Normal priority");
    Require(GetPriorityClass(GetCurrentProcess()) == ABOVE_NORMAL_PRIORITY_CLASS, "Benchmark priority was not applied");
    Require(GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system) && allowed == mask, "Benchmark affinity was not applied");
    printf("benchmark_cpu=%d affinity=0x%llx priority=AboveNormal render_threads=1\n", cpu, static_cast<unsigned long long>(mask));
}

static int Render(const wchar_t* script, const wchar_t* fonts, const wchar_t* output, int bitmapItems = -1, bool diagnostics = false)
{
    if (bitmapItems >= 0) CacheManager::SetBitmapCacheSize(bitmapItems);
    LoadFonts(fonts);
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    Require(renderer.Open(script, DEFAULT_CHARSET), "Cannot open subtitle script");
    if (benchmark.libass) {
        renderer.SetPreferLibass(true);
        Require(renderer.IsUsingLibass(), "This source does not support libass");
        renderer.GetLibassRenderer()->LoadFonts(fonts);
    }
    BCRYPT_ALG_HANDLE algorithm = NULL;
    Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0, "Cannot open SHA256");
    FILE* csv = NULL;
    Require(_wfopen_s(&csv, output, L"wb") == 0 && csv, "Cannot create output CSV");
    fprintf(csv, "frame,time_100ns,render_ms,bitmaps,sha256");
    if (diagnostics) fprintf(csv, ",render_core_ms,parse_ms,build_draw_ms,composite_ms,tree_ms,merge_ms,group_ms,bitmap_ms,input_items,groups,member_references,unique_members,draw_items,draw_vector_growths,xy_alloc_calls,xy_free_calls,xy_requested_bytes,xy_peak_above_start,xy_failures");
    if (diagnostics) fprintf(csv, ",draw_reuse_hits,draw_reuse_misses,machine_allocations,draw_allocations,overlay_key_allocations,draw_metadata_bytes");
    if (benchmark.active) fprintf(csv, ",thread_cpu_ms");
    fprintf(csv, "\n");
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const RECT rect = {0, 0, 1920, 1080};
    const SIZE size = {1920, 1080};
    // Every source frame in [22:20, EOF), 24000/1001 fps, evaluated at its midpoint.
    for (int frameIndex = benchmark.first; frameIndex <= benchmark.last; ++frameIndex) {
        const REFERENCE_TIME time = (REFERENCE_TIME(frameIndex) * 2 + 1) * 1001 * 10000000 / 48000;
        CComPtr<IXySubRenderFrame> frame;
        LARGE_INTEGER before, after;
        XyAllocationStats allocations = {};
        const size_t initialBytes = g_xy_malloc_used_size;
        allocations.peakUsed = initialBytes;
        if (diagnostics) renderer.EnableDiagnostics(true);
        HRESULT hr;
        LONGLONG phaseTicks = 0;
        ULONGLONG phaseCpu = 0;
        const auto drawBefore=renderer.GetDrawReuseStats();
        const unsigned __int64 cpuBefore = benchmark.active ? ThreadTicks() : 0;
        {
            NativeAllocationProbe probe(diagnostics && !benchmark.mode ? &allocations : NULL);
            QueryPerformanceCounter(&before);
            if (benchmark.mode) hr = renderer.BenchmarkNativePhase(&frame,time,benchmark.mode==1,phaseTicks,phaseCpu);
            else hr = renderer.RenderEx(&frame, MSP_RGBA, rect, rect, size, time, 24000.0 / 1001);
            QueryPerformanceCounter(&after);
        }
        const unsigned __int64 cpuAfter = benchmark.active ? ThreadTicks() : 0;
        Require(SUCCEEDED(hr), "RenderEx failed");
        int count = 0;
        CStringA hash;
        if (benchmark.hashes) hash = FrameSha256(frame, algorithm, &count);
        else if (frame) Require(SUCCEEDED(frame->GetBitmapCount(&count)), "Cannot read bitmap count");
        fprintf(csv, "%d,%lld,%.6f,%d,%s", frameIndex, time,
            (benchmark.mode ? phaseTicks : after.QuadPart - before.QuadPart) * 1000.0 / frequency.QuadPart, count, hash.GetString());
        if (diagnostics) {
            const RenderDiagnostics& stats = renderer.GetDiagnostics();
            for (int i = 0; i < RenderDiagnostics::PHASE_COUNT; ++i)
                fprintf(csv, ",%.6f", stats.ticks[i] * 1000.0 / frequency.QuadPart);
            fprintf(csv, ",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
                stats.inputItems, stats.groups, stats.memberReferences, stats.uniqueMembers,
                stats.drawItems, stats.drawVectorGrowths, allocations.allocCalls, allocations.freeCalls,
                allocations.requestedBytes, static_cast<unsigned long long>(allocations.peakUsed-initialBytes), allocations.failures);
        }
        if (diagnostics) {
            const auto d=renderer.GetDrawReuseStats();
            fprintf(csv,",%llu,%llu,%llu,%llu,%llu,%llu",d.hits-drawBefore.hits,d.misses-drawBefore.misses,
                d.machineAllocations-drawBefore.machineAllocations,d.drawAllocations-drawBefore.drawAllocations,
                d.keyAllocations-drawBefore.keyAllocations,static_cast<unsigned long long>(d.bytes));
        }
        if (benchmark.active) fprintf(csv, ",%.6f", (benchmark.mode ? phaseCpu : cpuAfter - cpuBefore) / 10000.0);
        fprintf(csv, "\n");
        if (frameIndex % 240 == 0) { fflush(csv); printf("frame %d\n", frameIndex); fflush(stdout); }
    }
    fclose(csv);
    AssTagListMruCache* tags = CacheManager::GetAssTagListMruCache();
    printf("tag_cache_queries=%llu hits=%llu items=%u\n",
        static_cast<unsigned long long>(tags->GetQueryCount()),
        static_cast<unsigned long long>(tags->GetCacheHitCount()), unsigned(tags->GetCurItemNum()));
    const CRenderedTextSubtitle::ParsedTagCacheStats syntax = renderer.GetParsedTagCacheStats();
    printf("event_tag_hits=%llu misses=%llu retained_bytes=%u peak_accounted_bytes=%u entries=%u\n",
        syntax.hits, syntax.misses, unsigned(syntax.bytes), unsigned(syntax.peakBytes), unsigned(syntax.entries));
    printf("subtitle_pool_hits=%llu misses=%llu empty_items=%u retained_bytes=%u\n",
        renderer.GetSubtitlePool().GetHits(), renderer.GetSubtitlePool().GetMisses(),
        unsigned(renderer.GetSubtitlePool().GetCount()), unsigned(renderer.GetSubtitlePool().GetRetainedBytes()));
    const auto scalar=renderer.GetScalarParameterCacheStats();
    printf("scalar_parameter_hits=%llu misses=%llu entries=%u retained_bytes=%u\n",scalar.hits,scalar.misses,unsigned(scalar.entries),unsigned(scalar.bytes));
    const auto maintenance=renderer.GetMaintenanceStats();
    printf("maintenance_order_builds=%llu hits=%llu parsed_expiry_scans=%llu subtitle_expiry_scans=%llu\n",
        maintenance.orderBuilds,maintenance.orderHits,maintenance.parsedScans,maintenance.subtitleScans);
    const auto ahead=renderer.GetLookaheadStats();
    printf("lookahead_attempted=%llu retained=%llu skipped=%llu elapsed_ms=%.6f largest_unit_ms=%.6f\n",
        ahead.attempted,ahead.retained,ahead.skipped,ahead.ticks*1000.0/frequency.QuadPart,ahead.largestUnit*1000.0/frequency.QuadPart);
    const auto drawReuse=renderer.GetDrawReuseStats();
    printf("draw_reuse_hits=%llu misses=%llu machine_allocations=%llu draw_allocations=%llu overlay_key_allocations=%llu retained_bytes=%llu peak_bytes=%llu entries=%llu\n",
        drawReuse.hits,drawReuse.misses,drawReuse.machineAllocations,drawReuse.drawAllocations,drawReuse.keyAllocations,
        static_cast<unsigned long long>(drawReuse.bytes),static_cast<unsigned long long>(drawReuse.peakBytes),static_cast<unsigned long long>(drawReuse.entries));
    const auto layout = renderer.GetPreparedLayoutStats();
    printf("prepared_layout_hits=%llu builds=%llu fallbacks=%llu updates=%llu retained_bytes=%llu peak_accounted_bytes=%llu entries=%llu\n",
        layout.hits, layout.builds, layout.fallbacks, layout.updates, static_cast<unsigned long long>(layout.bytes),
        static_cast<unsigned long long>(layout.peakBytes), static_cast<unsigned long long>(layout.entries));
    printf("reconstruction_hits=%llu entries=%u peak_entries=%u peak_accounted_bytes=%u\n",
        renderer.GetReconstructionCacheHits(), unsigned(renderer.GetReconstructionCacheCount()),
        unsigned(renderer.GetReconstructionCachePeak()), unsigned(renderer.GetReconstructionCachePeakBytes()));
    PrintCache("overlay", CacheManager::GetOverlayMruCache());
    PrintCache("subpixel", CacheManager::GetSubpixelVarianceCache());
    PrintCache("no_blur", CacheManager::GetOverlayNoBlurMruCache());
    PrintCache("path", CacheManager::GetPathDataMruCache());
    PrintCache("scanline", CacheManager::GetScanLineDataMruCache());
    PrintCache("scanline2", CacheManager::GetScanLineData2MruCache());
    PrintCache("bitmap", CacheManager::GetBitmapMruCache());
    PrintCache("bitmap_key", XyFwGroupedDrawItemsHashKey::GetCacher());
    PROCESS_MEMORY_COUNTERS_EX memory = {};
    memory.cb = sizeof(memory);
    Require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)) != 0, "Cannot read native memory counters");
    printf("process_private_bytes=%llu peak_pagefile_bytes=%llu working_set_bytes=%llu xy_allocated_bytes_end=%llu\n",
        static_cast<unsigned long long>(memory.PrivateUsage), static_cast<unsigned long long>(memory.PeakPagefileUsage),
        static_cast<unsigned long long>(memory.WorkingSetSize), static_cast<unsigned long long>(g_xy_malloc_used_size));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    g_hInst = GetModuleHandleW(NULL);
    if (!AfxWinInit(g_hInst, NULL, GetCommandLineW(), 0)) return 2;
    try {
        if (argc < 2 || wcscmp(argv[1], L"--benchmark")) ConfigureBenchmark(-1);
        if (argc==4 && !wcscmp(argv[1],L"--filter-contract")) return FilterContract(argv[3],!wcscmp(argv[2],L"xy"));
        if (argc == 2 && !wcscmp(argv[1], L"--libass-contract")) {
            return LibassContract();
        }
        if (argc >= 5 && !wcscmp(argv[1], L"--benchmark")) {
            benchmark.active = true;
            int cpu = -1, items = -1;
            bool diagnostics = true;
            int i = 2;
            for (; i < argc - 3; ++i) {
                if (!wcscmp(argv[i], L"--no-hash")) benchmark.hashes = false;
                else if (!wcscmp(argv[i], L"--no-diagnostics")) diagnostics = false;
                else if (!wcscmp(argv[i], L"--libass")) benchmark.libass = true;
                else if (!wcscmp(argv[i], L"--mode")) {
                    Require(i+1 < argc-3,"Missing benchmark mode");
                    ++i;
                    if (!wcscmp(argv[i],L"parse")) { benchmark.mode=1; benchmark.hashes=false; }
                    else if (!wcscmp(argv[i],L"composite")) benchmark.mode=2;
                    else Require(!wcscmp(argv[i],L"render"),"Unknown benchmark mode");
                }
                else {
                    Require(i + 1 < argc - 3, "Missing benchmark option value");
                    wchar_t* end = NULL;
                    const long value = wcstol(argv[++i], &end, 10);
                    Require(end != argv[i] && !*end && value >= 0 && value <= INT_MAX, "Invalid benchmark integer");
                    if (!wcscmp(argv[i-1], L"--cpu")) cpu = value;
                    else if (!wcscmp(argv[i-1], L"--bitmap-cache-items")) { Require(value <= 1024, "Cache capacity is too large"); items = value; }
                    else if (!wcscmp(argv[i-1], L"--first-frame")) benchmark.first = value;
                    else if (!wcscmp(argv[i-1], L"--last-frame")) benchmark.last = value;
                    else throw std::runtime_error("Unknown benchmark option");
                }
            }
            Require(i == argc - 3 && benchmark.last >= benchmark.first, "Invalid benchmark frame range");
            Require(!benchmark.libass || benchmark.mode==0,"Phase replay is native-only");
            ConfigureBenchmark(cpu);
            return Render(argv[i], argv[i+1], argv[i+2], items, diagnostics);
        }
        if (argc == 2 && !wcscmp(argv[1], L"--hash-contract")) return HashContract();
        if (argc == 2 && !wcscmp(argv[1], L"--intern-contract")) return InternContract();
        if (argc == 2 && !wcscmp(argv[1], L"--cache-contract")) return CacheContract();
        if (argc == 2 && !wcscmp(argv[1], L"--geometry-contract")) return GeometryContract();
        if (argc == 2 && !wcscmp(argv[1], L"--inline-list-contract")) return InlineListContract();
        if (argc == 2 && !wcscmp(argv[1], L"--ass-command-contract")) return AssCommandContract();
        if (argc == 2 && !wcscmp(argv[1], L"--font-layout-contract")) return FontLayoutContract();
        if (argc == 2 && !wcscmp(argv[1], L"--prepared-layout-contract")) return PreparedLayoutContract();
        if (argc == 2 && !wcscmp(argv[1], L"--constant-drawing-contract")) return ConstantDrawingContract();
        if (argc == 2 && !wcscmp(argv[1], L"--static-drawing-contract")) return StaticDrawingContract();
        if (argc == 2 && !wcscmp(argv[1], L"--polygon-scanner-contract")) return PolygonScannerContract();
        if (argc == 2 && !wcscmp(argv[1], L"--maintenance-contract")) return MaintenanceContract();
        if (argc == 2 && !wcscmp(argv[1], L"--lookahead-contract")) return LookaheadContract();
        if (argc == 2 && !wcscmp(argv[1], L"--draw-reuse-contract")) return DrawReuseContract();
        if (argc == 2 && !wcscmp(argv[1], L"--scalar-parameter-contract")) return ScalarParameterContract();
        if (argc == 2 && !wcscmp(argv[1], L"--tag-storage-contract")) return TagStorageContract();
        if (argc == 2 && !wcscmp(argv[1], L"--paint-bundle-contract")) return PaintBundleContract();
        if (argc == 2 && !wcscmp(argv[1], L"--clipper-retention-contract")) return ClipperRetentionContract();
        if (argc == 2 && !wcscmp(argv[1], L"--lazy-intern-contract")) return LazyInternContract();
        if (argc == 2 && !wcscmp(argv[1], L"--alpha-buffer-contract")) return AlphaBufferContract();
        if (argc == 2 && !wcscmp(argv[1], L"--fused-alpha-contract")) return FusedAlphaContract();
        if (argc == 2 && !wcscmp(argv[1], L"--zero-alpha-contract")) return ZeroAlphaContract();
        if (argc == 2 && !wcscmp(argv[1], L"--effect-contract")) return EffectContract();
        if (argc == 2 && !wcscmp(argv[1], L"--reconstruction-pool-contract")) return ReconstructionPoolContract();
        if (argc == 2 && !wcscmp(argv[1], L"--layer-key-contract")) return LayerKeyContract();
        if (argc == 2 && !wcscmp(argv[1], L"--reconstruction-cache-contract")) return ReconstructionCacheContract();
        if (argc == 2 && !wcscmp(argv[1], L"--group-key-contract")) return GroupKeyContract();
        if (argc == 2 && !wcscmp(argv[1], L"--group-view-contract")) return GroupViewContract();
        if (argc == 2 && !wcscmp(argv[1], L"--word-key-cache-contract")) return WordKeyCacheContract();
        if (argc == 2 && !wcscmp(argv[1], L"--karaoke-hash-contract")) return KaraokeHashContract();
        if (argc == 2 && !wcscmp(argv[1], L"--cache-capacity-contract")) return CacheCapacityContract();
        if (argc == 2 && !wcscmp(argv[1], L"--grouping-contract")) return GroupingContract();
        if (argc == 2 && !wcscmp(argv[1], L"--allocation-probe-contract")) return AllocationProbeContract();
        if (argc == 3 && !wcscmp(argv[1], L"--fixtures")) return Fixtures(argv[2]);
        if (argc == 6 && !wcscmp(argv[1],L"--bitmap-cache-items")) {
            const int items = _wtoi(argv[2]);
            Require(items >= 0 && items <= 1024, "Bitmap experiment must use 0-1024 entries");
            return Render(argv[3],argv[4],argv[5],items);
        }
        if (argc == 5 && !wcscmp(argv[1], L"--diagnostics")) return Render(argv[2],argv[3],argv[4],-1,true);
        if (argc == 7 && !wcscmp(argv[1], L"--diagnostics") && !wcscmp(argv[2],L"--bitmap-cache-items")) {
            const int items = _wtoi(argv[3]);
            Require(items >= 0 && items <= 1024, "Bitmap experiment must use 0-1024 entries");
            return Render(argv[4],argv[5],argv[6],items,true);
        }
        if (argc == 4) return Render(argv[1], argv[2], argv[3]);
        fprintf(stderr, "Usage: render_subtitles script.ass font_directory output.csv | --hash-contract\n");
        return 2;
    } catch (const std::exception& error) {
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
