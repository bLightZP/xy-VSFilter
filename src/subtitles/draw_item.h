#ifndef __DRAW_ITEM_21D18040_C396_4CA5_BFCE_5616A63F2C56_H__
#define __DRAW_ITEM_21D18040_C396_4CA5_BFCE_5616A63F2C56_H__

#include <WTypes.h>
#include "RTS.h"
#include <atlcoll.h>
#include <boost/shared_ptr.hpp>
#include "xy_bitmap.h"

class OverlayPaintMachine;
typedef ::boost::shared_ptr<OverlayPaintMachine> SharedPtrOverlayPaintMachine;

class CClipperPaintMachine;
typedef ::boost::shared_ptr<CClipperPaintMachine> SharedPtrCClipperPaintMachine;

class DrawItemHashKey;
typedef ::boost::shared_ptr<DrawItemHashKey> SharedPtrDrawItemHashKey;
struct DrawItem;
typedef ::boost::shared_ptr<DrawItem> SharedPtrDrawItem;

struct DrawItem
{
public:
    SharedPtrOverlayPaintMachine  overlay_paint_machine;
    CRect clip_rect;
    SharedPtrCClipperPaintMachine clipper;
    int   xsub;
    int   ysub;
    DWORD switchpts[6];
    bool  fBody;
    bool  fBorder;

    bool  use_addition_draw;

    SharedPtrDrawItemHashKey m_key;
    boost::shared_ptr<DrawReuseLease> retention;
    bool boundsSealed;
    CRectCoor2 sealedBounds;
    DrawItem() : boundsSealed(false) {}
    bool SealBounds();
public:
    CRectCoor2 GetDirtyRect();
    static CRectCoor2 Draw        ( XyBitmap *bitmap, DrawItem& draw_item, const CRectCoor2& clip_rect, AlphaMaskBuffer& buffer );
    static CRectCoor2 Draw(XyBitmap* bitmap, DrawItem& item, const CRectCoor2& clip,
        AlphaMaskBuffer& buffer, bool addition);
    static CRectCoor2 AlphaBltDraw( XyBitmap *bitmap, DrawItem& draw_item, const CRectCoor2& clip_rect, AlphaMaskBuffer& buffer );
    static CRectCoor2 AdditionDraw( XyBitmap *bitmap, DrawItem& draw_item, const CRectCoor2& clip_rect, AlphaMaskBuffer& buffer );
    const SharedPtrDrawItemHashKey& GetHashKey();

    static SharedPtrDrawItem CreateDrawItem(const SharedPtrOverlayPaintMachine& overlay_paint_machine,
        const CRect& clipRect,
        const SharedPtrCClipperPaintMachine &clipper,
        int xsub, int ysub,
        const DWORD* switchpts, bool fBody, bool fBorder);
};

typedef CAtlList<DrawItem> DrawItemList;

typedef CAtlList<DrawItemList> DrawItemListList;


// A frame-scoped pin keeps the normal fast strong overlay access during drawing.
// Copies share the pin count; no pin is stored in a retained line plan.
class FrameOverlayPin
{
    SharedPtrOverlayPaintMachine machine;
public:
    FrameOverlayPin() {}
    FrameOverlayPin(const FrameOverlayPin& other);
    FrameOverlayPin& operator=(const FrameOverlayPin& other);
    ~FrameOverlayPin();
    void Set(const SharedPtrOverlayPaintMachine& value);
};

struct CompositeDrawItem
{
public:
    SharedPtrDrawItem shadow;
    SharedPtrDrawItem outline;
    SharedPtrDrawItem body;
    bool shadowAddition, outlineAddition, bodyAddition;
    FrameOverlayPin shadowPin, outlinePin, bodyPin;
    void PinOverlays();
    CompositeDrawItem() : shadowAddition(false), outlineAddition(false), bodyAddition(false) {}
public:
    static CRectCoor2 GetDirtyRect( CompositeDrawItem& item );

    static void Draw(XySubRenderFrame**output, CompositeDrawItemListList& compDrawItemListList,
        RenderDiagnostics* diagnostics = NULL);
};

// Event-local draw slots: most events have one word, so keep its three layers
// inline instead of allocating an ATL node block. POSITION stores a one-based
// index and survives appends. Element addresses may move while appending; the
// grouping tree captures them only after DoRender has finished every event.
class CompositeDrawItemList
{
    CInlineList<CompositeDrawItem> m_items;
    CompositeDrawItemList(const CompositeDrawItemList&);
    CompositeDrawItemList& operator=(const CompositeDrawItemList&);
public:
    CompositeDrawItemList() {}
    size_t GetCount() const { return m_items.GetCount(); }
    size_t GetCapacity() const { return m_items.GetCapacity(); }
    POSITION GetHeadPosition() const { return GetCount() ? reinterpret_cast<POSITION>(1) : NULL; }
    POSITION AddTail()
    {
        m_items.AddTail(CompositeDrawItem());
        return reinterpret_cast<POSITION>(GetCount());
    }
    CompositeDrawItem& GetAt(POSITION pos)
    {
        ASSERT(pos && reinterpret_cast<size_t>(pos) <= GetCount());
        return m_items[reinterpret_cast<size_t>(pos)-1];
    }
    CompositeDrawItem& GetNext(POSITION& pos)
    {
        CompositeDrawItem& item = GetAt(pos);
        const size_t next = reinterpret_cast<size_t>(pos)+1;
        pos = next <= GetCount() ? reinterpret_cast<POSITION>(next) : NULL;
        return item;
    }
};

struct GroupedDrawItems
{
public:
    CInlineList<SharedPtrDrawItem> draw_item_list;
    CInlineList<BYTE> addition_flags;
    CRectCoor2 clip_rect;
public:
    void Draw(SharedPtrXyBitmap *bitmap, int *bitmap_identity_num, AlphaMaskBuffer& buffer);

    void CreateHashKey(GroupedDrawItemsHashKey *key);
};

#endif // __DRAW_ITEM_21D18040_C396_4CA5_BFCE_5616A63F2C56_H__
