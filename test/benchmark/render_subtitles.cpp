// Offline validation for the shared RTS renderer used by both filter paths.
#include "stdafx.h"
#include "xy_bitmap.h"
#include "xy_overlay_paint_machine.h"
#include "draw_item.h"
#include "xy_clipper_paint_machine.h"
#include <bcrypt.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <set>
#include <vector>
#include <stdexcept>
#include <cstdio>

HINSTANCE g_hInst = NULL;
CWinApp benchmarkApp;

static void Require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
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
    printf("inline_list_contract=passed (spill, order, truncation, refill, ownership, line iteration)\n");
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
        for (int y = 0; y < sa.cy; ++y)
            if (memcmp((const BYTE*)ba + y * pitcha, (const BYTE*)bb + y * pitchb, sa.cx * 4)) return false;
    }
    return true;
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

static int Render(const wchar_t* script, const wchar_t* fonts, const wchar_t* output, int bitmapItems = -1)
{
    if (bitmapItems >= 0) CacheManager::SetBitmapCacheSize(bitmapItems);
    LoadFonts(fonts);
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    Require(renderer.Open(script, DEFAULT_CHARSET), "Cannot open subtitle script");
    BCRYPT_ALG_HANDLE algorithm = NULL;
    Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0, "Cannot open SHA256");
    FILE* csv = NULL;
    Require(_wfopen_s(&csv, output, L"wb") == 0 && csv, "Cannot create output CSV");
    fprintf(csv, "frame,time_100ns,render_ms,bitmaps,sha256\n");
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const RECT rect = {0, 0, 1920, 1080};
    const SIZE size = {1920, 1080};
    // Every source frame in [22:20, EOF), 24000/1001 fps, evaluated at its midpoint.
    for (int frameIndex = 32128; frameIndex <= 34555; ++frameIndex) {
        const REFERENCE_TIME time = (REFERENCE_TIME(frameIndex) * 2 + 1) * 1001 * 10000000 / 48000;
        CComPtr<IXySubRenderFrame> frame;
        LARGE_INTEGER before, after;
        QueryPerformanceCounter(&before);
        HRESULT hr = renderer.RenderEx(&frame, MSP_RGBA, rect, rect, size, time, 24000.0 / 1001);
        QueryPerformanceCounter(&after);
        Require(SUCCEEDED(hr), "RenderEx failed");
        int count = 0;
        CStringA hash = FrameSha256(frame, algorithm, &count);
        fprintf(csv, "%d,%lld,%.6f,%d,%s\n", frameIndex, time,
            (after.QuadPart - before.QuadPart) * 1000.0 / frequency.QuadPart, count, hash.GetString());
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
        if (argc == 2 && !wcscmp(argv[1], L"--hash-contract")) return HashContract();
        if (argc == 2 && !wcscmp(argv[1], L"--intern-contract")) return InternContract();
        if (argc == 2 && !wcscmp(argv[1], L"--cache-contract")) return CacheContract();
        if (argc == 2 && !wcscmp(argv[1], L"--geometry-contract")) return GeometryContract();
        if (argc == 2 && !wcscmp(argv[1], L"--inline-list-contract")) return InlineListContract();
        if (argc == 2 && !wcscmp(argv[1], L"--alpha-buffer-contract")) return AlphaBufferContract();
        if (argc == 2 && !wcscmp(argv[1], L"--zero-alpha-contract")) return ZeroAlphaContract();
        if (argc == 2 && !wcscmp(argv[1], L"--effect-contract")) return EffectContract();
        if (argc == 2 && !wcscmp(argv[1], L"--reconstruction-pool-contract")) return ReconstructionPoolContract();
        if (argc == 2 && !wcscmp(argv[1], L"--layer-key-contract")) return LayerKeyContract();
        if (argc == 2 && !wcscmp(argv[1], L"--reconstruction-cache-contract")) return ReconstructionCacheContract();
        if (argc == 2 && !wcscmp(argv[1], L"--group-key-contract")) return GroupKeyContract();
        if (argc == 2 && !wcscmp(argv[1], L"--word-key-cache-contract")) return WordKeyCacheContract();
        if (argc == 2 && !wcscmp(argv[1], L"--karaoke-hash-contract")) return KaraokeHashContract();
        if (argc == 2 && !wcscmp(argv[1], L"--cache-capacity-contract")) return CacheCapacityContract();
        if (argc == 3 && !wcscmp(argv[1], L"--fixtures")) return Fixtures(argv[2]);
        if (argc == 6 && !wcscmp(argv[1],L"--bitmap-cache-items")) {
            const int items = _wtoi(argv[2]);
            Require(items >= 0 && items <= 1024, "Bitmap experiment must use 0-1024 entries");
            return Render(argv[3],argv[4],argv[5],items);
        }
        if (argc == 4) return Render(argv[1], argv[2], argv[3]);
        fprintf(stderr, "Usage: render_subtitles script.ass font_directory output.csv | --hash-contract\n");
        return 2;
    } catch (const std::exception& error) {
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
