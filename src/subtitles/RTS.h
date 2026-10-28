/*
 *  Copyright (C) 2003-2006 Gabest
 *  http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#include "STS.h"
#include "LibassRenderer.h"
#include "Rasterizer.h"
#include "../SubPic/SubPicProviderImpl.h"
#include "../SubPic/ISimpleSubPic.h"
#include <atlcoll.h>
#include <boost/flyweight/key_value.hpp>
#include <boost/smart_ptr.hpp>
#include <map>
#include <vector>
#include <utility>
#include "mru_cache.h"
#include "xy_int_map.h"

//how hard positioned is the ass cmd 
enum AssCmdPosLevel
{
    POS_LVL_NONE,
    POS_LVL_SOFT,
    POS_LVL_HARD
};

class CMyFont : public CFont
{
public:
    int m_ascent, m_descent;

    CMyFont(const STSStyleBase& style);
};

typedef ::boost::flyweights::flyweight<::boost::flyweights::key_value<STSStyleBase, CMyFont>, ::boost::flyweights::no_locking> FwCMyFont;

class CPolygon;

class CClipper;
typedef ::boost::shared_ptr<CClipper> SharedPtrCClipper;

struct CompositeDrawItem;
class CompositeDrawItemList;
typedef CAtlList<CompositeDrawItemList> CompositeDrawItemListList;

class CWord;
typedef CWord* PCWord;
typedef ::boost::shared_ptr<CWord> SharedPtrCWord;
typedef ::boost::shared_ptr<CPolygon> SharedPtrCPolygon;

class CClipperPaintMachine;
typedef ::boost::shared_ptr<CClipperPaintMachine> SharedPtrCClipperPaintMachine;

class OverlayKey;
class DrawItemHashKey;
interface IXySubRenderFrame;

// Optional native diagnostics. Normal playback leaves collection disabled.
// Phase times overlap: COMPOSITE includes its subphases and their cleanup.
struct RenderDiagnostics
{
    enum Phase { RENDER, PARSE, BUILD_DRAW, COMPOSITE, TREE, MERGE, GROUP, BITMAP, PHASE_COUNT };
    unsigned __int64 ticks[PHASE_COUNT];
    unsigned __int64 frames, inputItems, groups, memberReferences, uniqueMembers;
    unsigned __int64 drawItems, drawVectorGrowths;
    RenderDiagnostics() { Reset(); }
    void Reset() { memset(this, 0, sizeof(*this)); }
};

class RenderPhaseTimer
{
    RenderDiagnostics* m_stats;
    RenderDiagnostics::Phase m_phase;
    LARGE_INTEGER m_start;
public:
    RenderPhaseTimer(RenderDiagnostics* stats, RenderDiagnostics::Phase phase) : m_stats(stats), m_phase(phase)
    { if (m_stats) QueryPerformanceCounter(&m_start); }
    ~RenderPhaseTimer()
    {
        if (m_stats) {
            LARGE_INTEGER end; QueryPerformanceCounter(&end);
            m_stats->ticks[m_phase] += end.QuadPart - m_start.QuadPart;
        }
    }
private:
    RenderPhaseTimer(const RenderPhaseTimer&);
    RenderPhaseTimer& operator=(const RenderPhaseTimer&);
};

class CWord
{
    bool NeedTransform  ();
    void Transform      (PathData* path_data, const CPointCoor2& org);
//  void Transform_C    (PathData* path_data, const CPointCoor2 &org );
//  void Transform_SSE2 (PathData* path_data, const CPointCoor2 &org );
    bool CreateOpaqueBox();

protected:
    virtual bool CreatePath(PathData* path_data) = 0;

    bool DoPaint(const CPointCoor2& p, const CPointCoor2& trans_org, SharedPtrOverlay* overlay, const OverlayKey& key);
public:
    // str[0] = 0 -> m_fLineBreak = true (in this case we only need and use the height of m_font from the whole class)
    CWord(const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend
        , double target_scale_x=1.0, double target_scale_y=1.0
        , bool round_to_whole_pixel_after_scale_to_target = false);
    CWord(const CWord&);
    virtual ~CWord();

    // Conservative charge for an immutable, unpainted layout prototype.
    size_t GetPreparedStorageBytes() const;
    virtual SharedPtrCWord Copy() = 0;
    virtual bool Append(const SharedPtrCWord& w);
    
    bool operator==(const CWord& rhs)const;

    static void PaintFromOverlay   (const CPointCoor2& p, const CPointCoor2& trans_org2, const OverlayKey &subpixel_variance_key, SharedPtrOverlay& overlay);
    void PaintFromNoneBluredOverlay(SharedPtrOverlay raterize_result, const OverlayKey& overlay_key, SharedPtrOverlay* overlay);
    bool PaintFromScanLineData2    (const CPointCoor2& psub, const ScanLineData2& scan_line_data2, const OverlayKey& key    , SharedPtrOverlay* overlay);
    bool PaintFromPathData         (const CPointCoor2& psub, const CPointCoor2& trans_org, const PathData& path_data, const OverlayKey& key, SharedPtrOverlay* overlay );
    bool PaintFromRawData          (const CPointCoor2& psub, const CPointCoor2& trans_org, const OverlayKey& key    , SharedPtrOverlay* overlay );

protected:
    XyFwStringW       m_str;
    // Three small keys cover body/outline/shadow geometry and draw state.
    // Keys own no word or painted bitmap, so these references cannot cycle.
    boost::shared_ptr<OverlayKey> m_overlayKeyCache[3];
    boost::shared_ptr<DrawItemHashKey> m_drawKeyCache[3];
    unsigned m_nextOverlayKey, m_nextDrawKey;
public:
    bool              m_fWhiteSpaceChar, m_fLineBreak;
    FwSTSStyle        m_style;
    SharedPtrCPolygon m_pOpaqueBox;
    int               m_ktype, m_kstart, m_kend;
    int               m_width, m_ascent, m_descent;
    double            m_target_scale_x, m_target_scale_y;
    bool              m_round_to_whole_pixel_after_scale_to_target;//it is necessary to avoid some artifacts

    //friend class CWordCache;
    friend class PathDataCacheKey;
    friend class ClipperTraits;
    friend class ClipperAlphaMaskCacheKey;
    friend class CWordPaintMachine;
    friend class CWordPaintResultKey;
    friend class CClipper;
};

class CText : public CWord
{
public:
    struct TextInfo
    {
        int m_width, m_ascent, m_descent;
    };
    typedef ::boost::shared_ptr<TextInfo> SharedPtrTextInfo;
protected:
    virtual bool CreatePath(PathData* path_data);

    static void GetTextInfo(TextInfo *output, const FwSTSStyle& style, const CStringW& str);
public:
    CText(const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend
        , double target_scale_x=1.0, double target_scale_y=1.0);
    CText(const CText& src);
    bool HasSmallRetainedText() const { return m_str.Get().GetLength() <= 128; }

    virtual SharedPtrCWord Copy();
    virtual bool Append(const SharedPtrCWord& w);
};

class CPolygon : public CWord
{
    bool Get6BitFixedPoint(const WCHAR*& cursor, LONG& ret);
    bool GetPOINT(const WCHAR*& cursor, POINT& ret);
    bool ParseStr();
    void UpdateMetrics(bool scaleCoordinates);

protected:
    double            m_scalex, m_scaley;
    int               m_baseline;
    CAtlArray<BYTE>   m_pathTypesOrg;
    CAtlArray<CPoint> m_pathPointsOrg;
    SharedPtrConstPathData m_cachedPath;

    virtual bool CreatePath(PathData* path_data);

public:
    CPolygon(const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend
        , double scalex, double scaley, int baseline
        , double target_scale_x=1.0, double target_scale_y=1.0
        , bool round_to_whole_pixel_after_scale_to_target = false);
	// can't use a const reference because we need to use CAtlArray::Copy which expects a non-const reference
    CPolygon(CPolygon&); 
    bool HasSmallRetainedPath() const
    {
        const size_t count = m_cachedPath ? m_cachedPath->mPathPoints : m_pathTypesOrg.GetCount();
        return count <= 16 && m_str.Get().GetLength() <= 128;
    }
    virtual ~CPolygon();
    size_t GetPreparedPathBytes() const;
    bool FreezePreparedPath();

    virtual SharedPtrCWord Copy();
    virtual bool Append(const SharedPtrCWord& w);

    friend class ClipperTraits;
    friend class ClipperAlphaMaskCacheKey;
    friend class PathDataCacheKey;
};

enum eftype
{
    EF_MOVE = 0,    // {\move(x1=param[0], y1=param[1], x2=param[2], y2=param[3], t1=t[0], t2=t[1])} or {\pos(x=param[0], y=param[1])}
    EF_ORG,         // {\org(x=param[0], y=param[1])}
    EF_FADE,        // {\fade(a1=param[0], a2=param[1], a3=param[2], t1=t[0], t2=t[1], t3=t[2], t4=t[3])} or {\fad(t1=t[1], t2=t[2])
    EF_BANNER,      // Banner;delay=param[0][;lefttoright=param[1];fadeawaywidth=param[2]]
    EF_SCROLL,      // Scroll up/down=param[3];top=param[0];bottom=param[1];delay=param[2][;fadeawayheight=param[4]]
};

#define EF_NUMBEROFEFFECTS 5

class Effect
{
public:
    Effect()
    {
        memset(param, 0, sizeof(param));
        memset(t, 0, sizeof(t));
    }
    bool operator==(const Effect& rhs)const
    {
        return type==rhs.type 
            && !memcmp(param, rhs.param, sizeof(param))
            && !memcmp(t, rhs.t, sizeof(t));
    }
public:
    enum eftype type;
    int param[8];
    int t[4];
};

class CClipper
{
private:
    SharedPtrCPolygon m_polygon;
    CSizeCoor2        m_size;
    bool              m_inverse;
    Effect            m_effect;
    int               m_effectType;
    bool              m_painted;

    GrayImage2* PaintSimpleClipper();
    GrayImage2* PaintBaseClipper();
    GrayImage2* PaintBannerClipper();
    GrayImage2* PaintScrollClipper();
    
    GrayImage2* Paint();
public:
    CClipper(CStringW str, CSizeCoor2 size, double scalex, double scaley, bool inverse
        , double target_scale_x=1.0, double target_scale_y=1.0);
    void SetEffect(const Effect& effect, int effectType);
    virtual ~CClipper();

    static SharedPtrGrayImage2 GetAlphaMask(const SharedPtrCClipper& clipper);

    friend class ClipperTraits;
    friend class ClipperAlphaMaskCacheKey;
    friend class CClipperPaintMachine;
};

// Most subtitle events and lines contain one word. Keep that element inline;
// longer text spills into a vector whose storage follows the container lifetime.
template<class T> class CInlineList
{
    T m_first;
    std::vector<T> m_rest;
    bool m_hasFirst;
public:
    CInlineList() : m_first(), m_hasFirst(false) {}
    size_t GetCount() const { return m_hasFirst ? m_rest.size() + 1 : 0; }
    size_t GetCapacity() const { return 1 + m_rest.capacity(); }
    T& operator[](size_t i) { ASSERT(i < GetCount()); return i ? m_rest[i-1] : m_first; }
    const T& operator[](size_t i) const { ASSERT(i < GetCount()); return i ? m_rest[i-1] : m_first; }
    void AddTail(const T& value)
    {
        if (m_hasFirst) m_rest.push_back(value);
        else { m_first = value; m_hasFirst = true; }
    }
    void Reserve(size_t count) { if (count > 1) m_rest.reserve(count-1); }
    void Swap(CInlineList& other)
    {
        using std::swap;
        swap(m_first, other.m_first);
        m_rest.swap(other.m_rest);
        swap(m_hasFirst, other.m_hasFirst);
    }
    void Truncate(size_t count)
    {
        ASSERT(count <= GetCount());
        if (!count) { m_first = T(); m_hasFirst = false; m_rest.clear(); }
        else m_rest.resize(count-1);
    }
};

// Metadata only. Raster images remain weak references governed by existing MRUs.
struct DrawReuseBudget
{
    size_t limit, bytes, peakBytes, entries;
    unsigned __int64 hits, misses, machineAllocations, drawAllocations, keyAllocations;
    DrawReuseBudget() : limit(32*1024*1024), bytes(0), peakBytes(0), entries(0),
        hits(0), misses(0), machineAllocations(0), drawAllocations(0), keyAllocations(0) {}
};
struct DrawReuseLease
{
    boost::shared_ptr<DrawReuseBudget> budget;
    enum { CHARGE = 4096 };
    explicit DrawReuseLease(const boost::shared_ptr<DrawReuseBudget>& b) : budget(b)
    {
        budget->bytes+=CHARGE; ++budget->entries;
        budget->peakBytes=max(budget->peakBytes,budget->bytes);
    }
    ~DrawReuseLease() { budget->bytes-=CHARGE; --budget->entries; }
};
struct PreparedLineDraw;

class CLine
{
    friend class CSubtitle;
    CInlineList<SharedPtrCWord> m_words;
    boost::shared_ptr<PreparedLineDraw> m_drawPlan;
    CLine(const CLine&);
    CLine& operator=(const CLine&);
public:
    CLine() {}
    int m_width, m_ascent, m_descent, m_borderX, m_borderY;

    virtual ~CLine();

    void Empty() { m_drawPlan.reset(); m_words.Truncate(0); }
    void Compact();

    int GetWordCount();
    void AddWord2Tail(const SharedPtrCWord& words);
    bool IsEmpty();

    CRectCoor2 PaintAll(CompositeDrawItemList* output, const CRectCoor2& clipRect, 
        const CPointCoor2& margin,
        const SharedPtrCClipperPaintMachine &clipper, 
        CPoint p, const CPoint& org, const int time, const int alpha, POSITION outputPos = NULL,
        const boost::shared_ptr<DrawReuseBudget>& budget = boost::shared_ptr<DrawReuseBudget>());
};

// Immutable layout only. Effects, clipping and tag evaluation remain per-frame.
// Prototypes are never painted or modified; rendering gets independent copies.
struct PreparedSubtitleLayout
{
    struct Line
    {
        std::vector<SharedPtrCWord> words;
        int width, ascent, descent, borderX, borderY;
    };
    std::vector<SharedPtrCWord> rawWords;
    std::vector<Line> lines;
    FwSTSStyle style;
    CRect margin, rect;
    CSize size;
    int alignment, wrap, polygon, baseline, ktype, kstart, kend;
    int topBorder, bottomBorder;
    double scaleX, scaleY, targetX, targetY;
    size_t bytes;
    // Separate bounded appearance cache. These words are never restyled in
    // place; replacing the cache preserves every prior owner's word state.
    mutable FwSTSStyle renderedStyle;
    mutable CInlineList<SharedPtrCWord> renderedWords;
    PreparedSubtitleLayout(const FwSTSStyle& initial) : style(initial), renderedStyle(initial) {}
};
typedef boost::shared_ptr<const PreparedSubtitleLayout> SharedPreparedSubtitleLayout;

// Exact context for the conservative prepared event replay. Named resets and
// geometry-changing transforms are excluded by the operation dependency check.
struct PreparedEventContext
{
    STSStyle sourceStyle;
    CString effect;
    CRect margins;
    CSize screen;
    double par;
    int parType, wrap;
    bool scaledBorder;
    PreparedEventContext(const STSStyle& style) : sourceStyle(style) {}
};

class CSubtitle
{
    CInlineList<CLine*> m_lines;
    CLine m_inlineLine;
    Effect m_inlineEffect;
    bool m_inlineEffectUsed;
    CSubtitle(const CSubtitle&);
    CSubtitle& operator=(const CSubtitle&);
    int GetFullWidth();
    int GetFullLineWidth(size_t pos);
    int GetWrapWidth(size_t pos, int maxwidth);
    CLine* GetNextLine(size_t& pos, int maxwidth);

public:
    int                      m_scrAlignment;
    int                      m_wrapStyle;
    bool                     m_fAnimated;
    bool                     m_fAnimated2; //If this Subtitle has animate effect
    int                      m_relativeTo;
    Effect*                  m_effects[EF_NUMBEROFEFFECTS];
    CInlineList<SharedPtrCWord> m_words;
    SharedPtrCClipper        m_pClipper;
    CRect                    m_rect, m_clip;
    int                      m_topborder, m_bottomborder;
    bool                     m_clipInverse;
    double                   m_scalex, m_scaley;
    double                   m_target_scale_x, m_target_scale_y;
    POSITION                 m_cachePosition;
    int                      m_reuseFrom, m_reuseUntil, m_reuseDelay;
    size_t                   m_reuseCharge;
    CStringW                 m_sourceText;
    boost::shared_ptr<const PreparedEventContext> m_preparedContext;
    int                      m_hard_position_level;//-1: for no style override at all, others see @AssCmdPosLevel
public:
    CSubtitle();
    virtual ~CSubtitle();
    virtual void Empty();
    Effect* CreateEffect(eftype type);

    void CreateClippers(CSize size1, const CSizeCoor2& size_scale_to);

    void MakeLines(CSize size, CRect marginRect);
    SharedPreparedSubtitleLayout PrepareLayout(size_t available, CSize size,
        const CRect& margin, int polygon, int baseline) const;
    bool ReuseLayout(const PreparedSubtitleLayout& layout, const FwSTSStyle& style,
        CSize size, const CRect& margin, int polygon, int baseline,
        int ktype, int kstart, int kend);
    void UpdateLayoutAppearance(const PreparedSubtitleLayout& layout, const FwSTSStyle& style);
    size_t PrepareStaticDrawing(size_t available, size_t sourceCharacters);

    POSITION GetHeadLinePosition();
    CLine* GetNextLine(POSITION& pos);
};

// Empty event storage only: no words, clippers, effects or vector capacity
// survive recycling. Each renderer owns its pool and clears it on invalidation.
class CSubtitlePool
{
    enum { MAX_ITEMS = (1024*1024 - 64) / (sizeof(CSubtitle) + sizeof(CSubtitle*)) };
    CSubtitle* m_items[MAX_ITEMS];
    size_t m_count;
    unsigned __int64 m_hits, m_misses;
    CSubtitlePool(const CSubtitlePool&);
    CSubtitlePool& operator=(const CSubtitlePool&);
public:
    CSubtitlePool() : m_count(0), m_hits(0), m_misses(0) {}
    ~CSubtitlePool() { Clear(); }
    CSubtitle* Acquire();
    void Recycle(CSubtitle* subtitle);
    void Clear();
    static size_t GetCapacity() { return MAX_ITEMS; }
    size_t GetCount() const { return m_count; }
    size_t GetRetainedBytes() const { return sizeof(*this) + m_count * sizeof(CSubtitle); }
    unsigned __int64 GetHits() const { return m_hits; }
    unsigned __int64 GetMisses() const { return m_misses; }
};

struct CSubtitle2
{
    CSubtitle2():s(NULL){}

    CSubtitle2(CSubtitle* s_,const CRectCoor2& clipRect_, const CPoint& org_, const CPoint& org2_
        , const CPoint& p_, int alpha_, int time_)
        : s(s_), clipRect(clipRect_), org(org_), org2(org2_), p(p_), alpha(alpha_), time(time_)
    {

    }

    CSubtitle       *s;
    const CRectCoor2 clipRect;
    const CPoint     org;
    const CPoint     org2;
    const CPoint     p;
    int              alpha;
    int              time;
};

typedef CAtlList<CSubtitle2> CSubtitle2List;

class CScreenLayoutAllocator
{
    typedef struct
    {
        CRect r;
        int segment, entry, layer;
    } SubRect;

    CAtlList<SubRect> m_subrects;

public:
    virtual void Empty();

    void AdvanceToSegment(int segment, const CAtlArray<int>& sa);
    CRect AllocRect(CSubtitle* s, int segment, int entry, int layer, int collisions);
};

[uuid("537DCACA-2812-4a4f-B2C6-1A34C17ADEB0")]
class CRenderedTextSubtitle : public CSubPicProviderImpl, public ISubStream, public ISubPicProviderEx2, public CSimpleTextSubtitle
{
public:
    enum AssCmdType
    {
        CMD_1c = 0,
        CMD_2c,
        CMD_3c,
        CMD_4c,
        CMD_1a,
        CMD_2a,
        CMD_3a,
        CMD_4a,
        CMD_alpha,
        CMD_an,
        CMD_a,
        CMD_blur,
        CMD_bord,
        CMD_be,
        CMD_b,
        CMD_clip,
        CMD_iclip,
        CMD_c,
        CMD_fade,
        CMD_fad,
        CMD_fax,
        CMD_fay,
        CMD_fe,
        CMD_fn,
        CMD_frx,
        CMD_fry,
        CMD_frz,
        CMD_fr,
        CMD_fscx,
        CMD_fscy,
        CMD_fsc,
        CMD_fsp,
        CMD_fs,
        CMD_i,
        CMD_kt,
        CMD_kf,
        CMD_K,
        CMD_ko,
        CMD_k,
        CMD_move,
        CMD_org,
        CMD_pbo,
        CMD_pos,
        CMD_p,
        CMD_q,
        CMD_r,
        CMD_shad,
        CMD_s,
        CMD_t,
        CMD_u,
        CMD_xbord,
        CMD_xshad,
        CMD_ybord,
        CMD_yshad,
        CMD_COUNT
    };
    static const int MIN_CMD_LENGTH = 1;//c etc
    static const int MAX_CMD_LENGTH = 5;//alpha, iclip, xbord, xshad, ybord, yshad
    static CAtlMap<CStringW, AssCmdType, CStringElementTraits<CStringW>> m_cmdMap;
    static AssCmdType LookupAssCommand(const WCHAR* text, int length, int& commandLength);

    static CAtlArray<AssCmdPosLevel> m_cmd_pos_level;

    struct AssTag;
    // Lazy packed nodes. Empty nested programs occupy one pointer, and nodes
    // never move while a cached program or an evaluator retains their address.
    class AssTagList
    {
        struct Header { AssTag* head; };
        struct Storage;
        Header* m_storage;
        AssTagList(const AssTagList&);
        AssTagList& operator=(const AssTagList&);
    public:
        AssTagList() : m_storage(NULL) {}
        ~AssTagList();
        size_t GetCount() const;
        size_t GetCapacity() const;
        size_t GetStorageBytes() const;
        size_t GetBlockCount() const;
        bool IsEmpty() const { return GetCount()==0; }
        POSITION AddTail();
        __forceinline POSITION GetHeadPosition() const { return m_storage ? reinterpret_cast<POSITION>(m_storage->head) : NULL; }
        AssTag& GetAt(POSITION position);
        const AssTag& GetAt(POSITION position) const;
        AssTag& GetNext(POSITION& position);
        const AssTag& GetNext(POSITION& position) const;
    };
    typedef ::boost::shared_ptr<const AssTagList> SharedPtrConstAssTagList;
    // One owned string stays inline. The optional allocation owns both the
    // capacity header and uninitialized slots; only live arguments are built.
    class AssTagParams
    {
        struct Spill;
        CStringW m_first;
        size_t m_count;
        Spill* m_spill;
        AssTagParams(const AssTagParams&);
        AssTagParams& operator=(const AssTagParams&);
        const CStringW& SpillAt(size_t index) const;
    public:
        AssTagParams() : m_count(0),m_spill(NULL) {}
        ~AssTagParams();
        bool IsEmpty() const { return !m_count; }
        size_t GetCount() const { return m_count; }
        size_t GetCapacity() const;
        size_t GetStorageBytes() const;
        __forceinline const CStringW& operator[](size_t index) const
        { ASSERT(index<m_count);return index ? SpillAt(index-1) : m_first; }
        __forceinline CStringW& operator[](size_t index)
        { return const_cast<CStringW&>(static_cast<const AssTagParams&>(*this)[index]); }
        void Reserve(size_t count);
        void Add(const CStringW& value);
        void Truncate(size_t count);
    };
    struct AssTag
    {
        union Number { double real; long integer; };
        AssTag* next;
        AssTag() : next(NULL) {}
        AssCmdType cmdType;
        AssTagParams strParams;
        AssTagList embeded;
        Number firstNumber;
        boost::scoped_array<Number> extraNumbers;
        void AllocateNumbers(size_t count)
        {
            ASSERT(count > 0 && count <= 7);
            if (count > 1) extraNumbers.reset(new Number[count - 1]);
        }
        Number& GetNumber(size_t i) { return i == 0 ? firstNumber : extraNumbers[i - 1]; }
        const Number& GetNumber(size_t i) const { return i == 0 ? firstNumber : extraNumbers[i - 1]; }
    };
    struct ParsedTagCacheStats
    {
        unsigned __int64 hits, misses;
        size_t bytes, peakBytes, entries;
    };
public:
    static std::size_t SetMaxCacheSize(std::size_t max_cache_size);
    // Internal renderer diagnostics/control; this does not change the COM API.
    ParsedTagCacheStats GetParsedTagCacheStats() const;
    struct ScalarParameterStats { unsigned __int64 hits, misses; size_t entries, bytes; };
    ScalarParameterStats GetScalarParameterCacheStats() const;
    void SetScalarParameterCacheEnabled(bool enabled);
    DrawReuseBudget GetDrawReuseStats() const { return *m_drawReuse; }
    void SetDrawReuseCacheLimit(size_t bytes) { m_drawReuse->limit=min(bytes,size_t(32*1024*1024)); }
private:
    boost::shared_ptr<DrawReuseBudget> m_drawReuse;
public:
    struct PreparedLayoutStats
    {
        unsigned __int64 hits, builds, fallbacks, updates;
        size_t bytes, peakBytes, entries;
    };
    PreparedLayoutStats GetPreparedLayoutStats() const;
    void SetPreparedLayoutCacheLimit(size_t bytes);
    void SetParsedTagCacheLimit(size_t bytes);
private:
    struct ParsedTagBlock
    {
        int offset, tagEnd, textEnd;
        CStringW text;
        SharedPtrConstAssTagList tags;
    };
    struct ParsedEventTags
    {
        CStringW text;
        std::vector<ParsedTagBlock> blocks;
        size_t bytes;
        SharedPreparedSubtitleLayout layout;
        bool layoutDisabled;
        ParsedEventTags() : bytes(0), layoutDisabled(false) {}
    };
    typedef boost::shared_ptr<ParsedEventTags> SharedParsedEventTags;
    typedef std::map<int, SharedParsedEventTags> ParsedEventTagMap;
    ParsedEventTagMap m_parsedEventTags;
    class ScalarParameterCache;
    boost::scoped_ptr<ScalarParameterCache> m_scalarParameters;
    bool m_scalarParametersEnabled;
    size_t m_parsedTagBytes, m_parsedTagPeakBytes, m_parsedTagLimit;
    unsigned __int64 m_parsedTagHits, m_parsedTagMisses;
    REFERENCE_TIME m_parsedTagTime;
    REFERENCE_TIME m_nextParsedExpiry, m_nextSubtitleExpiry;
    struct OrderedEvent { int idx, layer, readorder; };
    CAtlArray<OrderedEvent> m_orderSource, m_orderSorted;
    bool m_maintenanceCacheEnabled;
    unsigned __int64 m_orderBuilds, m_orderHits, m_parsedExpiryScans, m_subtitleExpiryScans;
public:
    struct MaintenanceStats { unsigned __int64 orderBuilds, orderHits, parsedScans, subtitleScans; };
    MaintenanceStats GetMaintenanceStats() const
    {
        MaintenanceStats value = {m_orderBuilds,m_orderHits,m_parsedExpiryScans,m_subtitleExpiryScans};
        return value;
    }
    void SetMaintenanceCacheEnabled(bool enabled);
    // A synchronous preparation allowance inside RenderEx, never a worker.
    void SetLookaheadBudget(unsigned microseconds) { m_lookaheadBudget = min(microseconds,4000u); }
    struct LookaheadStats { unsigned __int64 attempted, retained, skipped; LONGLONG ticks, largestUnit; };
    LookaheadStats GetLookaheadStats() const { return m_lookaheadStats; }
private:
    static int CompareOrderedEvents(const void* a, const void* b);
    unsigned m_lookaheadBudget;
    bool m_preparingAhead;
    std::vector<BYTE> m_lookaheadAttempted;
    size_t m_lookaheadSegment, m_lookaheadOffset, m_lookaheadSourceSegments, m_lookaheadSourceEntries;
    bool m_lookaheadCursorValid;
    LookaheadStats m_lookaheadStats;
    void PrepareUpcoming(REFERENCE_TIME rt, double fps, LONGLONG renderStart);
    size_t m_layoutBytes, m_layoutPeakBytes, m_layoutLimit;
    unsigned __int64 m_layoutHits, m_layoutBuilds, m_layoutFallbacks, m_layoutUpdates;
    void DropPreparedLayout(ParsedEventTags& event);
    void ClearParsedTags();
    void ExpireParsedTags(REFERENCE_TIME rt, double fps);
    static size_t EstimateTagMemory(const AssTagList& tags, bool root = true);
    static bool HasLayoutAnimation(const AssTagList& tags, bool inTransform = false, unsigned depth = 0);
    SharedPtrConstAssTagList GetParsedSSATags(const CStringW& text);
    CSubtitlePool           m_subtitlePool;
    size_t m_reconstructionCount, m_reconstructionPeak, m_reconstructionLimit;
    size_t m_reconstructionBytes, m_reconstructionPeakBytes;
    unsigned __int64 m_reconstructionHits;
    int m_reconstructionFrom, m_reconstructionUntil;
    bool m_reconstructionStable;
    void ClearReconstructedSubtitles();
    XyIntMap<CSubtitle*>     m_subtitleCache;
    CAtlList<int>            m_subtitleCacheEntry;

    CScreenLayoutAllocator   m_sla;
    bool m_collectDiagnostics;
    RenderDiagnostics m_diagnostics;

    CRectCoor2               m_video_rect;//scaled video rect
    CRectCoor2               m_subtitle_target_rect;//NOT really supported yet

    CSize                    m_size;

    // temp variables, used when parsing the script
    int                      m_time, m_delay;
    int                      m_animStart, m_animEnd;
    double                   m_animAccel;
    int                      m_ktype, m_kstart, m_kend;
    int                      m_nPolygon;
    int                      m_polygonBaselineOffset;
    double                   m_fps;
    int                      m_period;//1000/m_fps
    double                   m_target_scale_x, m_target_scale_y;
    bool                     m_movable;

    static void InitCmdMap();

    void ParseEffect (CSubtitle* sub, const CString& str);
    void ParseString (CSubtitle* sub, CStringW str, const FwSTSStyle& style);
    void ParsePolygon(CSubtitle* sub, const CStringW& str, const FwSTSStyle& style);

    bool ParseSSATag(AssTagList *assTags, const CStringW& str);
    static void CacheTagNumbers(AssTag& tag);
    bool ParseSSATag(CSubtitle* sub, const AssTagList& assTags, STSStyle& style, const STSStyle& org, bool fAnimate = false, bool paintOnly = false);

    bool ParseHtmlTag(CSubtitle* sub, CStringW str, STSStyle& style, STSStyle& org);

    double CalcAnimation(double dst, double src, bool fAnimate);

    bool MatchesPreparedContext(int entry, CSubtitle* sub, const CStringW& sourceText);
    bool UpdatePreparedSubtitle(int entry, CSubtitle* sub, const CStringW& sourceText);
    CSubtitle* GetSubtitle(int entry);
    CSubtitle* GetSubtitle(int entry, CSubtitle* cached, const CStringW& sourceText);
    CSubtitle* CreateSubtitle(int entry, CSubtitle* cached, const CStringW& sourceText);

    void ClearUnCachedSubtitle(CSubtitle2List& sub2List);
    void ShrinkCache();
private:
    static std::size_t s_max_cache_size;
protected:
    virtual void OnChanged();
    virtual void OnSourceLoaded(CTextFile* file, ULONGLONG start);
    boost::scoped_ptr<LibassRenderer> m_libass;
    LibassRenderer::StyleBaseline m_libassOriginalStyles;
    bool m_preferLibass, m_libassFontsLoaded;
    
public:
    CRenderedTextSubtitle(CCritSec* pLock);
    virtual ~CRenderedTextSubtitle();

    virtual void Copy(CSimpleTextSubtitle& sts);
    virtual void Empty();

private:
    /*
     * The layout_size parameter is either derived from
     * valid LayoutRes{X,Y} script headers (both must be valid!)
     * or if those do not exist, equal to the video's storage resolution.
     * Will call Deinit()
     */
    bool Init(const CRectCoor2& video_rect, const CRectCoor2& subtitle_target_rect,
        const SIZE& layout_size);
public:
    void Deinit();
    void SetPreferLibass(bool prefer) { m_preferLibass = prefer; m_movable = !IsUsingLibass(); }
    bool IsUsingLibass() const { return m_preferLibass && m_libass && m_libass->HasSource(); }
    LibassRenderer* GetLibassRenderer() { return m_libass.get(); }
    void SetLibassEmbedded(bool embedded);
    void AddLibassPacket(const BYTE* data, int length, REFERENCE_TIME start, REFERENCE_TIME stop);
    void FlushLibass();
    void LoadLibassGraphFonts(IFilterGraph* graph);
    void EnableDiagnostics(bool enable) { m_collectDiagnostics = enable; m_diagnostics.Reset(); }
    const RenderDiagnostics& GetDiagnostics() const { return m_diagnostics; }
    // Offline-only seams: initialize geometry, then time production parsing or
    // composition. Composite inputs are prepared outside the measured interval.
    HRESULT BenchmarkNativePhase(IXySubRenderFrame** frame, REFERENCE_TIME time,
        bool parseOnly, LONGLONG& ticks, ULONGLONG& cpuTicks);
    const CSubtitlePool& GetSubtitlePool() const { return m_subtitlePool; }
    void SetReconstructionCacheLimit(size_t entries);
    size_t GetReconstructionCacheCount() const { return m_reconstructionCount; }
    size_t GetReconstructionCachePeak() const { return m_reconstructionPeak; }
    size_t GetReconstructionCacheBytes() const { return m_reconstructionBytes; }
    size_t GetReconstructionCachePeakBytes() const { return m_reconstructionPeakBytes; }
    unsigned __int64 GetReconstructionCacheHits() const { return m_reconstructionHits; }

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

    // ISubPicProviderEx2
    STDMETHODIMP Lock();
    STDMETHODIMP Unlock();
    STDMETHODIMP RenderEx(IXySubRenderFrame**subRenderFrame, int spd_type, 
        const RECT& video_rect, const RECT& subtitle_target_rect,
        const SIZE& original_video_size,
        REFERENCE_TIME rt, double fps);

    // ISubPicProviderEx && ISubPicProviderEx2
    STDMETHODIMP_(POSITION) GetStartPosition(REFERENCE_TIME rt, double fps);
    STDMETHODIMP_(POSITION) GetNext(POSITION pos);
    STDMETHODIMP_(REFERENCE_TIME) GetStart(POSITION pos, double fps);
    STDMETHODIMP_(REFERENCE_TIME) GetStop(POSITION pos, double fps);
    STDMETHODIMP_(VOID) GetStartStop(POSITION pos, double fps, /*out*/REFERENCE_TIME &start, /*out*/REFERENCE_TIME &stop);
    STDMETHODIMP_(bool) IsAnimated(POSITION pos);
    STDMETHODIMP Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECTCoor2& bbox);
    STDMETHODIMP RenderEx(SubPicDesc& spd, REFERENCE_TIME rt, double fps, CAtlList<CRectCoor2>& rectList);
    HRESULT ParseScript(REFERENCE_TIME rt, double fps, CSubtitle2List *outputSub2List );
    void DoRender( const SIZECoor2& output_size,
        const POINTCoor2& video_org, const RECTCoor2& margin_rect, 
        const CSubtitle2List& sub2List, 
        CompositeDrawItemListList *compDrawItemListList /*output*/);
    void RenderOneSubtitle(const SIZECoor2& output_size,
        const POINTCoor2& video_org, const RECTCoor2& margin_rect, 
        const CSubtitle2& sub2, 
        CompositeDrawItemList* compDrawItemList /*output*/);
    STDMETHODIMP_(bool) IsColorTypeSupported(int type);
    STDMETHODIMP_(bool) IsMovable();
    STDMETHODIMP_(bool) IsSimple();

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClassID);

    // ISubStream
    STDMETHODIMP_(int) GetStreamCount();
    STDMETHODIMP GetStreamInfo(int i, WCHAR** ppName, LCID* pLCID);
    STDMETHODIMP_(int) GetStream();
    STDMETHODIMP SetStream(int iStream);
    STDMETHODIMP Reload();
};

// Keep hot immutable-program traversal visible to every caller. POSITION names
// the stable tag node itself; links are never changed after publication.
__forceinline const CRenderedTextSubtitle::AssTag& CRenderedTextSubtitle::AssTagList::GetAt(POSITION position) const
{ ASSERT(position);return *reinterpret_cast<const AssTag*>(position); }
__forceinline CRenderedTextSubtitle::AssTag& CRenderedTextSubtitle::AssTagList::GetAt(POSITION position)
{ return const_cast<AssTag&>(static_cast<const AssTagList&>(*this).GetAt(position)); }
__forceinline const CRenderedTextSubtitle::AssTag& CRenderedTextSubtitle::AssTagList::GetNext(POSITION& position) const
{
    const AssTag& tag=GetAt(position);
    position=reinterpret_cast<POSITION>(tag.next);
    return tag;
}
__forceinline CRenderedTextSubtitle::AssTag& CRenderedTextSubtitle::AssTagList::GetNext(POSITION& position)
{ return const_cast<AssTag&>(static_cast<const AssTagList&>(*this).GetNext(position)); }
