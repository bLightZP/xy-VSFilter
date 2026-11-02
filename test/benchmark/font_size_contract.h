// Included after the common render/actual-DLL lifecycle helpers.
class PrivateCffFont {
    const wchar_t* path;
public:
    explicit PrivateCffFont(const wchar_t* file) : path(file) {
        Require(AddFontResourceExW(path,FR_PRIVATE,NULL)>0,"Cannot load private CFF regression font");
    }
    ~PrivateCffFont() { RemoveFontResourceExW(path,FR_PRIVATE,NULL); }
};

class FontPathProbe : public CText {
public:
    FontPathProbe(const STSStyle& style,const wchar_t* text) : CText(FwSTSStyle(style),text,0,0,0) {}
    bool Path(PathData& path) { return CreatePath(&path); }
    bool HasCachedMetrics() const {
        TextInfoCacheKey key;key.m_str_id=m_str.GetId();key.m_style=m_style;key.UpdateHashValue();
        return CacheManager::GetTextInfoCache()->Lookup(key)!=NULL;
    }
};

static CRect FontBounds(const PathData& path)
{
    Require(path.mPathPoints>0,"Large font has no glyph outline");
    CRect box(INT_MAX,INT_MAX,INT_MIN,INT_MIN);
    for (int i=0;i<path.mPathPoints;++i) {
        box.left=min(box.left,path.mpPathPoints[i].x);box.right=max(box.right,path.mpPathPoints[i].x);
        box.top=min(box.top,path.mpPathPoints[i].y);box.bottom=max(box.bottom,path.mpPathPoints[i].y);
    }
    return box;
}

static int FontSizeContract(const wchar_t* fontFile)
{
    PrivateCffFont resource(fontFile);
    CCritSec lock; CRenderedTextSubtitle renderer(&lock); // initialize native GDI state
    for (const wchar_t* face : {L"Kaushan Script",L"Arial"}) {
        STSStyle baseStyle;baseStyle.fontName=face;baseStyle.fontSize=9600;baseStyle.fontWeight=700;
        baseStyle.fontSpacing=0;baseStyle.fontScaleX=baseStyle.fontScaleY=100;
        STSStyle largeStyle=baseStyle;largeStyle.fontSize=19200;
        FontPathProbe a(baseStyle,L"HH"),b(largeStyle,L"HH"),warm(largeStyle,L"HH");
        PathData pa,pb,pwarm;
        Require(a.Path(pa) && b.Path(pb) && warm.Path(pwarm),"Large native font path failed");
        const CRect ba=FontBounds(pa),bb=FontBounds(pb);
        Require(abs(bb.Width()-2*ba.Width())<2*ba.Width()/100+4 &&
            abs(bb.Height()-2*ba.Height())<2*ba.Height()/100+4,"Font clamp changed authored glyph size");
        Require(abs(b.m_width-2*a.m_width)<2*a.m_width/100+4 &&
            abs(b.m_ascent-2*a.m_ascent)<2*a.m_ascent/100+4 &&
            abs(b.m_descent-2*a.m_descent)<2*a.m_descent/100+4,"Font clamp changed text metrics");
        Require(pb==pwarm && b.m_width==warm.m_width,"Warm font cache changed geometry");
        // GDI-space spacing stays independent of realization scaling. Two Hs
        // add one gap to the outline and two advances to the legacy metrics.
        largeStyle.fontSpacing=640;
        FontPathProbe spaced(largeStyle,L"HH");PathData ps;
        Require(spaced.Path(ps),"Spaced large font path failed");
        Require(abs(FontBounds(ps).Width()-bb.Width()-640)<=2 &&
            abs(spaced.m_width-b.m_width-160)<=1,"Font scaling also scaled authored spacing");
        largeStyle.fontScaleX=125;largeStyle.fontScaleY=75;
        FontPathProbe scaled(largeStyle,L"HH");
        Require(abs(scaled.m_width-spaced.m_width*1.25)<=2 &&
            abs(scaled.m_ascent-spaced.m_ascent*.75)<=2,"Nonuniform font scale lost normalized metrics");
    }
    STSStyle rejected;rejected.fontSize=1e30;
    FontPathProbe invalid(rejected,L"HH");PathData bad;
    Require(invalid.m_width==0 && invalid.m_ascent==0 && invalid.m_descent==0 && !invalid.Path(bad),
        "Failed font realization exposed invalid metrics/outline");
    Require(!invalid.HasCachedMetrics(),"Failed font measurement entered the metrics cache");
    STSStyle valid;valid.fontSize=4800;
    FontPathProbe recovered(valid,L"HH");PathData good;
    Require(recovered.Path(good) && FontBounds(good).Width()>0,"Failed font poisoned subsequent GDI state");
    printf("font_size_contract=passed (CFF/TrueType, glyph size, metrics, spacing, nonuniform scale, cache and failure recovery)\n");
    return 0;
}

static int FontSizeFilterContract(const wchar_t* dll,const wchar_t* fixture,const wchar_t* fontFile,bool xy)
{
    PrivateCffFont resource(fontFile);
    // Reuse the actual-DLL cold/warm, geometry, backward-seek, 128 retained-frame
    // and reopen/teardown schedule. This fixture has text rather than triangles.
    const int result=OutlineFilterContract(dll,fixture,xy);
    printf("font_size_filter_contract=passed (%s)\n",xy ? "xy" : "vs CSRI");
    return result;
}
