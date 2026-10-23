#include "stdafx.h"
#include <boost/make_shared.hpp>
#include "xy_overlay_paint_machine.h"
#include "RTS.h"
#include "Rasterizer.h"
#include "cache_manager.h"
#include "subpixel_position_controler.h"

//////////////////////////////////////////////////////////////////////////
//
// OverlayPaintMachine
//

void OverlayPaintMachine::Paint(SharedPtrOverlay* overlay)
{
    if (!m_overlay)
    {
        m_inner_paint_machine->Paint(m_layer, &m_overlay);
    }
    if(overlay)
        *overlay = m_overlay;
}

CRectCoor2 OverlayPaintMachine::CalcDirtyRect()
{
    ASSERT(m_inner_paint_machine);
    if (!m_overlay) m_inner_paint_machine->Paint(m_layer, &m_overlay);
    if (m_overlay)
    {
        int x = m_overlay->mOffsetX;
        int y = m_overlay->mOffsetY;
        int w = 8*m_overlay->mOverlayWidth;
        int h = 8*m_overlay->mOverlayHeight;
        return CRect(x, y, x+w, y+h);
    }
    return CRect(0,0,0,0);
}

const SharedPtrOverlayKey& OverlayPaintMachine::GetHashKey()
{
    return m_inner_paint_machine->GetHashKey(m_layer);
}

const SharedPtrDrawItemHashKey& OverlayPaintMachine::GetDrawItemHashKey(const DrawItem& item)
{
    return m_inner_paint_machine->GetDrawItemHashKey(item);
}

const SharedPtrDrawItemHashKey& CWordPaintMachine::GetDrawItemHashKey(const DrawItem& item)
{
    for (unsigned i = 0; i < 3; ++i)
        if (m_word->m_drawKeyCache[i] && m_word->m_drawKeyCache[i]->MatchesDrawItem(item))
            return m_word->m_drawKeyCache[i];
    SharedPtrDrawItemHashKey& key = m_word->m_drawKeyCache[m_word->m_nextDrawKey];
    key = boost::make_shared<DrawItemHashKey>(item);
    key->UpdateHashValue();
    m_word->m_nextDrawKey = (m_word->m_nextDrawKey+1)%3;
    return key;
}

void CWordPaintMachine::Paint( LAYER layer, SharedPtrOverlay* overlay )
{
    switch (layer)
    {
    case SHADOW:
        PaintShadow(m_word, m_shadow_pos, overlay);
        break;
    case OUTLINE:
        PaintOutline(m_word, m_outline_pos, overlay);
        break;
    case BODY:
        PaintBody(m_word, m_body_pos, overlay);
        break;
    }
}

void CWordPaintMachine::PaintBody( const SharedPtrCWord& word, const CPointCoor2& p, SharedPtrOverlay* overlay )
{
    if(!word->m_str.Get() || overlay==NULL) return;
    bool error = false;
    do 
    {
        CPoint trans_org2 = m_trans_org;    
        bool need_transform = word->NeedTransform();
        if(!need_transform)
        {
            trans_org2.x=0;
            trans_org2.y=0;
        }

        CPoint psub_true( (p.x&SubpixelPositionControler::EIGHT_X_EIGHT_MASK), (p.y&SubpixelPositionControler::EIGHT_X_EIGHT_MASK) );
        OverlayKey sub_key(*word, psub_true, trans_org2);
        sub_key.UpdateHashValue();
        if( SubpixelPositionControler::GetGlobalControler().UseBilinearShift() )
        {
            OverlayMruCache* overlay_cache = CacheManager::GetSubpixelVarianceCache();

            POSITION pos = overlay_cache->Lookup(sub_key);
            if(pos!=NULL) 
            {
                *overlay = overlay_cache->GetAt(pos);
                overlay_cache->UpdateCache( pos );
            }
        }
        if( !overlay->get() )
        {
            CPoint psub = SubpixelPositionControler::GetGlobalControler().GetSubpixel(p);
            OverlayKey overlay_key(*word, psub, trans_org2);
            overlay_key.UpdateHashValue();
            OverlayMruCache* overlay_cache = CacheManager::GetOverlayMruCache();
            POSITION pos = overlay_cache->Lookup(overlay_key);
            if(pos==NULL)
            {
                if( !word->DoPaint(psub, trans_org2, overlay, overlay_key) )
                {
                    error = true;
                    break;
                }                
            }
            else
            {
                *overlay = overlay_cache->GetAt(pos);
                overlay_cache->UpdateCache( pos );
            }
            CWord::PaintFromOverlay(p, trans_org2, sub_key, *overlay);
        }
    } while(false);
    if(error)
    {
        overlay->reset( DEBUG_NEW Overlay() );
    }
}

void CWordPaintMachine::PaintOutline( const SharedPtrCWord& word, const CPointCoor2& p, SharedPtrOverlay* overlay )
{
    if (word->m_style.get().borderStyle==0)
    {
        PaintBody(word, p, overlay);
    }
    else if (word->m_style.get().borderStyle==1)
    {
        if(word->CreateOpaqueBox())
        {
            PaintBody(word->m_pOpaqueBox, p, overlay);
        }
    }
}

void CWordPaintMachine::PaintShadow( const SharedPtrCWord& word, const CPointCoor2& p, SharedPtrOverlay* overlay )
{
    PaintOutline(word, p, overlay);
}

void CWordPaintMachine::CreatePaintMachines( const SharedPtrCWord& word
    , const CPointCoor2& shadow_pos, const CPointCoor2& outline_pos, const CPointCoor2& body_pos
    , const CPointCoor2& org
    , SharedPtrOverlayPaintMachine *shadow, SharedPtrOverlayPaintMachine *outline, SharedPtrOverlayPaintMachine *body)
{
    SharedCWordPaintMachine machine = boost::make_shared<CWordPaintMachine>(AllocationTag());
    machine->m_word = word;
    if (shadow!=NULL)
    {
        machine->m_trans_org = org - shadow_pos;
    }
    else if (outline!=NULL)
    {
        machine->m_trans_org = org - outline_pos;
    }
    else if (body!=NULL)
    {
        machine->m_trans_org = org - body_pos;
    }
    machine->m_shadow_pos = shadow_pos;
    machine->m_outline_pos = outline_pos;
    machine->m_body_pos = body_pos;

    const auto sameSubpixel = [](const CPointCoor2& a, const CPointCoor2& b) {
        const int mask = SubpixelPositionControler::EIGHT_X_EIGHT_MASK;
        return (a.x & mask) == (b.x & mask) && (a.y & mask) == (b.y & mask);
    };
    if (body) machine->m_body_key = machine->CreateBodyOverlayHashKey(word, machine->m_body_pos);
    if (outline)
    {
        // Layer keys describe the same geometry when their fractional position
        // matches. The draw-item key separately retains color, flags and full position.
        if (word->m_style.get().borderStyle == 0 && body && sameSubpixel(outline_pos, body_pos))
            machine->m_border_key = machine->m_body_key;
        else machine->m_border_key = machine->CreateOutlineOverlayHashKey(word, outline_pos);
    }
    if (shadow)
    {
        if (outline && sameSubpixel(shadow_pos, outline_pos))
            machine->m_shadow_key = machine->m_border_key;
        else if (word->m_style.get().borderStyle == 0 && body && sameSubpixel(shadow_pos, body_pos))
            machine->m_shadow_key = machine->m_body_key;
        else machine->m_shadow_key = machine->CreateShadowOverlayHashKey(word, shadow_pos);
    }

    if (body)
    {
        *body = boost::make_shared<OverlayPaintMachine>(machine, BODY);
    }
    if (outline)
    {
        if (body && machine->m_border_key == machine->m_body_key) *outline = *body;
        else *outline = boost::make_shared<OverlayPaintMachine>(machine, OUTLINE);
    }
    if (shadow)
    {
        if (outline && machine->m_shadow_key == machine->m_border_key) *shadow = *outline;
        else if (body && machine->m_shadow_key == machine->m_body_key) *shadow = *body;
        else *shadow = boost::make_shared<OverlayPaintMachine>(machine, SHADOW);
    }
}

void CWordPaintMachine::PaintBody( const SharedPtrCWord& word, const CPointCoor2& p, const CPointCoor2& org, SharedPtrOverlay* overlay )
{
    CWordPaintMachine machine;
    machine.m_word = word;
    machine.m_trans_org = org - p;//IMPORTANT! NOTE: not totally initiated
    machine.m_body_pos = p;
    machine.m_body_key = machine.CreateBodyOverlayHashKey(word, machine.m_body_pos);
    machine.PaintBody(word, p, overlay);
}

const SharedPtrOverlayKey& CWordPaintMachine::GetHashKey(LAYER layer)
{
    //fix me: sharing keys with paint functions
    switch (layer)
    {
    case SHADOW:
        return m_shadow_key;
        break;
    case OUTLINE:
        return m_border_key;
        break;
    case BODY:
        return m_body_key;
        break;
    }
    ASSERT(0);
    return m_body_key;
}

SharedPtrOverlayKey CWordPaintMachine::CreateBodyOverlayHashKey( const SharedPtrCWord& word, const CPointCoor2& p )
{
    CPoint trans_org2 = m_trans_org;    
    bool need_transform = word->NeedTransform();
    if(!need_transform)
    {
        trans_org2.x=0;
        trans_org2.y=0;
    }
    CPoint psub_true( (p.x&SubpixelPositionControler::EIGHT_X_EIGHT_MASK), (p.y&SubpixelPositionControler::EIGHT_X_EIGHT_MASK) );
    for (unsigned i = 0; i < 3; ++i)
        if (word->m_overlayKeyCache[i] && word->m_overlayKeyCache[i]->MatchesWord(*word,psub_true,trans_org2))
            return word->m_overlayKeyCache[i];
    SharedPtrOverlayKey key = boost::make_shared<OverlayKey>(*word, psub_true, trans_org2);
    key->UpdateHashValue();
    word->m_overlayKeyCache[word->m_nextOverlayKey] = key;
    word->m_nextOverlayKey = (word->m_nextOverlayKey+1)%3;
    return key;
}

SharedPtrOverlayKey CWordPaintMachine::CreateOutlineOverlayHashKey( const SharedPtrCWord& word, const CPointCoor2& p )
{
    if (word->m_style.get().borderStyle==0)
    {
        return CreateBodyOverlayHashKey(word, p);
    }
    else if (word->m_style.get().borderStyle==1)
    {
        if(word->CreateOpaqueBox())
        {
            return CreateBodyOverlayHashKey(word->m_pOpaqueBox, p);
        }
    }
    ASSERT(0);
    return SharedPtrOverlayKey();
}

SharedPtrOverlayKey CWordPaintMachine::CreateShadowOverlayHashKey( const SharedPtrCWord& word, const CPointCoor2& p )
{
    return CreateOutlineOverlayHashKey(word, p);
}
