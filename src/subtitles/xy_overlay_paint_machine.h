#ifndef __XY_OVERLAY_PAINT_MACHINE_47C522B7_4317_441C_A536_5F667DCB0141_H__
#define __XY_OVERLAY_PAINT_MACHINE_47C522B7_4317_441C_A536_5F667DCB0141_H__

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
struct DrawReuseBudget;
class CWord;
struct DrawItem;
class DrawItemHashKey;
typedef ::boost::shared_ptr<DrawItemHashKey> SharedPtrDrawItemHashKey;
typedef ::boost::shared_ptr<CWord> SharedPtrCWord;

struct Overlay;
typedef ::boost::shared_ptr<Overlay> SharedPtrOverlay;

class OverlayPaintMachine;
typedef ::boost::shared_ptr<OverlayPaintMachine> SharedPtrOverlayPaintMachine;

class OverlayKey;
typedef ::boost::shared_ptr<OverlayKey> SharedPtrOverlayKey;

class CWordPaintMachine
{
    struct AllocationTag {};
public:
    explicit CWordPaintMachine(AllocationTag) {}
    enum LAYER 
    {
        SHADOW = 0
        , OUTLINE
        , BODY
    };
public:
    static void CreatePaintMachines(const SharedPtrCWord& word
        , const CPointCoor2& shadow_pos, const CPointCoor2& outline_pos, const CPointCoor2& body_pos
        , const CPointCoor2& org
        , SharedPtrOverlayPaintMachine *shadow, SharedPtrOverlayPaintMachine *outline, SharedPtrOverlayPaintMachine *body,
        const boost::shared_ptr<DrawReuseBudget>& budget = boost::shared_ptr<DrawReuseBudget>());

    static void PaintBody(const SharedPtrCWord& word, const CPointCoor2& p, const CPointCoor2& org, SharedPtrOverlay* overlay);

    void Paint(LAYER layer, SharedPtrOverlay* overlay);
    const SharedPtrOverlayKey& GetHashKey(LAYER layer);
    const SharedPtrDrawItemHashKey& GetDrawItemHashKey(const DrawItem& item);
private:
    CWordPaintMachine(){}

    void PaintBody(const SharedPtrCWord& word, const CPointCoor2& p, SharedPtrOverlay* overlay, const SharedPtrOverlayKey& key);
    void PaintOutline(const SharedPtrCWord& word, const CPointCoor2& p, SharedPtrOverlay* overlay, const SharedPtrOverlayKey& key);
    void PaintShadow(const SharedPtrCWord& word, const CPointCoor2& p, SharedPtrOverlay* overlay, const SharedPtrOverlayKey& key);

    SharedPtrOverlayKey CreateBodyOverlayHashKey(const SharedPtrCWord& word, const CPointCoor2& p);
    SharedPtrOverlayKey CreateOutlineOverlayHashKey(const SharedPtrCWord& word, const CPointCoor2& p);
    SharedPtrOverlayKey CreateShadowOverlayHashKey(const SharedPtrCWord& word, const CPointCoor2& p);

    boost::shared_ptr<DrawReuseBudget> m_budget;
    SharedPtrCWord m_word;
    CPointCoor2 m_shadow_pos, m_outline_pos, m_body_pos;
    CPointCoor2 m_trans_org;

    SharedPtrOverlayKey m_shadow_key, m_border_key, m_body_key;
};

typedef ::boost::shared_ptr<CWordPaintMachine> SharedCWordPaintMachine;

class OverlayPaintMachine
{
public:
    OverlayPaintMachine(const SharedCWordPaintMachine& inner_paint_machine, CWordPaintMachine::LAYER layer)
        : m_inner_paint_machine(inner_paint_machine)
        , m_layer(layer), m_weakRetention(false), m_framePins(0) {}
 
    void Paint(SharedPtrOverlay* overlay);
    void EnableWeakRetention();
    void BeginFrame();
    void EndFrame();
    CRectCoor2 CalcDirtyRect();//return a 8x8 supersampled rect
    const SharedPtrOverlayKey& GetHashKey();
    const SharedPtrDrawItemHashKey& GetDrawItemHashKey(const DrawItem& item);
private:
    SharedCWordPaintMachine m_inner_paint_machine;
    CWordPaintMachine::LAYER m_layer;

    SharedPtrOverlay m_overlay;
    boost::weak_ptr<Overlay> m_weakOverlay;
    bool m_weakRetention;
    unsigned m_framePins;
};


#endif // __XY_OVERLAY_PAINT_MACHINE_47C522B7_4317_441C_A536_5F667DCB0141_H__
