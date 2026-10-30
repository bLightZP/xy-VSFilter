#include "stdafx.h"
#include <memory>
#include <new>
#include "draw_item.h"
#include <vector>
#include <algorithm>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include "xy_overlay_paint_machine.h"
#include "xy_clipper_paint_machine.h"
#include "../SubPic/ISubPic.h"
#include "xy_bitmap.h"

#if ENABLE_XY_LOG_TRACE_DRAW
#  define TRACE_DRAW(msg) XY_LOG_TRACE(msg)
#else
#  define TRACE_DRAW(msg)
#endif

using namespace std;

//////////////////////////////////////////////////////////////////////////
//
// DrawItem
// 
CRectCoor2 DrawItem::GetDirtyRect()
{
    if (boundsSealed) return sealedBounds;
    CRectCoor2 r;
    if(!switchpts || !fBody && !fBorder) return(r);

    ASSERT(overlay_paint_machine);
    CRectCoor2 overlay_rect = overlay_paint_machine->CalcDirtyRect();

    // Remember that all subtitle coordinates are specified in 1/8 pixels
    // (x+4)>>3 rounds to nearest whole pixel.
    // ??? What is xsub, ysub, mOffsetX and mOffsetY ?
    int x = (xsub + overlay_rect.left + 4)>>3;
    int y = (ysub + overlay_rect.top + 4)>>3;
    int w = overlay_rect.Width()>>3;
    int h = overlay_rect.Height()>>3;
    r = clip_rect & CRect(x, y, x+w, y+h);
    r &= clipper->CalcDirtyRect();

    //expand the rect, so that it is possible to perform chroma sub-sampling
    r.left &= ~1;
    r.right = (r.right + 1)&~1;
    r.top &= ~1;
    r.bottom = (r.bottom + 1)&~1;
    return r;
}

bool DrawItem::SealBounds()
{
    SharedPtrOverlay overlay;
    overlay_paint_machine->Paint(&overlay);
    if (!overlay || overlay->mOverlayWidth<=0 || overlay->mOverlayHeight<=0) return false;
    sealedBounds=GetDirtyRect(); boundsSealed=true;
    return true;
}

CRectCoor2 DrawItem::Draw( XyBitmap* bitmap, DrawItem& draw_item, const CRectCoor2& clip_rect, AlphaMaskBuffer& buffer )
{

    return Draw(bitmap,draw_item,clip_rect,buffer,draw_item.use_addition_draw);
}

CRectCoor2 DrawItem::Draw(XyBitmap* bitmap, DrawItem& draw_item,
    const CRectCoor2& clip_rect, AlphaMaskBuffer& buffer, bool addition)
{
    // PF 20180411 disable use_addition=true for planar
    // because bitmap->type == XyBitmap::PLANNA is not supported and
    // asserts FATAL in AdditionDraw (use case: Avisynth TextSub plugin for YV12)
    bool PLANAR = (bitmap->type == XyBitmap::PLANNA);

    return !PLANAR && addition ? AdditionDraw(bitmap, draw_item, clip_rect, buffer)
                                       : AlphaBltDraw(bitmap, draw_item, clip_rect, buffer);
}

CRectCoor2 DrawItem::AlphaBltDraw( XyBitmap* bitmap, DrawItem& draw_item, const CRectCoor2& clip_rect, AlphaMaskBuffer& buffer )
{
    CRect result;
    SharedPtrGrayImage2 alpha_mask;
    draw_item.clipper->Paint(&alpha_mask);

    SharedPtrOverlay overlay;
    ASSERT(draw_item.overlay_paint_machine);
    draw_item.overlay_paint_machine->Paint(&overlay);

    if (Rasterizer::DrawSingleColorFused(bitmap,overlay,draw_item.clip_rect & clip_rect,alpha_mask.get(),
        draw_item.xsub,draw_item.ysub,draw_item.switchpts,draw_item.fBody,draw_item.fBorder,&result)) return result;

    BYTE* alpha = Rasterizer::CompositeAlphaMask(overlay, draw_item.clip_rect & clip_rect, alpha_mask.get(),
        draw_item.xsub, draw_item.ysub, draw_item.switchpts, draw_item.fBody, draw_item.fBorder,
        &result, buffer);
    if (!alpha) return result;

    Rasterizer::Draw(bitmap, overlay, result, alpha,
        draw_item.xsub, draw_item.ysub, draw_item.switchpts, draw_item.fBody, draw_item.fBorder);
    return result;
}

CRectCoor2 DrawItem::AdditionDraw( XyBitmap *bitmap, DrawItem& draw_item, const CRectCoor2& clip_rect, AlphaMaskBuffer& buffer )
{
    TRACE_DRAW("AdditionDraw");
    CRect result;
    SharedPtrGrayImage2 alpha_mask;
    draw_item.clipper->Paint(&alpha_mask);

    SharedPtrOverlay overlay;
    ASSERT(draw_item.overlay_paint_machine);
    draw_item.overlay_paint_machine->Paint(&overlay);

    BYTE* alpha = Rasterizer::CompositeAlphaMask(overlay, draw_item.clip_rect & clip_rect, alpha_mask.get(),
        draw_item.xsub, draw_item.ysub, draw_item.switchpts, draw_item.fBody, draw_item.fBorder,
        &result, buffer);
    if (!alpha) return result;

    Rasterizer::AdditionDraw(bitmap, overlay, result, alpha,
        draw_item.xsub, draw_item.ysub, draw_item.switchpts, draw_item.fBody, draw_item.fBorder);
    return result;
}

SharedPtrDrawItem DrawItem::CreateDrawItem( const SharedPtrOverlayPaintMachine& overlay_paint_machine, const CRect& clipRect,
    const SharedPtrCClipperPaintMachine &clipper, int xsub, int ysub, const DWORD* switchpts, bool fBody, bool fBorder )
{
    SharedPtrDrawItem result      = boost::make_shared<DrawItem>();
    result->overlay_paint_machine = overlay_paint_machine;
    result->clip_rect             = clipRect;
    result->clipper               = clipper;
    result->xsub                  = xsub;
    result->ysub                  = ysub;

    memcpy(result->switchpts, switchpts, sizeof(result->switchpts));
    result->fBody   = fBody;
    result->fBorder = fBorder;

    result->use_addition_draw = false;

    result->m_key = overlay_paint_machine->GetDrawItemHashKey(*result);

    return result;
}

const SharedPtrDrawItemHashKey& DrawItem::GetHashKey()
{
    return m_key;
}

//////////////////////////////////////////////////////////////////////////
//
// CompositeDrawItem
// 

FrameOverlayPin::FrameOverlayPin(const FrameOverlayPin& other) { Set(other.machine); }
FrameOverlayPin& FrameOverlayPin::operator=(const FrameOverlayPin& other) { Set(other.machine); return *this; }
FrameOverlayPin::~FrameOverlayPin() { if (machine) machine->EndFrame(); }
void FrameOverlayPin::Set(const SharedPtrOverlayPaintMachine& value)
{
    if (machine==value) return;
    if (machine) machine->EndFrame();
    machine=value;
    if (machine) machine->BeginFrame();
}
void CompositeDrawItem::PinOverlays()
{
    if (shadow) shadowPin.Set(shadow->overlay_paint_machine);
    if (outline) outlinePin.Set(outline->overlay_paint_machine);
    if (body) bodyPin.Set(body->overlay_paint_machine);
}

CRectCoor2 CompositeDrawItem::GetDirtyRect( CompositeDrawItem& item )
{
    CRectCoor2 result;
    if (item.shadow)
    {
        result |= item.shadow->GetDirtyRect();
    }
    if (item.outline)
    {
        result |= item.outline->GetDirtyRect();
    }
    if (item.body)
    {
        result |= *item.body->GetDirtyRect();
    }
    return result;
}

// Frame-owned rectangle fragments: common short lists need no heap storage.
// Draw-item arrays are sized once before pointers to their elements are stored.
class FrameGroupIds
{
    enum { INLINE_IDS = 8 };
    int m_inline[INLINE_IDS];
    std::vector<int> m_spill;
    size_t m_count;
public:
    FrameGroupIds() : m_count(0) {}
    size_t GetCount() const { return m_count; }
    int operator[](size_t i) const
    { ASSERT(i < m_count); return i < INLINE_IDS ? m_inline[i] : m_spill[i-INLINE_IDS]; }
    void AddTail(int value)
    {
        if (m_count < INLINE_IDS) m_inline[m_count] = value;
        else m_spill.push_back(value);
        ++m_count;
    }
};

struct CompositeDrawItemEx;
typedef CInlineList<CompositeDrawItemEx*> PCompositeDrawItemExList;

// All of this metadata is owned by the current Draw call. The initial rectangle
// uses its item's inline membership; merged rectangles share lists from a
// frame-owned, address-stable pool rather than shared_ptr control blocks.
struct CompositeDrawItemEx
{
    CompositeDrawItem *item;
    FrameGroupIds rect_id_list;
    PCompositeDrawItemExList initial_members;
};
typedef CAtlList<PCompositeDrawItemExList> FrameMemberLists;

static void AppendMembers(PCompositeDrawItemExList& destination, const PCompositeDrawItemExList& source)
{
    const size_t count = source.GetCount();
    for (size_t i = 0; i < count; ++i) destination.AddTail(source[i]);
}
// Views preserve event-local shadow/outline/body ordering. All metadata lives
// in one frame-owned array, sized before any element addresses are published.
// The views are trivially relocatable by the outer ATL array and own no storage.
struct CompositeDrawItemExVec
{
    CompositeDrawItemEx* items;
    size_t count;
    size_t GetCount() const { return count; }
    CompositeDrawItemEx& operator[](size_t i) { ASSERT(i < count); return items[i]; }
    CompositeDrawItemEx& GetAt(size_t i) { return (*this)[i]; }
};
typedef CAtlArray<CompositeDrawItemExVec> CompositeDrawItemExTree;

class XyRectEx: public CRect
{
public:
    PCompositeDrawItemExList* item_ex_list;

    XyRectEx() : item_ex_list(NULL) {}
    void SetRect(const RECT& rect)
    {
        __super::operator=(rect);
    }
    void SetRect(
        _In_ int x1,
        _In_ int y1,
        _In_ int x2,
        _In_ int y2) throw()
    {
        __super::SetRect(x1,y1,x2,y2);
    }
};

typedef CAtlList<XyRectEx> XyRectExList;

void MergeRects(const XyRectExList& input, XyRectExList* output, FrameMemberLists& members);
void CreateDrawItemExTree( CompositeDrawItemListList& input,
    CAtlArray<CompositeDrawItemEx>* storage,
    CompositeDrawItemExTree *out_draw_item_ex_tree, 
    XyRectExList *out_rect_ex_list );
void DecideDrawMethod( CompositeDrawItemListList& compDrawItemListList, XyRectExList& rect_ex_list );

static void AppendGroupedDraw(GroupedDrawItems& group, const SharedPtrDrawItem& item, bool addition, RenderDiagnostics* stats)
{
    const size_t before = stats ? group.draw_item_list.GetCapacity() : 0;
    group.draw_item_list.AddTail(item);
    group.addition_flags.AddTail(addition);
    if (stats) {
        ++stats->drawItems;
        stats->drawVectorGrowths += group.draw_item_list.GetCapacity() != before;
    }
}

void CompositeDrawItem::Draw( XySubRenderFrame**output, CompositeDrawItemListList& compDrawItemListList,
    RenderDiagnostics* diagnostics )
{
    if (!output)
    {
        return;
    }
    *output = NULL;
    RenderPhaseTimer compositeTimer(diagnostics, RenderDiagnostics::COMPOSITE);

    CAtlArray<CompositeDrawItemEx> draw_item_storage;
    CompositeDrawItemExTree draw_item_ex_tree;
    // Nodes never move, including when more bands are discovered. Only used
    // nodes construct lists; block allocation amortizes the remaining scratch
    // list headers. No frame or cache retains pointers to these memberships.
    FrameMemberLists member_lists(64);
    XyRectExList rect_ex_list;
    {
        RenderPhaseTimer timer(diagnostics, RenderDiagnostics::TREE);
        CreateDrawItemExTree(compDrawItemListList, &draw_item_storage, &draw_item_ex_tree, &rect_ex_list);
        //fix subpixel gap whenever possible
        DecideDrawMethod(compDrawItemListList, rect_ex_list);
    }
    if (diagnostics) diagnostics->inputItems += rect_ex_list.GetCount();

    XyRectExList grouped_rect_exs;
    {
        RenderPhaseTimer timer(diagnostics, RenderDiagnostics::MERGE);
        MergeRects(rect_ex_list, &grouped_rect_exs, member_lists);
    }

    CAtlArray<GroupedDrawItems> grouped_draw_items;

    grouped_draw_items.SetCount(grouped_rect_exs.GetCount());
    if (diagnostics) diagnostics->groups += grouped_rect_exs.GetCount();

    {
        RenderPhaseTimer groupTimer(diagnostics, RenderDiagnostics::GROUP);
        POSITION pos = grouped_rect_exs.GetHeadPosition();
        for(int rect_id=0;pos;rect_id++)
        {
            XyRectEx& item = grouped_rect_exs.GetNext(pos);
            if (diagnostics) diagnostics->memberReferences += item.item_ex_list->GetCount();
            grouped_draw_items.GetAt(rect_id).clip_rect = item;
            size_t drawCount = 0;
            for (size_t member = 0; member < item.item_ex_list->GetCount(); ++member)
            {
                CompositeDrawItemEx* draw_item_ex = (*item.item_ex_list)[member];
                FrameGroupIds& ids = draw_item_ex->rect_id_list;
                // Group IDs are visited in order; only the last ID can repeat.
                if (!ids.GetCount() || ids[ids.GetCount()-1] != rect_id)
                {
                    ids.AddTail(rect_id);
                    if (draw_item_ex->item->shadow) ++drawCount;
                    if (draw_item_ex->item->outline) ++drawCount;
                    if (draw_item_ex->item->body) ++drawCount;
                    if (diagnostics) ++diagnostics->uniqueMembers;
                }
            }
            // One allocation at most for the final spill; appending retains order.
            grouped_draw_items[rect_id].draw_item_list.Reserve(drawCount);
            grouped_draw_items[rect_id].addition_flags.Reserve(drawCount);
        }
        for (unsigned i=0;i<draw_item_ex_tree.GetCount();i++)
        {
            CompositeDrawItemExVec &draw_item_ex_vec = draw_item_ex_tree[i];
            for (unsigned j=0;j<draw_item_ex_vec.GetCount();j++)
            {
                CompositeDrawItemEx &draw_item_ex = draw_item_ex_vec[j];
                if (draw_item_ex.item->shadow)
                {
                    for (size_t group = 0; group < draw_item_ex.rect_id_list.GetCount(); ++group)
                    {
                        const int id = draw_item_ex.rect_id_list[group];
                        AppendGroupedDraw(grouped_draw_items[id], draw_item_ex.item->shadow, draw_item_ex.item->shadowAddition, diagnostics);
                    }
                }
            }
            for (unsigned j=0;j<draw_item_ex_vec.GetCount();j++)
            {
                CompositeDrawItemEx &draw_item_ex = draw_item_ex_vec[j];
                if (draw_item_ex.item->outline)
                {
                    for (size_t group = 0; group < draw_item_ex.rect_id_list.GetCount(); ++group)
                    {
                        const int id = draw_item_ex.rect_id_list[group];
                        AppendGroupedDraw(grouped_draw_items[id], draw_item_ex.item->outline, draw_item_ex.item->outlineAddition, diagnostics);
                    }
                }
            }
            for (unsigned j=0;j<draw_item_ex_vec.GetCount();j++)
            {
                CompositeDrawItemEx &draw_item_ex = draw_item_ex_vec[j];
                if (draw_item_ex.item->body)
                {
                    for (size_t group = 0; group < draw_item_ex.rect_id_list.GetCount(); ++group)
                    {
                        const int id = draw_item_ex.rect_id_list[group];
                        AppendGroupedDraw(grouped_draw_items[id], draw_item_ex.item->body, draw_item_ex.item->bodyAddition, diagnostics);
                    }
                }
            }
        }
    }
    XySubRenderFrameCreater *render_frame_creater = XySubRenderFrameCreater::GetDefaultCreater();

    *output = NULL;
    std::unique_ptr<XySubRenderFrame> frame(render_frame_creater->NewXySubRenderFrame(grouped_draw_items.GetCount()));
    XySubRenderFrame& sub_render_frame = *frame;

    AlphaMaskBuffer alphaBuffer;
    RenderPhaseTimer bitmapTimer(diagnostics, RenderDiagnostics::BITMAP);
    for (unsigned i=0;i<grouped_draw_items.GetCount();i++)
    {
        grouped_draw_items[i].Draw(&(sub_render_frame.m_bitmaps.GetAt(i)), &(sub_render_frame.m_bitmap_ids.GetAt(i)), alphaBuffer);
    }
    *output = frame.release();
}

void CreateDrawItemExTree( CompositeDrawItemListList& input,
    CAtlArray<CompositeDrawItemEx>* storage,
    CompositeDrawItemExTree *out_draw_item_ex_tree, 
    XyRectExList *out_rect_ex_list )
{
    ASSERT(storage!=NULL && out_draw_item_ex_tree!=NULL && out_rect_ex_list!=NULL);

    size_t item_count = 0;
    POSITION list_pos = input.GetHeadPosition();
    while (list_pos)
    {
        const size_t count = input.GetNext(list_pos).GetCount();
        if (count > static_cast<size_t>(-1) - item_count) AtlThrow(E_OUTOFMEMORY);
        item_count += count;
    }
    storage->SetCount(item_count);
    int list_count = input.GetCount();
    out_draw_item_ex_tree->SetCount(list_count);
    size_t offset = 0;
    list_pos = input.GetHeadPosition();
    for (int list_id=0;list_id<list_count;list_id++)
    {
        CompositeDrawItemList& compDrawItemList = input.GetNext(list_pos);
        int count = compDrawItemList.GetCount();
        CompositeDrawItemExVec& out_draw_item_exs = out_draw_item_ex_tree->GetAt(list_id);
        out_draw_item_exs.items = count ? storage->GetData() + offset : NULL;
        out_draw_item_exs.count = count;
        offset += count;
        POSITION item_pos = compDrawItemList.GetHeadPosition();
        for (int item_id=0; item_id<count; item_id++ )
        {
            CompositeDrawItem& comp_draw_item = compDrawItemList.GetNext(item_pos);
            CompositeDrawItemEx& comp_draw_item_ex = out_draw_item_exs.GetAt(item_id);
            comp_draw_item_ex.item = &comp_draw_item;

            XyRectEx &rect_ex = out_rect_ex_list->GetAt(out_rect_ex_list->AddTail());
            comp_draw_item_ex.initial_members.AddTail(&comp_draw_item_ex);
            rect_ex.item_ex_list = &comp_draw_item_ex.initial_members;
            rect_ex.SetRect( CompositeDrawItem::GetDirtyRect(comp_draw_item) );
        }
    }
}

void MergeRects(const XyRectExList& input, XyRectExList* output, FrameMemberLists& members)
{
    int input_count = input.GetCount();
    if(output==NULL || input_count==0)
        return;

    typedef CAtlList<XyRectEx> Segment;

    struct BreakPoint
    {
        int x;
        const XyRectEx* rect;

        inline bool operator<(const BreakPoint& breakpoint ) const
        {
            return (x < breakpoint.x);
        }
    };

    std::vector<int> vertical_breakpoints(2*input_count);
    std::vector<BreakPoint> horizon_breakpoints(input_count);

    POSITION pos = input.GetHeadPosition();
    for(int i=0; i<input_count; i++)
    {
        const XyRectEx& rect = input.GetNext(pos);
        vertical_breakpoints[2*i]=rect.top;
        vertical_breakpoints[2*i+1]=rect.bottom;

        horizon_breakpoints[i].x = rect.left;
        horizon_breakpoints[i].rect = &rect;
    }

    std::sort(vertical_breakpoints.begin(), vertical_breakpoints.end());
    // Duplicate boundaries never create a band. Remove them before constructing
    // ATL list headers, while retaining exactly the same nonempty bands/order.
    vertical_breakpoints.erase(std::unique(vertical_breakpoints.begin(),vertical_breakpoints.end()),vertical_breakpoints.end());
    std::sort(horizon_breakpoints.begin(), horizon_breakpoints.end());

    CAtlArray<Segment> tempSegments;
    tempSegments.SetCount(vertical_breakpoints.size()-1);
    int prev = vertical_breakpoints[0], count = 0;
    for(unsigned ptr = 1; ptr<vertical_breakpoints.size(); ptr++)
    {
        if(vertical_breakpoints[ptr] != prev)
        {
            Segment& seg = tempSegments[count];
            seg.AddTail();
            seg.GetTail().SetRect(INT_MIN, prev, INT_MIN, vertical_breakpoints[ptr]);
            seg.GetTail().item_ex_list = &members.GetAt(members.AddTail());

            prev = vertical_breakpoints[ptr];
            count++;
        }
    }

    for(int i=0; i<input_count; i++)
    {
        const XyRectEx& rect = *horizon_breakpoints[i].rect;

        int start = 0, mid, end = count;

        while(start<end)
        {
            mid = (start+end)>>1;
            if(tempSegments[mid].GetTail().top < rect.top)
            {
                start = mid+1;
            }
            else
            {
                end = mid;
            }
        }
        for(; start < count; start++)
        {
            CAtlList<XyRectEx>& cur_line = tempSegments[start];
            XyRectEx & item = cur_line.GetTail();
            if (item.top >= rect.bottom)
            {
                break;
            }
            if (item.right<rect.left)
            {
                cur_line.AddTail();
                XyRectEx & new_item = cur_line.GetTail();
                new_item.SetRect( rect.left, item.top, rect.right, item.bottom );
                new_item.item_ex_list = &members.GetAt(members.AddTail());
                AppendMembers(*new_item.item_ex_list, *rect.item_ex_list);
            }
            else
            {
                if (item.right<rect.right)
                {
                    item.right = rect.right;
                }
                AppendMembers(*item.item_ex_list, *rect.item_ex_list);
            }
        }
    }

    for (int i=count-1;i>0;i--)
    {
        CAtlList<XyRectEx>& cur_line = tempSegments[i];
        CAtlList<XyRectEx>& upper_line = tempSegments[i-1];

        POSITION pos_cur_line = cur_line.GetTailPosition();
        XyRectEx *cur_rect = &cur_line.GetPrev(pos_cur_line);
        if(cur_rect->top == upper_line.GetTail().bottom)
        {
            POSITION pos = upper_line.GetTailPosition();                          
            while(pos)
            {
                XyRectEx& upper_rect = upper_line.GetPrev(pos);
                while( upper_rect.right<cur_rect->right || upper_rect.left<cur_rect->left )
                {
                    output->AddHead(*cur_rect);
                    //if(!cur_line.IsEmpty())
                    cur_rect = &cur_line.GetPrev(pos_cur_line);
                }
                if(!pos_cur_line)
                    break;

                if(upper_rect.right==cur_rect->right && upper_rect.left==cur_rect->left)
                {
                    upper_rect.bottom = cur_rect->bottom;
                    AppendMembers(*upper_rect.item_ex_list, *cur_rect->item_ex_list);
                    //if(!cur_line.IsEmpty())
                    cur_rect = &cur_line.GetPrev(pos_cur_line);
                }
                //else if ( upper_rect.right>cur_rect.right || upper_rect.left>cur_rect.left )             
            }
        }
        while(pos_cur_line)
        {
            output->AddHead(*cur_rect);
            cur_rect = &cur_line.GetPrev(pos_cur_line);
        }
    }
    if(count>0)
    {
        CAtlList<XyRectEx>& cur_line = tempSegments[0];
        POSITION pos_cur_line = cur_line.GetTailPosition();
        XyRectEx *cur_rect = &cur_line.GetPrev(pos_cur_line);
        while(pos_cur_line)
        {
            output->AddHead(*cur_rect);
            cur_rect = &cur_line.GetPrev(pos_cur_line);
        }
    }
}

void DecideDrawMethod( CompositeDrawItemListList& compDrawItemListList, XyRectExList& rect_ex_list )
{
    // These decisions belong to this composition, never to retained DrawItems.
    for (POSITION p=compDrawItemListList.GetHeadPosition();p;)
    {
        CompositeDrawItemList& list=compDrawItemListList.GetNext(p);
        for (POSITION q=list.GetHeadPosition();q;)
        {
            CompositeDrawItem& item=list.GetNext(q);
            item.shadowAddition=item.outlineAddition=item.bodyAddition=false;
        }
    }
    int count = rect_ex_list.GetCount();
    if (count>128)//this implementation is too slow for input of large scale
    {
        return;
    }
    bool not_lowest[128];
    memset(not_lowest, 0, sizeof(not_lowest));

    int list_count = compDrawItemListList.GetCount();
    POSITION list_pos = compDrawItemListList.GetHeadPosition();
    POSITION rect_pos = rect_ex_list.GetHeadPosition();
    bool *not_lowest1 = not_lowest;
    for (;list_pos;)
    {
        CompositeDrawItemList& compDrawItemList = compDrawItemListList.GetNext(list_pos);
        POSITION item_pos = compDrawItemList.GetHeadPosition();
        for (; item_pos; )
        {
            CompositeDrawItem& comp_draw_item = compDrawItemList.GetNext(item_pos);
            XyRectEx &rect_ex = rect_ex_list.GetNext(rect_pos);

            POSITION pos = rect_pos;
            bool *not_lowest2 = not_lowest1;
            while(pos)
            {
                XyRectEx &rect_ex2 = rect_ex_list.GetNext(pos);
                not_lowest2++;
                if (!*not_lowest2)
                {
                    if (!(rect_ex.left  >= rect_ex2.right || 
                          rect_ex.right <= rect_ex2.left  || 
                          rect_ex.top   >= rect_ex2.bottom||
                          rect_ex.bottom<= rect_ex2.top))
                    {
                        *not_lowest2 = true;
                    }
                }
            }
            if (!*not_lowest1)
            {
                if (comp_draw_item.shadow ) comp_draw_item.shadowAddition  = true;
                int n = !!comp_draw_item.body + !!comp_draw_item.outline + !!comp_draw_item.shadow;
                if (n==1 || 
                    (!comp_draw_item.shadow && comp_draw_item.outline && !comp_draw_item.outline->fBody))
                {
                    if (comp_draw_item.body   ) comp_draw_item.bodyAddition    = true;
                    if (comp_draw_item.outline) comp_draw_item.outlineAddition = true;
                }
            }
            not_lowest1++;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// GroupedDrawItems
// 

void GroupedDrawItems::Draw( SharedPtrXyBitmap *bitmap, int *bitmap_identity_num, AlphaMaskBuffer& buffer )
{
    ASSERT(bitmap && bitmap_identity_num);
    BitmapMruCache *bitmap_cache = CacheManager::GetBitmapMruCache();
    // Borrow the already assembled children for this lookup. The interner's
    // value copy materializes owned keys on a miss; no view escapes this call.
#ifdef XY_BENCHMARK_OWNED_GROUP_KEYS
    // Build-only control for measuring borrowed probes against owned snapshots.
    GroupedDrawItemsHashKey key;
    CreateHashKey(&key);
#else
    GroupedDrawItemsHashKey key(*this);
#endif
    XyFwGroupedDrawItemsHashKey::IdType key_id = XyFwGroupedDrawItemsHashKey(key).GetId();
    POSITION pos = bitmap_cache->Lookup( key_id );
    if (pos==NULL)
    {
        XyBitmap *tmp = XySubRenderFrameCreater::GetDefaultCreater()->CreateBitmap(clip_rect);
        if (!tmp) throw std::bad_alloc();
        bitmap->reset(tmp);

        for (size_t i = 0; i < draw_item_list.GetCount(); ++i)
        {
            DrawItem::Draw(tmp, *draw_item_list[i], clip_rect, buffer,
                addition_flags.GetCount()==draw_item_list.GetCount() ? addition_flags[i]!=0 : draw_item_list[i]->use_addition_draw);
        }
        XyColorSpace color_space;
        HRESULT hr = XySubRenderFrameCreater::GetDefaultCreater()->GetColorSpace(&color_space);
        ASSERT(SUCCEEDED(hr));
        if (color_space==XY_CS_ARGB_F)
            XyBitmap::FlipAlphaValue(tmp->bits, tmp->w, tmp->h, tmp->pitch);
#ifdef XY_DBG_SHOW_BITMAP_BOX_IN_COLOR
        if (color_space==XY_CS_ARGB_F||color_space==XY_CS_ARGB)//not support plannar format yet
        {
            if (tmp->w>0 && tmp->h>0)
            {
                DWORD color = color_space==XY_CS_ARGB_F ? (0xff000000 | XY_DBG_SHOW_BITMAP_BOX_IN_COLOR)
                    : (0x00ffffff & XY_DBG_SHOW_BITMAP_BOX_IN_COLOR);
                BYTE *pixel = (BYTE *)tmp->bits;
                memsetd(pixel, color, tmp->w*4);
                int i=1;
                for (;i<tmp->h-1;i++)
                {
                    pixel += tmp->pitch;
                    *(DWORD*)pixel = color;
                    *((DWORD*)pixel+tmp->w-1) = color;
                }
                if (i<tmp->h)
                {
                    pixel += tmp->pitch;
                    memsetd(pixel, color, tmp->w*4);
                }
            }

        }
#endif
        bitmap_cache->UpdateCache(key_id, *bitmap);
    }
    else
    {
        *bitmap = bitmap_cache->GetAt(pos);
        bitmap_cache->UpdateCache(pos);
    }
    *bitmap_identity_num  = key_id;
}

void GroupedDrawItems::CreateHashKey(GroupedDrawItemsHashKey *key)
{
    ASSERT(key);
    // This existing API continues to return an owning snapshot, including when
    // its destination previously held a borrowed lookup view.
    const GroupedDrawItemsHashKey probe(*this);
    *key = probe;
}
