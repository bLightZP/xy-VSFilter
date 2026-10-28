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

#include "stdafx.h"
#include <new>
#include <cstdint>
#include <cstddef>
#include <math.h>
#include <time.h>
#include <locale.h>
#include <float.h>
#include <boost/make_shared.hpp>
#include "RTS.h"
#include "draw_item.h"
#include "cache_manager.h"
#include "subpixel_position_controler.h"
#include "xy_overlay_paint_machine.h"
#include "xy_clipper_paint_machine.h"
#include "..\dsutil\DSUtil.h"
#include "../dsutil/DSMPropertyBag.h"
#include <stdexcept>

#if ENABLE_XY_LOG_TEXT_PARSER
#  define TRACE_PARSER(msg) XY_LOG_TRACE(msg)
#else
#  define TRACE_PARSER(msg)
#endif

#if ENABLE_XY_LOG_RENDERER_REQUEST2
#  define TRACE_RENDERER_REQUEST(msg) XY_LOG_TRACE(msg)
#  define TRACE_RENDERER_REQUEST_TIMING(msg) XY_AUTO_TIMING(msg)
#else
#  define TRACE_RENDERER_REQUEST(msg)
#  define TRACE_RENDERER_REQUEST_TIMING(msg)
#endif

const int MAX_SUB_PIXEL = 8;
const double MAX_SUB_PIXEL_F = 8.0;

// WARNING: this isn't very thread safe, use only one RTS a time.
static HDC g_hDC;
static int g_hDC_refcnt = 0;

static long revcolor(long c)
{
    return ((c&0xff0000)>>16) + (c&0xff00) + ((c&0xff)<<16);
}

// Skip all leading whitespace
inline CStringW::PCXSTR SkipWhiteSpaceLeft(const CStringW& str)
{
    CStringW::PCXSTR psz = str.GetString();

    while( iswspace( *psz ) )
    {
        psz++;
    }
    return psz;
}

// Skip all trailing whitespace
inline CStringW::PCXSTR SkipWhiteSpaceRight(const CStringW& str)
{
    CStringW::PCXSTR psz = str.GetString();
    CStringW::PCXSTR pszLast = psz + str.GetLength() - 1;
    bool first_white = false;
    while( iswspace( *pszLast ) )
    {
        pszLast--;
        if(pszLast<psz)
            break;
    }
    return pszLast;
}

// Skip all leading whitespace
inline CStringW::PCXSTR SkipWhiteSpaceLeft(CStringW::PCXSTR start, CStringW::PCXSTR end)
{
    while( start!=end && iswspace( *start ) )
    {
        start++;
    }
    return start;
}

// Skip all trailing whitespace, first char must NOT be white space
inline CStringW::PCXSTR FastSkipWhiteSpaceRight(CStringW::PCXSTR start, CStringW::PCXSTR end)
{
    while( iswspace( *--end ) );
    return end+1;
}

inline CStringW::PCXSTR FindChar(CStringW::PCXSTR start, CStringW::PCXSTR end, WCHAR c)
{
    while( start!=end && *start!=c )
    {
        start++;
    }
    return start;
}

//////////////////////////////////////////////////////////////////////////////////////////////

// CMyFont

CMyFont::CMyFont(const STSStyleBase& style)
{
    LOGFONT lf;
    ZeroMemory(&lf, sizeof(lf));
    lf <<= style;
    lf.lfHeight         = (LONG)(style.fontSize+0.5);
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    lf.lfQuality        = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH|FF_DONTCARE;
    if(!CreateFontIndirect(&lf))
    {
        _tcscpy(lf.lfFaceName, _T("Arial"));
        VERIFY(CreateFontIndirect(&lf));
    }
    HFONT hOldFont = SelectFont(g_hDC, *this);
    TEXTMETRIC tm;
    GetTextMetrics(g_hDC, &tm);
    m_ascent  = ((tm.tmAscent  + 4) >> 3);
    m_descent = ((tm.tmDescent + 4) >> 3);
    SelectFont(g_hDC, hOldFont);
}

// CWord

CWord::CWord( const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend
    , double target_scale_x/*=1.0*/, double target_scale_y/*=1.0*/
    , bool round_to_whole_pixel_after_scale_to_target/*=false*/)
    : m_style(style), m_str(str), m_nextOverlayKey(0), m_nextDrawKey(0)
    , m_width(0), m_ascent(0), m_descent(0)
    , m_ktype(ktype), m_kstart(kstart), m_kend(kend)
    , m_fLineBreak(false), m_fWhiteSpaceChar(false)
    , m_target_scale_x(target_scale_x), m_target_scale_y(target_scale_y)
    , m_round_to_whole_pixel_after_scale_to_target(round_to_whole_pixel_after_scale_to_target)
    //, m_pOpaqueBox(NULL)
{
    if(m_str.Get().IsEmpty())
    {
        m_fWhiteSpaceChar = m_fLineBreak = true;
    }
    m_width = 0;
}

CWord::CWord( const CWord& src):m_str(src.m_str), m_style(src.m_style), m_nextOverlayKey(0), m_nextDrawKey(0)
{
    m_fWhiteSpaceChar                            = src.m_fWhiteSpaceChar;
    m_fLineBreak                                 = src.m_fLineBreak;
    m_pOpaqueBox                                 = src.m_pOpaqueBox;//allow since it is shared_ptr
    m_ktype                                      = src.m_ktype;
    m_kstart                                     = src.m_kstart;
    m_kend                                       = src.m_kend;
    m_width                                      = src.m_width;
    m_ascent                                     = src.m_ascent;
    m_descent                                    = src.m_descent;
    m_target_scale_x                             = src.m_target_scale_x;
    m_target_scale_y                             = src.m_target_scale_y;
    m_round_to_whole_pixel_after_scale_to_target = src.m_round_to_whole_pixel_after_scale_to_target;
}

size_t CWord::GetPreparedStorageBytes() const
{
    // Prototypes have empty paint-key caches and no opaque box. Charge full
    // referenced strings/style and polygon storage even when another owner
    // shares them; this is conservative admission, not a process-memory limit.
    size_t bytes = sizeof(CPolygon) + sizeof(STSStyle) + 256
        + (m_str.Get().GetAllocLength()+1)*sizeof(WCHAR)
        + (m_style.get().fontName.GetAllocLength()+1)*sizeof(TCHAR);
    const CPolygon* polygon = dynamic_cast<const CPolygon*>(this);
    return bytes + (polygon ? polygon->GetPreparedPathBytes() : 0);
}

CWord::~CWord()
{
    //if(m_pOpaqueBox) delete m_pOpaqueBox;
}

bool CWord::Append(const SharedPtrCWord& w)
{
    if (m_style != w->m_style ||
          m_fLineBreak || w->m_fLineBreak || 
          w->m_kstart != w->m_kend || m_ktype != w->m_ktype)
          return(false);
    m_fWhiteSpaceChar = m_fWhiteSpaceChar && w->m_fWhiteSpaceChar;
    CStringW str = m_str.Get();
    str += w->m_str.Get();
    m_str = XyFwStringW(str);
    m_width += w->m_width;
    return(true);
}

void CWord::PaintFromOverlay(const CPointCoor2& p, const CPointCoor2& trans_org2, const OverlayKey &subpixel_variance_key, SharedPtrOverlay& overlay)
{
    if( SubpixelPositionControler::GetGlobalControler().UseBilinearShift() )
    {
        CPoint psub = SubpixelPositionControler::GetGlobalControler().GetSubpixel(p);
        if( (psub.x!=(p.x&SubpixelPositionControler::EIGHT_X_EIGHT_MASK) || 
             psub.y!=(p.y&SubpixelPositionControler::EIGHT_X_EIGHT_MASK)) )
        {
            overlay.reset(overlay->GetSubpixelVariance((p.x&SubpixelPositionControler::EIGHT_X_EIGHT_MASK) - psub.x, 
                (p.y&SubpixelPositionControler::EIGHT_X_EIGHT_MASK) - psub.y));
            OverlayMruCache* overlay_cache = CacheManager::GetSubpixelVarianceCache();
            overlay_cache->UpdateCache(subpixel_variance_key, overlay);
        }
    }
}

void CWord::PaintFromNoneBluredOverlay(SharedPtrOverlay raterize_result, const OverlayKey& overlay_key, SharedPtrOverlay* overlay)
{
    if( Rasterizer::IsItReallyBlur(m_style.get().fBlur, m_style.get().fGaussianBlur) )
    {
        overlay->reset(DEBUG_NEW Overlay());
        if (!Rasterizer::Blur(
            *raterize_result, 
            m_style.get().fBlur, 
            m_style.get().fGaussianBlur, 
            m_target_scale_x, 
            m_target_scale_y, 
            *overlay))
        {
            *overlay = raterize_result;
        }
    }
    else
    {
        *overlay = raterize_result;
    }
    OverlayMruCache* overlay_cache = CacheManager::GetOverlayMruCache();
    overlay_cache->UpdateCache(overlay_key, *overlay);
}

bool CWord::PaintFromScanLineData2(const CPointCoor2& psub, const ScanLineData2& scan_line_data2, const OverlayKey& key, SharedPtrOverlay* overlay)
{
    SharedPtrOverlay raterize_result(DEBUG_NEW Overlay());
    if(!Rasterizer::Rasterize(scan_line_data2, psub.x, psub.y, raterize_result)) 
    {     
        return false;
    }
    OverlayNoBlurMruCache* overlay_no_blur_cache = CacheManager::GetOverlayNoBlurMruCache();
    overlay_no_blur_cache->UpdateCache(key, raterize_result);
    PaintFromNoneBluredOverlay(raterize_result, key, overlay);
    return true;
}

bool CWord::PaintFromPathData(const CPointCoor2& psub, const CPointCoor2& trans_org, const PathData& path_data, const OverlayKey& key, SharedPtrOverlay* overlay )
{
    bool result = false;

    PathData *path_data2 = DEBUG_NEW PathData(path_data);//fix me: this copy operation can be saved if no transform is needed
    SharedPtrConstPathData shared_ptr_path_data2(path_data2);
    bool need_transform = NeedTransform();
    if(need_transform)
        Transform(path_data2, CPoint(trans_org.x*MAX_SUB_PIXEL, trans_org.y*MAX_SUB_PIXEL));

    CPoint left_top;
    CSize  size;
    path_data2->AlignLeftTop(&left_top, &size);

    int border_x = static_cast<int>(m_style.get().outlineWidthX*m_target_scale_x+0.5);//fix me: rounding err
    int border_y = static_cast<int>(m_style.get().outlineWidthY*m_target_scale_y+0.5);//fix me: rounding err
    int wide_border = border_x>border_y ? border_x:border_y;
    if (m_style.get().borderStyle==1)
    {
        border_x = border_y = 0;
    }

    OverlayNoOffsetMruCache* overlay_key_cache = CacheManager::GetOverlayNoOffsetMruCache();
    OverlayNoOffsetKey overlay_no_offset_key(shared_ptr_path_data2, psub.x, psub.y, border_x, border_y);
    overlay_no_offset_key.UpdateHashValue();
    POSITION pos_key = overlay_key_cache->Lookup(overlay_no_offset_key);
    POSITION pos = NULL;
        
    OverlayNoBlurMruCache* overlay_cache = CacheManager::GetOverlayNoBlurMruCache();
    if (pos_key!=NULL)
    {
        OverlayNoBlurKey overlay_key = overlay_key_cache->GetAt(pos_key);
        pos = overlay_cache->Lookup(overlay_key);
    }
    if (pos)
    {
        SharedPtrOverlay raterize_result( DEBUG_NEW Overlay() );
        *raterize_result = *overlay_cache->GetAt(pos);
        raterize_result->mOffsetX = left_top.x - psub.x - ((wide_border+7)&~7);
        raterize_result->mOffsetY = left_top.y - psub.y - ((wide_border+7)&~7);
        PaintFromNoneBluredOverlay(raterize_result, key, overlay);
        result = true;
        overlay_cache->UpdateCache(key, raterize_result);
    }
    else
    {
        ScanLineDataMruCache* scan_line_data_cache = CacheManager::GetScanLineDataMruCache();
        pos = scan_line_data_cache->Lookup(overlay_no_offset_key);
        SharedPtrConstScanLineData scan_line_data;
        if( pos != NULL )
        {
            scan_line_data = scan_line_data_cache->GetAt(pos);
            scan_line_data_cache->UpdateCache(pos);
        }
        else
        {
            ScanLineData *tmp = DEBUG_NEW ScanLineData();
            scan_line_data.reset(tmp);
            if(!tmp->ScanConvert(*path_data2, size))
            {
                return false;
            }
            scan_line_data_cache->UpdateCache(overlay_no_offset_key, scan_line_data);
        }
        ScanLineData2 *tmp = DEBUG_NEW ScanLineData2(left_top, scan_line_data);
        SharedPtrScanLineData2 scan_line_data2( tmp );
        if(m_style.get().borderStyle == 0 && (m_style.get().outlineWidthX+m_style.get().outlineWidthY > 0))
        {
            if(!tmp->CreateWidenedRegion(border_x, border_y)) 
            {
                return false;
            }
        }
        ScanLineData2MruCache* scan_line_data2_cache = CacheManager::GetScanLineData2MruCache();
        scan_line_data2_cache->UpdateCache(key, scan_line_data2);
        result = PaintFromScanLineData2(psub, *tmp, key, overlay);
    }
    if (result)
    {
        if (pos_key!=NULL)
        {
            overlay_key_cache->UpdateCache(pos_key, key);
        }
        else
        {
            overlay_key_cache->UpdateCache(overlay_no_offset_key, key);
        }
    }
    return result;
}

bool CWord::PaintFromRawData( const CPointCoor2& psub, const CPointCoor2& trans_org, const OverlayKey& key, SharedPtrOverlay* overlay )
{
    PathDataMruCache* path_data_cache = CacheManager::GetPathDataMruCache();

    PathData *tmp=DEBUG_NEW PathData();
    SharedPtrPathData path_data(tmp);
    if(!CreatePath(tmp))
    {
        return false;
    }
    path_data_cache->UpdateCache(key, path_data);
    return PaintFromPathData(psub, trans_org, *tmp, key, overlay);
}

bool CWord::DoPaint(const CPointCoor2& psub, const CPointCoor2& trans_org, SharedPtrOverlay* overlay, const OverlayKey& key)
{
    bool result = true;
    OverlayNoBlurMruCache* overlay_no_blur_cache = CacheManager::GetOverlayNoBlurMruCache();
    POSITION pos = overlay_no_blur_cache->Lookup(key);

    if(pos!=NULL)
    {
        SharedPtrOverlay raterize_result = overlay_no_blur_cache->GetAt(pos);
        overlay_no_blur_cache->UpdateCache( pos );
        PaintFromNoneBluredOverlay(raterize_result, key, overlay);
    }  
    else
    {
        ScanLineData2MruCache* scan_line_data_cache = CacheManager::GetScanLineData2MruCache();
        pos = scan_line_data_cache->Lookup(key);
        if(pos!=NULL)
        {
            SharedPtrConstScanLineData2 scan_line_data = scan_line_data_cache->GetAt(pos);
            scan_line_data_cache->UpdateCache( pos );
            result = PaintFromScanLineData2(psub, *scan_line_data, key, overlay);
        }
        else
        {     
            PathDataMruCache* path_data_cache = CacheManager::GetPathDataMruCache();
            POSITION pos_path = path_data_cache->Lookup(key);
            if(pos_path!=NULL)    
            {
                SharedPtrConstPathData path_data = path_data_cache->GetAt(pos_path); //important! copy not ref
                path_data_cache->UpdateCache( pos_path );
                result = PaintFromPathData(psub, trans_org, *path_data, key, overlay);
            }
            else
            {
                result = PaintFromRawData(psub, trans_org, key, overlay);
            }
        }
    }
    return result;
}

bool CWord::NeedTransform()
{
    return (fabs(m_style.get().fontScaleX - 100) > 0.000001) ||
           (fabs(m_style.get().fontScaleY - 100) > 0.000001) ||
           (fabs(m_style.get().fontAngleX) > 0.000001) ||
           (fabs(m_style.get().fontAngleY) > 0.000001) ||
           (fabs(m_style.get().fontAngleZ) > 0.000001) ||
           (fabs(m_style.get().fontShiftX) > 0.000001) ||
           (fabs(m_style.get().fontShiftY) > 0.000001) ||
           (fabs(m_target_scale_x-1.0) > 0.000001) ||
           (fabs(m_target_scale_y-1.0) > 0.000001);
}

//void CWord::Transform(PathData* path_data, const CPointCoor2& org)
//{
//    //// CPUID from VDub
//    //bool fSSE2 = !!(g_cpuid.m_flags & CCpuID::sse2);
//
//    //if(fSSE2) {	// SSE code
//    //	Transform_SSE2(path_data, org);
//    //} else		// C-code
//          Transform_C(path_data, org);
//}

void CWord::Transform(PathData* path_data, const CPointCoor2 &org )
{
    ASSERT(path_data);
    const STSStyle& style = m_style.get();

    const double scalex = style.fontScaleX/100.0;
    const double scaley = style.fontScaleY/100.0;

    const double caz = cos((M_PI/180.0)*style.fontAngleZ);
    const double saz = sin((M_PI/180.0)*style.fontAngleZ);
    const double cax = cos((M_PI/180.0)*style.fontAngleX);
    const double sax = sin((M_PI/180.0)*style.fontAngleX);
    const double cay = cos((M_PI/180.0)*style.fontAngleY);
    const double say = sin((M_PI/180.0)*style.fontAngleY);

    double xxx[3][3];
    /******************
          targetScaleX            0    0
     S0 =            0 targetScaleY    0
                     0            0    1
    /******************
          20000     0    0
     A0 =     0 20000    0
              0     0    1
    /******************
          cay    0  say
     A1 =   0    1    0
          say    0 -cay
    /******************
            1    0    0
     A2 =   0  cax  sax
            0  sax -cax
    /******************
          caz  saz    0
     A3 =-saz  caz    0
            0    0    1
    /******************
          scalex            scalex*fontShiftX -org.x/targetScaleX
     A4 = scaley*fontShiftY scaley            -org.y/targetScaleY
          0                 0                  0
    /******************
              0     0      0
     B0 =     0     0      0
              0     0  20000
    /******************
     Formula:
       (x,y,z)' = (S0*A0*A1*A2*A3*A4 + B0) * (x y 1)'
       z = max(1000,z)
       x = x/z + org.x
       y = y/z + org.y
    *******************/

    //A3*A4
    ASSERT(m_target_scale_x!=0 && m_target_scale_y!=0);
    double tmp1 = -org.x/m_target_scale_x;
    double tmp2 = -org.y/m_target_scale_y;

    xxx[0][0] = caz*scalex + saz*scaley*style.fontShiftY;
    xxx[0][1] = caz*scalex*style.fontShiftX + saz*scaley;
    xxx[0][2] = caz*tmp1 + saz*tmp2;

    xxx[1][0] = -saz*scalex + caz*scaley*style.fontShiftY;
    xxx[1][1] = -saz*scalex*style.fontShiftX + caz*scaley;
    xxx[1][2] = -saz*tmp1 + caz*tmp2;

    xxx[2][0] = 
    xxx[2][0] = 
    xxx[2][0] = 0;
    
    //A2*A3*A4

    xxx[2][0] = sax*xxx[1][0];
    xxx[2][1] = sax*xxx[1][1];
    xxx[2][2] = sax*xxx[1][2];

    xxx[1][0] = cax*xxx[1][0];
    xxx[1][1] = cax*xxx[1][1];
    xxx[1][2] = cax*xxx[1][2];

    //A1*A2*A3*A4

    tmp1 = xxx[0][0];
    tmp2 = xxx[0][1];
    double tmp3 = xxx[0][2];
    xxx[0][0] = cay*tmp1 + say*xxx[2][0];
    xxx[0][1] = cay*tmp2 + say*xxx[2][1];
    xxx[0][2] = cay*tmp3 + say*xxx[2][2];

    xxx[2][0] = say*tmp1 - cay*xxx[2][0];
    xxx[2][1] = say*tmp2 - cay*xxx[2][1];
    xxx[2][2] = say*tmp3 - cay*xxx[2][2];

    //S0*A0*A1*A2*A3*A4

    tmp1 = 20000.0*m_target_scale_x;
    xxx[0][0] *= tmp1;
    xxx[0][1] *= tmp1;
    xxx[0][2] *= tmp1;

    tmp1 = 20000.0*m_target_scale_y;
    xxx[1][0] *= tmp1;
    xxx[1][1] *= tmp1;
    xxx[1][2] *= tmp1;

    //A0*A1*A2*A3*A4+B0

    xxx[2][2] += 20000.0;

    double scaled_org_x = org.x+0.5;
    double scaled_org_y = org.y+0.5;

    for (ptrdiff_t i = 0; i < path_data->mPathPoints; i++) {
        double x, y, z, xx;

        xx = path_data->mpPathPoints[i].x;
        y = path_data->mpPathPoints[i].y;

        z = xxx[2][0] * xx + xxx[2][1] * y + xxx[2][2];
        x = xxx[0][0] * xx + xxx[0][1] * y + xxx[0][2];
        y = xxx[1][0] * xx + xxx[1][1] * y + xxx[1][2];

        z = z > 1000.0 ? z : 1000.0;

        x = x / z;
        y = y / z;

        path_data->mpPathPoints[i].x = (long)(x + scaled_org_x);
        path_data->mpPathPoints[i].y = (long)(y + scaled_org_y);
        if (m_round_to_whole_pixel_after_scale_to_target && (m_target_scale_x!=1.0 || m_target_scale_y!=1.0))
        {
            path_data->mpPathPoints[i].x = (path_data->mpPathPoints[i].x + 32)&~63;
            path_data->mpPathPoints[i].y = (path_data->mpPathPoints[i].y + 32)&~63;//fix me: readability
        }
    }
}

bool CWord::CreateOpaqueBox()
{
    if(m_pOpaqueBox) return(true);
    STSStyle style      = m_style.get();
    style.borderStyle   = 0;
    style.outlineWidthX = style.outlineWidthY = 0;
    style.colors[0]     = m_style.get().colors[2];
    style.alpha[0]      = m_style.get().alpha[2];
    int w               = (int)(m_style.get().outlineWidthX + 0.5);
    int h               = (int)(m_style.get().outlineWidthY + 0.5);
    CStringW str;
    str.Format(L"m %d %d l %d %d %d %d %d %d",
               -w,                   -h,
        m_width+w,                   -h,
        m_width+w, m_ascent+m_descent+h,
               -w, m_ascent+m_descent+h);
    m_pOpaqueBox = boost::make_shared<CPolygon>(FwSTSStyle(style), str, 0, 0, 0, 1.0/MAX_SUB_PIXEL, 1.0/MAX_SUB_PIXEL, 0, m_target_scale_x, m_target_scale_y);
    return(!!m_pOpaqueBox);
}

bool CWord::operator==( const CWord& rhs ) const
{
    return (         this ==&rhs) || (
        m_str.GetId()     == rhs.m_str.GetId()     &&
        m_fWhiteSpaceChar == rhs.m_fWhiteSpaceChar &&
        m_fLineBreak      == rhs.m_fLineBreak      &&
        m_style           == rhs.m_style           && //fix me:?
        m_ktype           == rhs.m_ktype           &&
        m_kstart          == rhs.m_kstart          &&
        m_kend            == rhs.m_kend            &&
        m_width           == rhs.m_width           &&
        m_ascent          == rhs.m_ascent          &&
        m_descent         == rhs.m_descent         &&
        m_target_scale_x  == rhs.m_target_scale_x  &&
        m_target_scale_y  == rhs.m_target_scale_y);
    //m_pOpaqueBox
}


// CText

CText::CText( const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend
    , double target_scale_x/*=1.0*/, double target_scale_y/*=1.0*/ )
    : CWord(style, str, ktype, kstart, kend, target_scale_x, target_scale_y)
{
    if(m_str.Get() == L" ")
    {
        m_fWhiteSpaceChar = true;
    }
    SharedPtrTextInfo text_info;
    TextInfoCacheKey text_info_key;
    text_info_key.m_str_id = m_str.GetId();
    text_info_key.m_style  = m_style;
    text_info_key.UpdateHashValue();
    TextInfoMruCache* text_info_cache = CacheManager::GetTextInfoCache();
    POSITION pos = text_info_cache->Lookup(text_info_key);
    if(pos==NULL)
    {
        TextInfo* tmp=DEBUG_NEW TextInfo();
        GetTextInfo(tmp, m_style, m_str.Get());
        text_info.reset(tmp);
        text_info_cache->UpdateCache(text_info_key, text_info);
    }
    else
    {
        text_info = text_info_cache->GetAt(pos);
        text_info_cache->UpdateCache( pos );
    }
    this->m_ascent  = text_info->m_ascent;
    this->m_descent = text_info->m_descent;
    this->m_width   = text_info->m_width;
}

CText::CText( const CText& src ):CWord(src)
{
    m_width = src.m_width;
}

SharedPtrCWord CText::Copy()
{
    return boost::make_shared<CText>(*this);
}

bool CText::Append(const SharedPtrCWord& w)
{
    boost::shared_ptr<CText> p = boost::dynamic_pointer_cast<CText>(w);
    return (p && CWord::Append(w));
}

bool CText::CreatePath(PathData* path_data)
{
    bool succeeded = false;
    FwCMyFont font(m_style);
    HFONT hOldFont = SelectFont(g_hDC, font.get());
    ASSERT(hOldFont);

    int width = 0;
    const CStringW& str = m_str.Get();
    if(m_style.get().fontSpacing || (long)GetVersion() < 0)
    {
        bool bFirstPath = true;
        for(LPCWSTR s = str; *s; s++)
        {
            CSize extent;
            if(!GetTextExtentPoint32W(g_hDC, s, 1, &extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);}
            path_data->PartialBeginPath(g_hDC, bFirstPath);
            bFirstPath = false;
            TextOutW(g_hDC, 0, 0, s, 1);
            path_data->PartialEndPath(g_hDC, width, 0);
            width += extent.cx + (int)m_style.get().fontSpacing;
        }
    }
    else
    {
        CSize extent;
        succeeded = !!GetTextExtentPoint32W(g_hDC, str, str.GetLength(), &extent);
        if(!succeeded)
        {
            SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);
        }
        succeeded = path_data->BeginPath(g_hDC);
        if(!succeeded)
        {
            SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);
        }
        succeeded = !!TextOutW(g_hDC, 0, 0, str, str.GetLength());
        if(!succeeded)
        {
            SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);
        }
        succeeded = path_data->EndPath(g_hDC);
        if(!succeeded)
        {
            SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);
        }
    }
    ASSERT(SelectFont(g_hDC, hOldFont));
    return(true);
}

void CText::GetTextInfo(TextInfo *output, const FwSTSStyle& style, const CStringW& str )
{
    FwCMyFont font(style);
    output->m_ascent = (int)(style.get().fontScaleY/100*font.get().m_ascent);
    output->m_descent = (int)(style.get().fontScaleY/100*font.get().m_descent);

    HFONT hOldFont = SelectFont(g_hDC, font.get());
    if(style.get().fontSpacing || (long)GetVersion() < 0)
    {
        bool bFirstPath = true;
        for(LPCWSTR s = str; *s; s++)
        {
            CSize extent;
            if(!GetTextExtentPoint32W(g_hDC, s, 1, &extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return;}
            output->m_width += extent.cx + (int)style.get().fontSpacing;
        }
        //          m_width -= (int)m_style.get().fontSpacing; // TODO: subtract only at the end of the line
    }
    else
    {
        CSize extent;
        if(!GetTextExtentPoint32W(g_hDC, str, wcslen(str), &extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return;}
        output->m_width += extent.cx;
    }
    output->m_width = (int)(style.get().fontScaleX/100*output->m_width + 4) >> 3;
    SelectFont(g_hDC, hOldFont);
}

// CPolygon

CPolygon::CPolygon( const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend 
    , double scalex, double scaley, int baseline 
    , double target_scale_x/*=1.0*/, double target_scale_y/*=1.0*/
    , bool round_to_whole_pixel_after_scale_to_target/*=false*/)
    : CWord(style, str, ktype, kstart, kend, target_scale_x, target_scale_y, round_to_whole_pixel_after_scale_to_target)
    , m_scalex(scalex), m_scaley(scaley), m_baseline(baseline)
{
    ParseStr();
}

CPolygon::CPolygon(CPolygon& src) : CWord(src)
{
    m_scalex           = src.m_scalex;
    m_scaley           = src.m_scaley;
    m_baseline         = src.m_baseline;
    m_width            = src.m_width;
    m_ascent           = src.m_ascent;
    m_descent          = src.m_descent;
    m_cachedPath       = src.m_cachedPath;
    m_pathTypesOrg.Copy (src.m_pathTypesOrg );
    m_pathPointsOrg.Copy(src.m_pathPointsOrg);
}

bool CPolygon::FreezePreparedPath()
{
    if (m_cachedPath || !m_pathTypesOrg.GetCount()) return true;
    SharedPtrPathData path = boost::make_shared<PathData>();
    if (!CreatePath(path.get())) return false;
    m_cachedPath = path;
    m_pathTypesOrg.RemoveAll();
    m_pathPointsOrg.RemoveAll();
    return true;
}

size_t CPolygon::GetPreparedPathBytes() const
{
    return 128 + sizeof(PathData)
        + m_pathTypesOrg.GetCount()*sizeof(BYTE)
        + m_pathPointsOrg.GetCount()*sizeof(CPoint)
        + (m_cachedPath ? m_cachedPath->mPathPoints*(sizeof(BYTE)+sizeof(POINT)) : 0);
}

CPolygon::~CPolygon()
{
}

SharedPtrCWord CPolygon::Copy()
{
    return boost::make_shared<CPolygon>(*this);
}

bool CPolygon::Append(const SharedPtrCWord& w)
{
    // TODO
    return(false);
}

bool CPolygon::Get6BitFixedPoint(const WCHAR*& cursor, LONG& ret)
{
    const WCHAR* start = cursor;
    WCHAR* end;
    ret = wcstod(cursor, &end) * 64;
    cursor = end;
    TRACE_PARSER(ret);//fix me: use a specific logger for it
    return(end > start);
}

bool CPolygon::GetPOINT(const WCHAR*& cursor, POINT& ret)
{
    return(Get6BitFixedPoint(cursor, ret.x) && Get6BitFixedPoint(cursor, ret.y));
}

bool CPolygon::ParseStr()
{
    if(m_cachedPath || m_pathTypesOrg.GetCount() > 0) return(true);
    // PaintFromRawData retains untransformed, script-scaled vertices. Reuse
    // those vertices before tokenizing the same drawing again. A zero scale
    // can also identify a text key, so keep degenerate drawings on the parser.
    if (m_scalex != 0 && m_scaley != 0)
    {
        PathDataCacheKey key(*this);
        key.UpdateHashValue();
        PathDataMruCache* cache = CacheManager::GetPathDataMruCache();
        POSITION pos = cache->Lookup(key);
        if (pos && cache->GetKeyAt(pos).HasExactPolygonScale(m_scalex, m_scaley))
        {
            SharedPtrConstPathData path = cache->GetAt(pos);
            if (path && path->mPathPoints > 0)
            {
                // Retain the immutable owner, rather than allocating two
                // arrays only to copy the same vertices into each new word.
                m_cachedPath.swap(path);
                cache->UpdateCache(pos);
                UpdateMetrics(false);
                return true;
            }
        }
    }
    CPoint p;
    int j, lastsplinestart = -1, firstmoveto = -1, lastmoveto = -1;
    const auto isCommand=[](WCHAR c) {
        switch(c) { case L'm': case L'n': case L'l': case L'b': case L's': case L'p': case L'c': return true; }
        return false;
    };
    const WCHAR* source=m_str.Get().GetString();
    WCHAR local[256];
    std::vector<WCHAR> spill;
    // Preserve the legacy separators, including literal '*', command letters
    // inside malformed numbers, and the original case/TrimLeft behavior.
    // wcstod still receives a terminated command span; conversion/rounding is
    // unchanged. Most commands need no heap buffer and no CString substring.
    while(*source)
    {
        while(*source==L'*') ++source;
        if(!*source) break;
        const WCHAR* begin=source++;
        while(*source && *source!=L'*' && !isCommand(*source)) ++source;
        const WCHAR c=*begin;
        while(begin<source && (isCommand(*begin) || *begin==L' ')) ++begin;
        const size_t length=source-begin;
        WCHAR* buffer=local;
        if(length>=_countof(local)) { spill.resize(length+1); buffer=spill.data(); }
        memcpy(buffer,begin,length*sizeof(WCHAR)); buffer[length]=0;
        const WCHAR* cursor=buffer;
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
    UpdateMetrics(true);
    return true;
}

void CPolygon::UpdateMetrics(bool scaleCoordinates)
{
    ASSERT(!scaleCoordinates || !m_cachedPath);
    const size_t count = m_cachedPath ? m_cachedPath->mPathPoints : m_pathTypesOrg.GetCount();
    int minx = INT_MAX, miny = INT_MAX, maxx = -INT_MAX, maxy = -INT_MAX;
    for(size_t i = 0; i < count; i++)
    {
        if (scaleCoordinates)
        {
            m_pathPointsOrg[i].x = (int)(m_scalex * m_pathPointsOrg[i].x);
            m_pathPointsOrg[i].y = (int)(m_scaley * m_pathPointsOrg[i].y);
        }
        const POINT& point = m_cachedPath ? m_cachedPath->mpPathPoints[i] : static_cast<const POINT&>(m_pathPointsOrg[i]);
        if(minx > point.x) minx = point.x;
        if(miny > point.y) miny = point.y;
        if(maxx < point.x) maxx = point.x;
        if(maxy < point.y) maxy = point.y;
    }
    m_width      = max(maxx - minx, 0);
    m_ascent     = max(maxy - miny, 0);
    int baseline = (int)(64 * m_scaley * m_baseline);
    m_descent    = baseline;
    m_ascent    -= baseline;
    m_width      = ((int)(m_style.get().fontScaleX/100 * m_width  ) + 4) >> 3;
    m_ascent     = ((int)(m_style.get().fontScaleY/100 * m_ascent ) + 4) >> 3;
    m_descent    = ((int)(m_style.get().fontScaleY/100 * m_descent) + 4) >> 3;
}

bool CPolygon::CreatePath(PathData* path_data)
{
    int len = m_cachedPath ? m_cachedPath->mPathPoints : m_pathTypesOrg.GetCount();
    if(len == 0) return(false);
    if(path_data->mPathPoints != len)
    {
        BYTE* pNewPathTypes = (BYTE*)realloc(path_data->mpPathTypes, len * sizeof(BYTE));
        if (!pNewPathTypes)
        {
            TRACE_PARSER("Overflow!");
            return false;
        }
        path_data->mpPathTypes = pNewPathTypes;

        POINT* pNewPathPoints = (POINT*)realloc(path_data->mpPathPoints, len*sizeof(POINT));
        if (!pNewPathPoints)
        {
            TRACE_PARSER("Overflow!");
            return false;
        }
        path_data->mpPathPoints = pNewPathPoints;
        path_data->mPathPoints  = len;
    }
    memcpy(path_data->mpPathTypes, m_cachedPath ? m_cachedPath->mpPathTypes : m_pathTypesOrg.GetData(), len*sizeof(BYTE));
    memcpy(path_data->mpPathPoints, m_cachedPath ? static_cast<const void*>(m_cachedPath->mpPathPoints) : m_pathPointsOrg.GetData(), len*sizeof(POINT));
    return(true);
}

// CClipper

CClipper::CClipper(CStringW str, CSizeCoor2 size, double scalex, double scaley, bool inverse
    , double target_scale_x/*=1.0*/, double target_scale_y/*=1.0*/)
    : m_polygon( boost::make_shared<CPolygon>(FwSTSStyle(), str, 0, 0, 0, scalex, scaley, 0, target_scale_x, target_scale_y, true) )
    , m_size(size), m_inverse(inverse)
    , m_effectType(-1), m_painted(false)
{
}

CClipper::~CClipper()
{
}

GrayImage2* CClipper::PaintSimpleClipper()
{
    GrayImage2* result = NULL;
    if(m_size.cx < 0 || m_size.cy < 0)
        return result;

    SharedPtrOverlay overlay;
    CWordPaintMachine::PaintBody( m_polygon, CPoint(0, 0), CPoint(0, 0), &overlay );
    int w =  overlay->mOverlayWidth, 
        h =  overlay->mOverlayHeight;
    int x = (overlay->mOffsetX+4)>>3,
        y = (overlay->mOffsetY+4)>>3;
    result = DEBUG_NEW GrayImage2();
    if( !result )
        return result;
    result->data  = overlay->mBody;
    result->pitch = overlay->mOverlayPitch;
    result->size.SetSize(w, h);
    result->left_top.SetPoint(x, y);
    return result;
}

GrayImage2* CClipper::PaintBaseClipper()
{
    GrayImage2* result = NULL;
    //m_pAlphaMask = NULL;
    if (m_size.cx <= 0 || m_size.cy <= 0) {
        return result;
    }

    const size_t alphaMaskSize = size_t(m_size.cx) * m_size.cy;

    SharedPtrOverlay overlay;

    CWordPaintMachine::PaintBody( m_polygon, CPoint(0, 0), CPoint(0, 0), &overlay );
    int w =  overlay->mOverlayWidth,
        h =  overlay->mOverlayHeight;
    int x = (overlay->mOffsetX+4)>>3,
        y = (overlay->mOffsetY+4)>>3;
    int xo = 0, yo = 0;

    if(x < 0) {xo = -x; w -= -x; x = 0;}
    if(y < 0) {yo = -y; h -= -y; y = 0;}
    if(x+w > m_size.cx) w = m_size.cx-x;
    if(y+h > m_size.cy) h = m_size.cy-y;

    if(w <= 0 || h <= 0) return result;

    result = DEBUG_NEW GrayImage2();

    if( !result ) {
        return result;
    }

    result->data.reset( reinterpret_cast<BYTE*>(xy_malloc(alphaMaskSize)), xy_free );
    result->pitch = m_size.cx;
    result->size  = m_size;
    result->left_top.SetPoint(0, 0);

    BYTE * result_data = result->data.get();

    if(!result_data)
    {
        delete result;
        return NULL;
    }

    memset(result_data, (m_inverse ? 0x40 : 0), alphaMaskSize);

    const BYTE* src = overlay->mBody.get() + (overlay->mOverlayPitch * yo + xo);
    BYTE* dst = result_data + m_size.cx * y + x;

    if (m_inverse) {
          for (ptrdiff_t i = 0; i < h; ++i) {
            for (ptrdiff_t wt = 0; wt < w; ++wt) {
                dst[wt] = 0x40 - src[wt];
            }

            src += overlay->mOverlayPitch;
            dst += m_size.cx;
          }
    } else {
       for (ptrdiff_t i = 0; i < h; ++i) {
            memcpy(dst, src, w * sizeof(BYTE));
            src += overlay->mOverlayPitch;
            dst += m_size.cx;
       }
    }
    return result;
}

GrayImage2* CClipper::PaintBannerClipper()
{
    ASSERT(m_polygon);

    int width = static_cast<int>(m_effect.param[2] * m_polygon->m_target_scale_x);//fix me: rounding err
    int w = m_size.cx, 
        h = m_size.cy;

    GrayImage2* result = PaintBaseClipper();
    if(!result)
        return result;

    int da = (64<<8)/width;
    BYTE* am = result->data.get();
    for(int j = 0; j < h; j++, am += w)
    {
        int a = 0;
        int k = min(width, w);
        for(int i = 0; i < k; i++, a += da)
            am[i] = (am[i]*a)>>14;
        a = 0x40<<8;
        k = w-width;
        if(k < 0) {a -= -k*da; k = 0;}
        for(int i = k; i < w; i++, a -= da)
            am[i] = (am[i]*a)>>14;
    }
    return result;
}

GrayImage2* CClipper::PaintScrollClipper()
{
    ASSERT(m_polygon);

    int height = static_cast<int>(m_effect.param[4] * m_polygon->m_target_scale_y);//fix me: rounding err
    int w = m_size.cx,
        h = m_size.cy;

    GrayImage2* result = PaintBaseClipper();
    if(!result)
        return result;

    BYTE* data = result->data.get();

    int da = (64<<8)/height;
    int a = 0;
    int k = (static_cast<int>(m_effect.param[0] * m_polygon->m_target_scale_y)>>3);//fix me: rounding err
    int l = k+height;
    if(k < 0) {a += -k*da; k = 0;}
    if(l > h) {l = h;}
    if(k < h)
    {
        BYTE* am = &data[k*w];
        memset(data, 0, am - data);
        for(int j = k; j < l; j++, a += da)
        {
            for(int i = 0; i < w; i++, am++)
                *am = ((*am)*a)>>14;
        }
    }
    da = -(64<<8)/height;
    a = 0x40<<8;
    l = (static_cast<int>(m_effect.param[1] * m_polygon->m_target_scale_y)>>3);//fix me: rounding err
    k = l-height;
    if(k < 0) {a += -k*da; k = 0;}
    if(l > h) {l = h;}
    if(k < h)
    {
        BYTE* am = &data[k*w];
        int j = k;
        for(; j < l; j++, a += da)
        {
            for(int i = 0; i < w; i++, am++)
                *am = ((*am)*a)>>14;
        }
        memset(am, 0, (h-j)*w);
    }
    return result;
}

GrayImage2* CClipper::Paint()
{
    GrayImage2* result = NULL;
    switch(m_effectType)
    {
    case -1:
        if (!m_inverse)
        {
            result = PaintSimpleClipper();
        }
        else
        {
            result = PaintBaseClipper();
        }
        break;
    case EF_BANNER:
        result = PaintBannerClipper();
        break;
    case EF_SCROLL:
        result = PaintScrollClipper();
        break;
    }
    return result;
}

void CClipper::SetEffect( const Effect& effect, int effectType )
{
    m_effectType = effectType;
    m_effect     = effect;
}

SharedPtrGrayImage2 CClipper::GetAlphaMask( const SharedPtrCClipper& clipper )
{
    SharedPtrGrayImage2 result;
    CClipperPaintMachine paint_machine(clipper);
    paint_machine.Paint(&result);
    return result;
}

// CLine

CLine::~CLine()
{
}

void CLine::Compact()
{
    size_t first = 0, end = m_words.GetCount();
    while (first < end && m_words[first]->m_fWhiteSpaceChar)
        m_width -= m_words[first++]->m_width;
    while (end > first && m_words[end-1]->m_fWhiteSpaceChar)
        m_width -= m_words[--end]->m_width;
    if (first == end) { m_words.Truncate(0); return; }
    const bool singleWord = end-first == 1;
    SharedPtrCWord last;
    size_t count = 0;
    for (size_t i = first; i < end; ++i)
    {
        SharedPtrCWord w = m_words[i];
        if (!last || !last->Append(w))
        {
            last = singleWord || dynamic_cast<CPolygon*>(w.get()) ? w : w->Copy();
            m_words[count++] = last;
        }
    }
    m_words.Truncate(count);
    m_ascent = m_descent = m_borderX = m_borderY = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const SharedPtrCWord& w = m_words[i];
        if(m_ascent  < w->m_ascent)  m_ascent  = w->m_ascent;
        if(m_descent < w->m_descent) m_descent = w->m_descent;
        if(m_borderX < w->m_style.get().outlineWidthX) m_borderX = (int)(w->m_style.get().outlineWidthX+0.5);
        if(m_borderY < w->m_style.get().outlineWidthY) m_borderY = (int)(w->m_style.get().outlineWidthY+0.5);
    }
}

struct PreparedLineDraw
{
    SharedPtrCWord word;
    FwSTSStyle style;
    CPoint p, org, margin;
    CRect clip;
    int alpha, subpixel, width, ascent;
    double targetX,targetY;
    XyColorSpace colorSpace;
    bool correction,tv;
    CompositeDrawItem item;
    boost::shared_ptr<DrawReuseLease> lease;
    explicit PreparedLineDraw(const SharedPtrCWord& w) : word(w),style(w->m_style) {}
};

CRectCoor2 CLine::PaintAll( CompositeDrawItemList* output, const CRectCoor2& clipRect, 
    const CPointCoor2& margin,
    const SharedPtrCClipperPaintMachine &clipper, CPoint p, const CPoint& org, const int time, const int alpha, POSITION outputPos, const boost::shared_ptr<DrawReuseBudget>& budget )
{
    CRectCoor2 bbox(0, 0, 0, 0);
    if (!outputPos) outputPos = output->GetHeadPosition();
    const bool eligible=budget && budget->limit && m_words.GetCount()==1
        && !m_words[0]->m_fLineBreak && m_words[0]->m_ktype==0
        && m_words[0]->m_kstart==0 && m_words[0]->m_kend==0;
    XySubRenderFrameCreater* creator=XySubRenderFrameCreater::GetDefaultCreater();
    XyColorSpace colorSpace; creator->GetColorSpace(&colorSpace);
    const int subpixel=SubpixelPositionControler::GetGlobalControler().GetSubpixelLevel();
    if (eligible && m_drawPlan && m_drawPlan->lease->budget==budget
        && budget->bytes<=budget->limit)
    {
        const PreparedLineDraw& d=*m_drawPlan;
        const CWord& w=*m_words[0];
        if (d.word==m_words[0] && d.style==w.m_style && d.p==p && d.org==org
            && d.margin==margin && d.clip==clipRect && d.alpha==alpha && d.subpixel==subpixel
            && d.width==w.m_width && d.ascent==m_ascent-w.m_ascent
            && d.targetX==w.m_target_scale_x && d.targetY==w.m_target_scale_y
            && d.colorSpace==colorSpace && d.correction==creator->GetVsfilterCompactRgbCorrection()
            && d.tv==creator->GetRgbOutputTvLevel())
        {
            CompositeDrawItem& item=output->GetNext(outputPos);
            item.shadow=d.item.shadow; item.outline=d.item.outline; item.body=d.item.body;
            item.PinOverlays();
            ++budget->hits;
            return CompositeDrawItem::GetDirtyRect(item);
        }
    }
    m_drawPlan.reset();
    if (eligible) ++budget->misses;
    for (size_t i = 0; i < m_words.GetCount(); ++i)
    {
        const SharedPtrCWord& w = m_words[i];
        CompositeDrawItem& outputItem = output->GetNext(outputPos);
        if(w->m_fLineBreak) return(bbox); // should not happen since this class is just a line of text without any breaks
        CPointCoor2 shadowPos, outlinePos, bodyPos, org_coor2;

        double shadowPos_x = p.x + w->m_style.get().shadowDepthX;
        double shadowPos_y = p.y + w->m_style.get().shadowDepthY + m_ascent - w->m_ascent;
        outlinePos = CPoint(p.x, p.y + m_ascent - w->m_ascent);
        bodyPos    = CPoint(p.x, p.y + m_ascent - w->m_ascent);

        shadowPos.x   = static_cast<int>(w->m_target_scale_x * shadowPos_x + 0.5) + margin.x;
        shadowPos.y   = static_cast<int>(w->m_target_scale_y * shadowPos_y + 0.5) + margin.y;
        outlinePos.x  =                  w->m_target_scale_x * outlinePos.x       + margin.x;
        outlinePos.y  =                  w->m_target_scale_y * outlinePos.y       + margin.y;
        bodyPos.x     =                  w->m_target_scale_x * bodyPos.x          + margin.x;
        bodyPos.y     =                  w->m_target_scale_y * bodyPos.y          + margin.y;
        org_coor2.x   =                  w->m_target_scale_x * org.x              + margin.x;//fix me: move it out of this loop
        org_coor2.y   =                  w->m_target_scale_y * org.y              + margin.y;

        bool hasShadow  =   w->m_style.get().shadowDepthX != 0 || w->m_style.get().shadowDepthY != 0;
        bool hasOutline = ((w->m_style.get().outlineWidthX*w->m_target_scale_x+0.5>=1.0) ||
                           (w->m_style.get().outlineWidthY*w->m_target_scale_y+0.5>=1.0)) && 
                          !(w->m_ktype == 2 && time < w->m_kstart);
        bool hasBody = true;

        SharedPtrOverlayPaintMachine shadow_pm, outline_pm, body_pm;
        CWordPaintMachine::CreatePaintMachines(w, shadowPos, outlinePos, bodyPos, org_coor2,
            hasShadow  ? &shadow_pm  : NULL, 
            hasOutline ? &outline_pm : NULL, 
            hasBody    ? &body_pm    : NULL,budget);

        //shadow
        if(hasShadow)
        {
            //NOTE: Calculation of shadow's alpha value is different from outline's and body's in the way they're rounded.
            // Shadow is rounded to invisible while outline and body is rounded to visible.
            // Should we change it to be consist with one rounding polocy?
            DWORD a = 0xff - w->m_style.get().alpha[3];
            if(alpha > 0) a = MulDiv(a, 0xff - alpha, 0xff);
            COLORREF shadow = revcolor(w->m_style.get().colors[3]) | (a<<24);

            DWORD sw[6] = {shadow, -1};
            sw[0] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[0]);
            if(w->m_style.get().borderStyle == 0)
            {
                outputItem.shadow = DrawItem::CreateDrawItem(shadow_pm, clipRect, clipper, shadowPos.x, shadowPos.y, sw,
                    w->m_ktype > 0 || w->m_style.get().alpha[0] < 0xff,
                    hasOutline);
            }
            else if(w->m_style.get().borderStyle == 1)
            {
                outputItem.shadow = DrawItem::CreateDrawItem( shadow_pm, clipRect, clipper, shadowPos.x, shadowPos.y, sw, true, false);
            }
        }
        //outline
        if(hasOutline)
        {
            DWORD aoutline = w->m_style.get().alpha[2];
            if(alpha > 0) aoutline += MulDiv(alpha, 0xff - w->m_style.get().alpha[2], 0xff);
            COLORREF outline = revcolor(w->m_style.get().colors[2]) | ((0xff-aoutline)<<24);
            DWORD sw[6] = {outline, -1};
            sw[0] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[0]);
            if(w->m_style.get().borderStyle == 0)
            {
                outputItem.outline = DrawItem::CreateDrawItem(outline_pm, clipRect, clipper, outlinePos.x, outlinePos.y, sw,
                    !w->m_style.get().alpha[0] && !w->m_style.get().alpha[1] && !alpha, true);
            }
            else if(w->m_style.get().borderStyle == 1)
            {
                outputItem.outline = DrawItem::CreateDrawItem(outline_pm, clipRect, clipper, outlinePos.x, outlinePos.y, sw, true, false);
            }
        }
        //body
        if(hasBody)
        {
            // colors
            DWORD aprimary   = w->m_style.get().alpha[0];
            DWORD asecondary = w->m_style.get().alpha[1];
            if(alpha > 0) aprimary   += MulDiv(alpha, 0xff - w->m_style.get().alpha[0], 0xff),
                          asecondary += MulDiv(alpha, 0xff - w->m_style.get().alpha[1], 0xff);
            COLORREF primary   = revcolor(w->m_style.get().colors[0]) | ((0xff-aprimary  )<<24);
            COLORREF secondary = revcolor(w->m_style.get().colors[1]) | ((0xff-asecondary)<<24);
            DWORD sw[6] = {primary, 0, secondary};
            // karaoke
            double t;
            if(w->m_ktype == 0 || w->m_ktype == 2)
            {
                t = time < w->m_kstart ? 0 : 1;
            }
            else if(w->m_ktype == 1)
            {
                if(time < w->m_kstart) t = 0;
                else if(time < w->m_kend)
                {
                    t = 1.0 * (time - w->m_kstart) / (w->m_kend - w->m_kstart);
                    double angle = fmod(w->m_style.get().fontAngleZ, 360.0);
                    if(angle > 90 && angle < 270)
                    {
                        t = 1-t;
                        COLORREF tmp = sw[0];
                        sw[0] = sw[2];
                        sw[2] = tmp;
                    }
                }
                else t = 1.0;
            }
            if(t >= 1)
            {
                sw[1] = 0xffffffff;
            }
            sw[3] = (int)( (w->m_style.get().outlineWidthX + t*w->m_width)*w->m_target_scale_x ) >> 3;
            sw[4] = sw[2];
            sw[5] = 0x00ffffff;
            sw[0] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[0]);
            sw[2] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[2]);
            sw[4] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[4]);
            outputItem.body = DrawItem::CreateDrawItem(body_pm, clipRect, clipper, bodyPos.x, bodyPos.y, sw, true, false);
        }
        bbox |= CompositeDrawItem::GetDirtyRect(outputItem);
        if (budget) budget->drawAllocations+=!!outputItem.shadow+!!outputItem.outline+!!outputItem.body;
        if (eligible && budget->bytes+DrawReuseLease::CHARGE<=budget->limit)
        {
            bool valid=true;
            SharedPtrDrawItem layers[]={outputItem.shadow,outputItem.outline,outputItem.body};
            for (unsigned j=0;j<3;++j) if (layers[j] && !layers[j]->SealBounds()) valid=false;
            if (valid)
            {
                boost::shared_ptr<PreparedLineDraw> d=boost::make_shared<PreparedLineDraw>(w);
                d->p=p; d->org=org; d->margin=margin; d->clip=clipRect; d->alpha=alpha;
                d->subpixel=subpixel; d->width=w->m_width; d->ascent=m_ascent-w->m_ascent;
                d->targetX=w->m_target_scale_x; d->targetY=w->m_target_scale_y;
                d->colorSpace=colorSpace; d->correction=creator->GetVsfilterCompactRgbCorrection();
                d->tv=creator->GetRgbOutputTvLevel();
                d->item.shadow=outputItem.shadow; d->item.outline=outputItem.outline; d->item.body=outputItem.body;
                outputItem.PinOverlays();
                d->lease=boost::make_shared<DrawReuseLease>(budget);
                for (unsigned j=0;j<3;++j) if (layers[j])
                {
                    layers[j]->retention=d->lease;
                    layers[j]->overlay_paint_machine->EnableWeakRetention();
                }
                m_drawPlan=d;
            }
        }
        p.x += w->m_width;
    }
    return(bbox);
}

void CLine::AddWord2Tail( const SharedPtrCWord& words )
{
    m_words.AddTail(words);
}

bool CLine::IsEmpty()
{
    return m_words.GetCount() == 0;
}

int CLine::GetWordCount()
{
    return static_cast<int>(m_words.GetCount());
}

// CSubtitle

CSubtitle::CSubtitle() : m_inlineEffectUsed(false), m_cachePosition(NULL)
{
    memset(m_effects, 0, sizeof(Effect*)*EF_NUMBEROFEFFECTS);
    m_clipInverse = false;
    m_scalex = m_scaley = 1;
    m_fAnimated2 = false;

    m_target_scale_x = m_target_scale_y = 1.0;
    m_hard_position_level = -1;
}

CSubtitle::~CSubtitle()
{
    Empty();
}

void CSubtitle::Empty()
{
    for (size_t i = 0; i < m_lines.GetCount(); ++i)
        if (m_lines[i] != &m_inlineLine) delete m_lines[i];
    m_inlineLine.Empty();
    m_lines.Truncate(0);
    m_words.Truncate(0);
    m_preparedContext.reset();
    for(int i = 0; i < EF_NUMBEROFEFFECTS; i++)
        if(m_effects[i] && m_effects[i] != &m_inlineEffect) delete m_effects[i];
    memset(m_effects, 0, sizeof(Effect*)*EF_NUMBEROFEFFECTS);
    m_inlineEffectUsed = false;
}

Effect* CSubtitle::CreateEffect(eftype type)
{
    ASSERT(type >= 0 && type < EF_NUMBEROFEFFECTS);
    if (m_effects[type]) return m_effects[type];
    Effect* effect;
    if (!m_inlineEffectUsed)
    {
        effect = &m_inlineEffect;
        memset(effect->param, 0, sizeof(effect->param));
        memset(effect->t, 0, sizeof(effect->t));
        m_inlineEffectUsed = true;
    }
    else effect = DEBUG_NEW Effect();
    effect->type = type;
    m_effects[type] = effect;
    return effect;
}

int CSubtitle::GetFullWidth()
{
    int width = 0;
    for (size_t i = 0; i < m_words.GetCount(); ++i) width += m_words[i]->m_width;
    return(width);
}

int CSubtitle::GetFullLineWidth(size_t pos)
{
    int width = 0;
    while(pos < m_words.GetCount())
    {
        const SharedPtrCWord& w = m_words[pos++];
        if(w->m_fLineBreak) break;
        width += w->m_width;
    }
    return(width);
}

int CSubtitle::GetWrapWidth(size_t pos, int maxwidth)
{
    if(m_wrapStyle == 0 || m_wrapStyle == 3)
    {
        if(maxwidth > 0)
        {
            int fullwidth = GetFullLineWidth(pos);
            int minwidth = fullwidth / ((abs(fullwidth) / maxwidth) + 1);
            int width = 0, wordwidth = 0;
            while(pos < m_words.GetCount() && width < minwidth)
            {
                const SharedPtrCWord& w = m_words[pos++];
                wordwidth = w->m_width;
                if(abs(width + wordwidth) < abs(maxwidth)) width += wordwidth;
            }
            if (m_wrapStyle == 3 && width < fullwidth && fullwidth - width + wordwidth < maxwidth) {
                width -= wordwidth;
            }
            maxwidth = width;
        }
    }
    else if(m_wrapStyle == 1)
    {
//      maxwidth = maxwidth;
    }
    else if(m_wrapStyle == 2)
    {
        maxwidth = INT_MAX;
    }
    return(maxwidth);
}

CLine* CSubtitle::GetNextLine(size_t& pos, int maxwidth)
{
    if(pos == m_words.GetCount()) return(NULL);
    CLine* ret = m_lines.GetCount() ? DEBUG_NEW CLine() : &m_inlineLine;
    if(!ret) return(NULL);
    ret->m_width = ret->m_ascent = ret->m_descent = ret->m_borderX = ret->m_borderY = 0;
    maxwidth = GetWrapWidth(pos, maxwidth);
    bool fEmptyLine = true;
    while(pos < m_words.GetCount())
    {
        const SharedPtrCWord& w = m_words[pos++];
        if(ret->m_ascent < w->m_ascent) ret->m_ascent = w->m_ascent;
        if(ret->m_descent < w->m_descent) ret->m_descent = w->m_descent;
        if(ret->m_borderX < w->m_style.get().outlineWidthX) ret->m_borderX = (int)(w->m_style.get().outlineWidthX+0.5);
        if(ret->m_borderY < w->m_style.get().outlineWidthY) ret->m_borderY = (int)(w->m_style.get().outlineWidthY+0.5);
        if(w->m_fLineBreak)
        {
            if(fEmptyLine) {ret->m_ascent /= 2; ret->m_descent /= 2; ret->m_borderX = ret->m_borderY = 0;}
            ret->Compact();
            return(ret);
        }
        fEmptyLine = false;
        bool fWSC = w->m_fWhiteSpaceChar;
        int width = w->m_width;
        size_t pos2 = pos;
        while(pos2 < m_words.GetCount())
        {
            if(m_words[pos2]->m_fWhiteSpaceChar != fWSC
                    || m_words[pos2]->m_fLineBreak) break;
            const SharedPtrCWord& w2 = m_words[pos2++];
            width += w2->m_width;
        }
        if((ret->m_width += width) <= maxwidth || ret->IsEmpty())
        {
            ret->AddWord2Tail(w);
            while(pos != pos2)
            {
                ret->AddWord2Tail(m_words[pos++]);
            }
            pos = pos2;
        }
        else
        {
            --pos;
            ret->m_width -= width;
            break;
        }
    }
    ret->Compact();
    return(ret);
}

void CSubtitle::CreateClippers( CSize size1, const CSizeCoor2& size_scale_to )
{
    size1.cx >>= 3;
    size1.cy >>= 3;
    if(m_effects[EF_BANNER] && m_effects[EF_BANNER]->param[2])
    {
        int w = size1.cx, h = size1.cy;
        if(!m_pClipper)
        {
            CStringW str;
            str.Format(L"m %d %d l %d %d %d %d %d %d", 0, 0, w, 0, w, h, 0, h);
            m_pClipper.reset( DEBUG_NEW CClipper(str, size_scale_to, 1, 1, false, m_target_scale_x, m_target_scale_y) );
            if(!m_pClipper) return;
        }
        m_pClipper->SetEffect( *m_effects[EF_BANNER], EF_BANNER );
    }
    else if(m_effects[EF_SCROLL] && m_effects[EF_SCROLL]->param[4])
    {
        int height = m_effects[EF_SCROLL]->param[4];
        int w = size1.cx, h = size1.cy;
        if(!m_pClipper)
        {
            CStringW str;
            str.Format(L"m %d %d l %d %d %d %d %d %d", 0, 0, w, 0, w, h, 0, h);
            m_pClipper.reset( DEBUG_NEW CClipper(str, size_scale_to, 1, 1, false, m_target_scale_x, m_target_scale_y) ); 
            if(!m_pClipper) return;
        }
        m_pClipper->SetEffect(*m_effects[EF_SCROLL], EF_SCROLL);
    }
}

void CSubtitle::MakeLines(CSize size, CRect marginRect)
{
    CSize spaceNeeded(0, 0);
    bool fFirstLine = true;
    m_topborder = m_bottomborder = 0;
    CLine* l = NULL;
    size_t pos = 0;
    if (m_lines.GetCount() == 0 && m_words.GetCount() == 1 &&
        !m_words[0]->m_fWhiteSpaceChar && !m_words[0]->m_fLineBreak)
    {
        // The general algorithm always accepts the first non-whitespace word,
        // even if it exceeds the wrap width. No merge or trimming is possible.
        const SharedPtrCWord& word = m_words[0];
        l = &m_inlineLine;
        ASSERT(l->IsEmpty());
        l->m_width = word->m_width;
        l->m_ascent = max(0, word->m_ascent);
        l->m_descent = max(0, word->m_descent);
        l->m_borderX = l->m_borderY = 0;
        if (l->m_borderX < word->m_style.get().outlineWidthX)
            l->m_borderX = (int)(word->m_style.get().outlineWidthX+0.5);
        if (l->m_borderY < word->m_style.get().outlineWidthY)
            l->m_borderY = (int)(word->m_style.get().outlineWidthY+0.5);
        l->AddWord2Tail(word);
        m_lines.AddTail(l);
        m_topborder = l->m_borderY;
        spaceNeeded.cx = max(0, l->m_width+l->m_borderX);
        spaceNeeded.cy = l->m_ascent+l->m_descent;
    }
    else while(pos < m_words.GetCount())
    {
        l = GetNextLine(pos, size.cx - marginRect.left - marginRect.right);
        if(!l) break;
        if(fFirstLine) {m_topborder = l->m_borderY; fFirstLine = false;}
        spaceNeeded.cx = max(l->m_width+l->m_borderX, spaceNeeded.cx);
        spaceNeeded.cy += l->m_ascent + l->m_descent;
        m_lines.AddTail(l);
    }
    if(l) m_bottomborder = l->m_borderY;
    m_rect = CRect(
                 CPoint((m_scrAlignment%3) == 1 ? marginRect.left
                        : (m_scrAlignment%3) == 2 ? (marginRect.left + (size.cx - marginRect.right) - spaceNeeded.cx + 1) / 2
                        : (size.cx - marginRect.right - spaceNeeded.cx),
                        m_scrAlignment <= 3 ? (size.cy - marginRect.bottom - spaceNeeded.cy)
                        : m_scrAlignment <= 6 ? (marginRect.top + (size.cy - marginRect.bottom) - spaceNeeded.cy + 1) / 2
                        : marginRect.top),
                 spaceNeeded);
}

SharedPreparedSubtitleLayout CSubtitle::PrepareLayout(size_t available, CSize size,
    const CRect& margin, int polygon, int baseline) const
{
    if (!m_words.GetCount() || m_words.GetCount() > 128 || m_lines.GetCount() > 128)
        return SharedPreparedSubtitleLayout();
    const FwSTSStyle& style = m_words[0]->m_style;
    if (style.get().fontName.GetLength() > 128) return SharedPreparedSubtitleLayout();
    size_t charge = sizeof(PreparedSubtitleLayout) + 128;
    std::map<CWord*, SharedPtrCWord> prototypes;
    const auto account = [&](const SharedPtrCWord& word) {
        if (word->m_style != style) return false;
        if (prototypes.insert(std::make_pair(word.get(), SharedPtrCWord())).second)
            charge += word->GetPreparedStorageBytes();
        return charge <= min(available, size_t(512*1024));
    };
    for (size_t i=0; i<m_words.GetCount(); ++i)
        if (!account(m_words[i])) return SharedPreparedSubtitleLayout();
    for (size_t i=0; i<m_lines.GetCount(); ++i)
        for (size_t j=0; j<m_lines[i]->m_words.GetCount(); ++j)
        {
            const SharedPtrCWord& word = m_lines[i]->m_words[j];
            // Reserve the last-appearance word plus bounded paint keys/box,
            // including referenced path bytes even if shared with a prototype.
            charge += word->GetPreparedStorageBytes() + 4096 + sizeof(SharedPtrCWord);
            if (!account(word)) return SharedPreparedSubtitleLayout();
        }
    boost::shared_ptr<PreparedSubtitleLayout> layout = boost::make_shared<PreparedSubtitleLayout>(style);
    layout->style = style;
    layout->size = size; layout->margin = margin; layout->rect = m_rect;
    layout->alignment = m_scrAlignment; layout->wrap = m_wrapStyle;
    layout->polygon = polygon; layout->baseline = baseline;
    layout->ktype = m_words[0]->m_ktype; layout->kstart = m_words[0]->m_kstart; layout->kend = m_words[0]->m_kend;
    layout->topBorder = m_topborder; layout->bottomBorder = m_bottomborder;
    layout->scaleX = m_scalex; layout->scaleY = m_scaley;
    layout->targetX = m_target_scale_x; layout->targetY = m_target_scale_y;
    for (auto& item : prototypes)
    {
        item.second = item.first->Copy();
        item.second->m_pOpaqueBox.reset();
        CPolygon* drawing = dynamic_cast<CPolygon*>(item.second.get());
        if (drawing && !drawing->FreezePreparedPath()) return SharedPreparedSubtitleLayout();
    }
    layout->rawWords.reserve(m_words.GetCount());
    for (size_t i=0; i<m_words.GetCount(); ++i) layout->rawWords.push_back(prototypes[m_words[i].get()]);
    layout->lines.resize(m_lines.GetCount());
    charge += layout->rawWords.capacity()*sizeof(SharedPtrCWord)
        + layout->lines.capacity()*sizeof(PreparedSubtitleLayout::Line);
    for (size_t i=0; i<m_lines.GetCount(); ++i)
    {
        const CLine& line = *m_lines[i];
        PreparedSubtitleLayout::Line& copy = layout->lines[i];
        copy.width = line.m_width; copy.ascent = line.m_ascent; copy.descent = line.m_descent;
        copy.borderX = line.m_borderX; copy.borderY = line.m_borderY;
        copy.words.reserve(line.m_words.GetCount());
        for (size_t j=0; j<line.m_words.GetCount(); ++j) copy.words.push_back(prototypes[line.m_words[j].get()]);
        charge += copy.words.capacity()*sizeof(SharedPtrCWord) + 64;
    }
    if (charge > min(available, size_t(512*1024))) return SharedPreparedSubtitleLayout();
    layout->bytes = charge;
    return layout;
}

static void PrepareLayoutAppearance(const PreparedSubtitleLayout& layout, const FwSTSStyle& style)
{
    size_t wordCount = 0;
    for (const auto& line : layout.lines) wordCount += line.words.size();
    if (layout.renderedStyle != style || layout.renderedWords.GetCount() != wordCount)
    {
        CInlineList<SharedPtrCWord> replacement;
        replacement.Reserve(wordCount);
        for (const auto& line : layout.lines)
            for (const auto& prototype : line.words)
            {
                SharedPtrCWord word = prototype->Copy();
                word->m_style = style;
                word->m_pOpaqueBox.reset();
                replacement.AddTail(word);
            }
        layout.renderedWords.Swap(replacement);
        layout.renderedStyle = style;
    }
}

bool CSubtitle::ReuseLayout(const PreparedSubtitleLayout& layout, const FwSTSStyle& style,
    CSize size, const CRect& margin, int polygon, int baseline, int ktype, int kstart, int kend)
{
    // Compare every layout dependency exactly. Color/alpha, blur, shadow,
    // rotation and shear affect painting; fontsize, spacing, scale and outline
    // still require layout. Never substitute epsilon comparisons here.
    STSStyle geometry(style.get());
    memcpy(geometry.colors, layout.style.get().colors, sizeof(geometry.colors));
    memcpy(geometry.alpha, layout.style.get().alpha, sizeof(geometry.alpha));
    geometry.fBlur = layout.style.get().fBlur; geometry.fGaussianBlur = layout.style.get().fGaussianBlur;
    geometry.shadowDepthX = layout.style.get().shadowDepthX; geometry.shadowDepthY = layout.style.get().shadowDepthY;
    geometry.fontAngleX = layout.style.get().fontAngleX; geometry.fontAngleY = layout.style.get().fontAngleY;
    geometry.fontAngleZ = layout.style.get().fontAngleZ;
    geometry.fontShiftX = layout.style.get().fontShiftX; geometry.fontShiftY = layout.style.get().fontShiftY;
    if (geometry != layout.style.get() || size != layout.size || margin != layout.margin
        || abs(m_scrAlignment) != layout.alignment || m_wrapStyle != layout.wrap
        || polygon != layout.polygon || baseline != layout.baseline
        || ktype != layout.ktype || kstart != layout.kstart || kend != layout.kend
        || m_scalex != layout.scaleX || m_scaley != layout.scaleY
        || m_target_scale_x != layout.targetX || m_target_scale_y != layout.targetY)
        return false;
    ASSERT(!m_lines.GetCount() && !m_words.GetCount());
    // Raw words are immutable metric/source prototypes; only the final lines
    // are rendered. Preserve the original raw count for reconstruction policy.
    for (size_t i=0; i<layout.rawWords.size(); ++i) m_words.AddTail(layout.rawWords[i]);
    PrepareLayoutAppearance(layout, style);
    size_t renderedWord = 0;
    for (size_t i=0; i<layout.lines.size(); ++i)
    {
        const PreparedSubtitleLayout::Line& source = layout.lines[i];
        CLine* line = i ? DEBUG_NEW CLine() : &m_inlineLine;
        m_lines.AddTail(line);
        line->m_width = source.width; line->m_ascent = source.ascent; line->m_descent = source.descent;
        line->m_borderX = source.borderX; line->m_borderY = source.borderY;
        for (size_t j=0; j<source.words.size(); ++j)
        {
            line->AddWord2Tail(layout.renderedWords[renderedWord++]);
        }
    }
    m_rect = layout.rect; m_topborder = layout.topBorder; m_bottomborder = layout.bottomBorder;
    return true;
}

void CSubtitle::UpdateLayoutAppearance(const PreparedSubtitleLayout& layout, const FwSTSStyle& style)
{
    PrepareLayoutAppearance(layout, style);
    ASSERT(m_lines.GetCount() == layout.lines.size());
    size_t word = 0;
    for (size_t i=0; i<m_lines.GetCount(); ++i)
    {
        CLine& line = *m_lines[i];
        ASSERT(line.m_words.GetCount() == layout.lines[i].words.size());
        for (size_t j=0; j<line.m_words.GetCount(); ++j)
            line.m_words[j] = layout.renderedWords[word++];
    }
}

size_t CSubtitle::PrepareStaticDrawing(size_t available, size_t sourceCharacters)
{
    if (m_words.GetCount()==0 || m_words.GetCount()>128 || m_lines.GetCount()>128 || m_pClipper
        || m_effects[EF_BANNER] || m_effects[EF_SCROLL] || m_effects[EF_ORG]) return 0;
    size_t charge=sizeof(CSubtitle)+sizeof(PreparedEventContext)+(sourceCharacters+1)*sizeof(WCHAR)+8192;
    for(size_t i=0;i<m_words.GetCount();++i)
    {
        CPolygon* polygon=dynamic_cast<CPolygon*>(m_words[i].get());
        if (!polygon || polygon->m_style.get().fontName.GetLength()>128) return 0;
        // The immutable vertices remain in this guarded state, not a new
        // unbounded global path owner. No painting occurs during preparation.
        if (!polygon->FreezePreparedPath()) return 0;
        charge+=polygon->GetPreparedStorageBytes()+polygon->GetPreparedPathBytes()+4096;
    }
    for(size_t i=0;i<m_lines.GetCount();++i)
        charge+=sizeof(CLine)+m_lines[i]->GetWordCount()*(sizeof(SharedPtrCWord)+64)+128;
    return charge<=min(available,size_t(512*1024)) ? charge : 0;
}

POSITION CSubtitle::GetHeadLinePosition()
{
    return m_lines.GetCount() ? reinterpret_cast<POSITION>(1) : NULL;
}

CLine* CSubtitle::GetNextLine( POSITION& pos )
{
    const size_t index = reinterpret_cast<size_t>(pos)-1;
    ASSERT(index < m_lines.GetCount());
    CLine* line = m_lines[index];
    pos = index+1 < m_lines.GetCount() ? reinterpret_cast<POSITION>(index+2) : NULL;
    return line;
}

CSubtitle* CSubtitlePool::Acquire()
{
    static_assert(sizeof(CSubtitlePool) + MAX_ITEMS*sizeof(CSubtitle) < 1024*1024,
        "Empty subtitle storage must remain below one MiB");
    if (m_count) { ++m_hits; return m_items[--m_count]; }
    ++m_misses;
    return DEBUG_NEW CSubtitle();
}

void CSubtitlePool::Recycle(CSubtitle* subtitle)
{
    if (!subtitle) return;
    if (m_count == MAX_ITEMS) { delete subtitle; return; }
    // End all old ownership before reconstructing the default state. Cached
    // draw items may still own words, but never point to this event or its line.
    subtitle->~CSubtitle();
    m_items[m_count++] = ::new (static_cast<void*>(subtitle)) CSubtitle();
}

void CSubtitlePool::Clear()
{
    while (m_count) delete m_items[--m_count];
}

// CScreenLayoutAllocator

void CScreenLayoutAllocator::Empty()
{
    m_subrects.RemoveAll();
}

void CScreenLayoutAllocator::AdvanceToSegment(int segment, const CAtlArray<int>& sa)
{
    TRACE_RENDERER_REQUEST("Begin AdvanceToSegment. m_subrects.size:"
        <<m_subrects.GetCount()<<" sa.size:"<<sa.GetCount());
    POSITION pos = m_subrects.GetHeadPosition();
    while(pos)
    {
        POSITION prev = pos;
        SubRect& sr = m_subrects.GetNext(pos);
        bool fFound = false;
        if(abs(sr.segment - segment) <= 1) // using abs() makes it possible to play the subs backwards, too :)
        {
            for(size_t i = 0; i < sa.GetCount() && !fFound; i++)
            {
                if(sa[i] == sr.entry)
                {
                    sr.segment = segment;
                    fFound = true;
                }
            }
        }
        if(!fFound) m_subrects.RemoveAt(prev);
    }
}

CRect CScreenLayoutAllocator::AllocRect(CSubtitle* s, int segment, int entry, int layer, int collisions)
{
    // TODO: handle collisions == 1 (reversed collisions)
    POSITION pos = m_subrects.GetHeadPosition();
    while(pos)
    {
        SubRect& sr = m_subrects.GetNext(pos);
        if(sr.segment == segment && sr.entry == entry)
        {
            return(sr.r + CRect(0, -s->m_topborder, 0, -s->m_bottomborder));
        }
    }
    CRect r = s->m_rect + CRect(0, s->m_topborder, 0, s->m_bottomborder);
    bool fSearchDown = s->m_scrAlignment > 3;
    bool fOK;
    do
    {
        fOK = true;
        pos = m_subrects.GetHeadPosition();
        while(pos)
        {
            SubRect& sr = m_subrects.GetNext(pos);
            if(layer == sr.layer && !(r & sr.r).IsRectEmpty())
            {
                if(fSearchDown)
                {
                    r.bottom = sr.r.bottom + r.Height();
                    r.top    = sr.r.bottom;
                }
                else
                {
                    r.top    = sr.r.top - r.Height();
                    r.bottom = sr.r.top;
                }
                fOK = false;
            }
        }
    }
    while(!fOK);
    SubRect sr;
    sr.r       = r;
    sr.segment = segment;
    sr.entry   = entry;
    sr.layer   = layer;
    m_subrects.AddTail(sr);
    return(sr.r + CRect(0, -s->m_topborder, 0, -s->m_bottomborder));
}

// CRenderedTextSubtitle

CAtlMap<CStringW, CRenderedTextSubtitle::AssCmdType, CStringElementTraits<CStringW>> CRenderedTextSubtitle::m_cmdMap;

std::size_t CRenderedTextSubtitle::s_max_cache_size = SIZE_MAX;

std::size_t CRenderedTextSubtitle::SetMaxCacheSize( std::size_t max_cache_size )
{
    s_max_cache_size = max_cache_size;
    XY_LOG_INFO("MAX_CACHE_SIZE: "<<s_max_cache_size);
    return s_max_cache_size;
}

CAtlArray<AssCmdPosLevel> CRenderedTextSubtitle::m_cmd_pos_level;

// Parameter storage keeps scalar tags small. Spill slots are constructed only
// when used, and copy-on-write strings preserve source and cached-program owners.
struct CRenderedTextSubtitle::AssTagParams::Spill
{
    size_t capacity;
    CStringW* Values() { return reinterpret_cast<CStringW*>(this+1); }
    const CStringW* Values() const { return reinterpret_cast<const CStringW*>(this+1); }
};
CRenderedTextSubtitle::AssTagParams::~AssTagParams()
{
    for(size_t i=1;i<m_count;++i) m_spill->Values()[i-1].~CStringW();
    ::operator delete(m_spill);
    // m_first releases its owner directly through its member destructor.
}
size_t CRenderedTextSubtitle::AssTagParams::GetCapacity() const
{ return 1+(m_spill ? m_spill->capacity : 0); }
size_t CRenderedTextSubtitle::AssTagParams::GetStorageBytes() const
{ return m_spill ? sizeof(Spill)+m_spill->capacity*sizeof(CStringW)+64 : 0; }
const CStringW& CRenderedTextSubtitle::AssTagParams::SpillAt(size_t index) const
{ return m_spill->Values()[index]; }
void CRenderedTextSubtitle::AssTagParams::Reserve(size_t count)
{
    if(count<=GetCapacity()) return;
    const size_t capacity=count-1;
    if(capacity>(SIZE_MAX-sizeof(Spill))/sizeof(CStringW)) throw std::bad_alloc();
    static_assert(sizeof(Spill)%alignof(CStringW)==0,"Unaligned parameter spill");
    Spill* fresh=::new (::operator new(sizeof(Spill)+capacity*sizeof(CStringW))) Spill();
    fresh->capacity=capacity;
    size_t copied=0;
    try
    {
        for(size_t i=1;i<m_count;++i,++copied)
            ::new (static_cast<void*>(fresh->Values()+copied)) CStringW(m_spill->Values()[i-1]);
    }
    catch(...)
    {
        while(copied) fresh->Values()[--copied].~CStringW();
        ::operator delete(fresh);
        throw;
    }
    for(size_t i=1;i<m_count;++i) m_spill->Values()[i-1].~CStringW();
    ::operator delete(m_spill);
    m_spill=fresh;
}
void CRenderedTextSubtitle::AssTagParams::Add(const CStringW& value)
{
    if(!m_count) { m_first=value; m_count=1; return; }
    if(m_count==GetCapacity())
    {
        const size_t capacity=GetCapacity()-1;
        if(capacity>(SIZE_MAX-1)/2) throw std::bad_alloc();
        if(m_spill)
        {
            // Own aliased spill arguments before growth releases their slot.
            CStringW owned(value);
            Reserve(1+max(size_t(3),capacity*2));
            ::new (static_cast<void*>(m_spill->Values()+m_count-1)) CStringW(owned);
        }
        else
        {
            // The inline first argument and all external values stay in place.
            Reserve(4);
            ::new (static_cast<void*>(m_spill->Values())) CStringW(value);
        }
    }
    else ::new (static_cast<void*>(m_spill->Values()+m_count-1)) CStringW(value);
    ++m_count;
}
void CRenderedTextSubtitle::AssTagParams::Truncate(size_t count)
{
    ASSERT(count<=m_count);
    for(size_t i=m_count;i>max(count,size_t(1));--i) m_spill->Values()[i-2].~CStringW();
    if(!count) m_first=CStringW();
    m_count=count;
}

// The first allocation combines list bookkeeping and eight stable nodes.
// Further blocks hold sixteen nodes, avoiding an allocation per tag and an
// embedded ATL list header in every scalar tag. There is no global arena/pool.
struct CRenderedTextSubtitle::AssTagList::Storage : Header
{
    typedef AssTag Node;
    struct Block { Block* previous; Node* nodes; size_t used,capacity; };
    Block first;
    Block* tail;
    Node* last;
    size_t count,capacity,bytes,blocks;
    static size_t Aligned(size_t size) { return (size+alignof(Node)-1)&~(alignof(Node)-1); }
    static Storage* Create()
    {
        static_assert(alignof(Node)<=alignof(std::max_align_t),"Tag node needs aligned allocation");
        const size_t header=Aligned(sizeof(Storage));
        const size_t bytes=header+8*sizeof(Node);
        Storage* storage=::new (::operator new(bytes)) Storage();
        storage->first.nodes=reinterpret_cast<Node*>(reinterpret_cast<BYTE*>(storage)+header);
        storage->first.capacity=8;
        storage->tail=&storage->first;
        storage->capacity=8;
        storage->bytes=bytes+64;
        storage->blocks=1;
        return storage;
    }
    void Grow()
    {
        const size_t header=Aligned(sizeof(Block));
        const size_t bytes=header+16*sizeof(Node);
        Block* block=::new (::operator new(bytes)) Block();
        block->previous=tail;
        block->nodes=reinterpret_cast<Node*>(reinterpret_cast<BYTE*>(block)+header);
        block->capacity=16;
        tail=block;
        capacity+=16;this->bytes+=bytes+64;++blocks;
    }
};
CRenderedTextSubtitle::AssTagList::~AssTagList()
{
    Storage* storage=static_cast<Storage*>(m_storage);
    if(!storage) return;
    Storage::Block* block=storage->tail;
    while(block)
    {
        Storage::Block* previous=block->previous;
        for(size_t i=block->used;i;--i) block->nodes[i-1].~AssTag();
        if(block!=&storage->first) { block->~Block(); ::operator delete(block); }
        block=previous;
    }
    storage->~Storage();
    ::operator delete(storage);
}
size_t CRenderedTextSubtitle::AssTagList::GetCount() const
{ return m_storage ? static_cast<const Storage*>(m_storage)->count : 0; }
size_t CRenderedTextSubtitle::AssTagList::GetCapacity() const
{ return m_storage ? static_cast<const Storage*>(m_storage)->capacity : 0; }
size_t CRenderedTextSubtitle::AssTagList::GetStorageBytes() const
{ return m_storage ? static_cast<const Storage*>(m_storage)->bytes : 0; }
size_t CRenderedTextSubtitle::AssTagList::GetBlockCount() const
{ return m_storage ? static_cast<const Storage*>(m_storage)->blocks : 0; }
POSITION CRenderedTextSubtitle::AssTagList::AddTail()
{
    if(!m_storage) m_storage=Storage::Create();
    Storage* storage=static_cast<Storage*>(m_storage);
    if(storage->tail->used==storage->tail->capacity) storage->Grow();
    Storage::Block& block=*storage->tail;
    Storage::Node* node=::new (static_cast<void*>(block.nodes+block.used)) Storage::Node();
    if(storage->last) storage->last->next=node; else storage->head=node;
    storage->last=node;
    ++block.used;++storage->count;
    return reinterpret_cast<POSITION>(node);
}

// Owned by one renderer and used under its existing serialization. The key is
// the exact command and raw parameter span; collisions always compare text.
class CRenderedTextSubtitle::ScalarParameterCache
{
public:
    enum { SLOTS=512, MAX_CHARACTERS=128 };
    struct Entry
    {
        AssCmdType type;
        CStringW raw, parameter;
        AssTag::Number number;
        bool invariant;
        Entry() : type(CMD_COUNT),invariant(false) { number.real=0; }
    };
    Entry slots[SLOTS];
    unsigned __int64 hits,misses;
    ScalarParameterCache() : hits(0),misses(0) {}
    static bool InvariantNumber(AssCmdType type,const CStringW& text)
    {
        bool hex=false,real=false;
        switch(type)
        {
        case CMD_c: case CMD_1c: case CMD_2c: case CMD_3c: case CMD_4c:
        case CMD_1a: case CMD_2a: case CMD_3a: case CMD_4a: case CMD_alpha: hex=true; break;
        case CMD_an: case CMD_a: case CMD_b: case CMD_fe: case CMD_i:
        case CMD_pbo: case CMD_p: case CMD_q: case CMD_s: case CMD_u: break;
        case CMD_blur: case CMD_bord: case CMD_be: case CMD_fax: case CMD_fay:
        case CMD_frx: case CMD_fry: case CMD_frz: case CMD_fr: case CMD_fscx:
        case CMD_fscy: case CMD_fsp: case CMD_fs: case CMD_kt: case CMD_kf:
        case CMD_K: case CMD_ko: case CMD_k: case CMD_shad: case CMD_xbord:
        case CMD_xshad: case CMD_ybord: case CMD_yshad: real=true; break;
        default: return false;
        }
        if(text.IsEmpty()) return true;
        const auto space=[](WCHAR c) { return c==L' ' || (c>=L'\t' && c<=L'\r'); };
        const WCHAR* p=text.GetString();while(space(*p)) ++p;
        if(*p==L'+' || *p==L'-') ++p;
        if(hex && p[0]==L'0' && (p[1]==L'x' || p[1]==L'X')) p+=2;
        const WCHAR* first=p;
        unsigned magnitude=0;
        while((*p>=L'0' && *p<=L'9') || (hex && ((*p>=L'a' && *p<=L'f') || (*p>=L'A' && *p<=L'F'))))
        {
            // Real commands accept only small integral tokens here: their exact
            // value (including signed zero) is independent of CRT locale and
            // FP rounding/precision. Fractions/exponents retain wcstod.
            if(real && (magnitude>6553 || (magnitude==6553 && *p>L'5'))) return false;
            if(real) magnitude=magnitude*10+unsigned(*p-L'0');
            ++p;
        }
        if(p==first) return false;
        while(space(*p)) ++p;
        return !*p;
    }
    Entry* Lookup(AssCmdType type,const WCHAR* raw,int length,bool& hit)
    {
        unsigned hash=2166136261u^unsigned(type);
        for(int i=0;i<length;++i) hash=(hash^unsigned(raw[i]))*16777619u;
        Entry& entry=slots[hash&(SLOTS-1)];
        hit=entry.type==type && entry.raw.GetLength()==length
            && !wmemcmp(entry.raw.GetString(),raw,length);
        if(hit) ++hits; else ++misses;
        return &entry;
    }
    void Store(Entry& entry,AssCmdType type,const WCHAR* raw,int length,const AssTag& tag)
    {
        const CStringW& parameter=tag.strParams[0];
        // Non-color parameters share the same owned CString allocation for key
        // and value. Trimmed color keys keep their exact pre-trim input too.
        entry.raw=parameter.GetLength()==length && !wmemcmp(parameter.GetString(),raw,length)
            ? parameter : CStringW(raw,length);
        entry.parameter=parameter; entry.number=tag.firstNumber; entry.invariant=InvariantNumber(type,parameter); entry.type=type;
    }
    ScalarParameterStats Stats() const
    {
        ScalarParameterStats value={hits,misses,0,sizeof(*this)+4096};
        for(const auto& entry:slots) if(entry.type!=CMD_COUNT)
        {
            ++value.entries;
            value.bytes+=(entry.raw.GetAllocLength()+entry.parameter.GetAllocLength()+2)*sizeof(WCHAR)+128;
        }
        return value;
    }
};

CRenderedTextSubtitle::ScalarParameterStats CRenderedTextSubtitle::GetScalarParameterCacheStats() const
{
    if(m_scalarParameters) return m_scalarParameters->Stats();
    ScalarParameterStats empty={}; return empty;
}
void CRenderedTextSubtitle::SetScalarParameterCacheEnabled(bool enabled)
{
    m_scalarParametersEnabled=enabled;
    if(!enabled) m_scalarParameters.reset();
}

CRenderedTextSubtitle::CRenderedTextSubtitle(CCritSec* pLock)
    : CSubPicProviderImpl(pLock)
    , m_target_scale_x(1.0), m_target_scale_y(1.0)
    , m_scalarParametersEnabled(true)
    , m_parsedTagBytes(0), m_parsedTagPeakBytes(0), m_parsedTagLimit(64 * 1024 * 1024)
    , m_parsedTagHits(0), m_parsedTagMisses(0), m_parsedTagTime(_I64_MIN)
    , m_nextParsedExpiry(_I64_MAX), m_nextSubtitleExpiry(_I64_MIN), m_maintenanceCacheEnabled(true)
    , m_orderBuilds(0), m_orderHits(0), m_parsedExpiryScans(0), m_subtitleExpiryScans(0)
    , m_lookaheadBudget(4000), m_preparingAhead(false), m_lookaheadSegment(0), m_lookaheadOffset(0)
    , m_lookaheadSourceSegments(0), m_lookaheadSourceEntries(0), m_lookaheadCursorValid(false)
    , m_layoutBytes(0), m_layoutPeakBytes(0), m_layoutLimit(32*1024*1024)
    , m_layoutHits(0), m_layoutBuilds(0), m_layoutFallbacks(0), m_layoutUpdates(0)
    , m_reconstructionCount(0), m_reconstructionPeak(0), m_reconstructionLimit(16384), m_reconstructionHits(0)
    , m_reconstructionBytes(0), m_reconstructionPeakBytes(0)
    , m_collectDiagnostics(false)
    , m_time(0), m_delay(0)
    , m_preferLibass(false), m_libassFontsLoaded(false)
{
    if( m_cmdMap.IsEmpty() )
    {
        InitCmdMap();
    }
    m_size = CSize(0, 0);
    if(g_hDC_refcnt == 0)
    {
        g_hDC = CreateCompatibleDC(NULL);
        SetBkMode(g_hDC, TRANSPARENT);
        SetTextColor(g_hDC, 0xffffff);
        SetMapMode(g_hDC, MM_TEXT);
    }
    g_hDC_refcnt++;
    m_movable = true;
    m_drawReuse=boost::make_shared<DrawReuseBudget>();
    memset(&m_lookaheadStats,0,sizeof(m_lookaheadStats));
}

CRenderedTextSubtitle::~CRenderedTextSubtitle()
{
    Deinit();
    g_hDC_refcnt--;
    if(g_hDC_refcnt == 0) DeleteDC(g_hDC);
}

void CRenderedTextSubtitle::InitCmdMap()
{
    if( m_cmdMap.IsEmpty() )
    {
        m_cmdMap.SetAt(L"1c",        CMD_1c   );
        m_cmdMap.SetAt(L"2c",        CMD_2c   );
        m_cmdMap.SetAt(L"3c",        CMD_3c   );
        m_cmdMap.SetAt(L"4c",        CMD_4c   );
        m_cmdMap.SetAt(L"1a",        CMD_1a   );
        m_cmdMap.SetAt(L"2a",        CMD_2a   );
        m_cmdMap.SetAt(L"3a",        CMD_3a   );
        m_cmdMap.SetAt(L"4a",        CMD_4a   );
        m_cmdMap.SetAt(L"alpha",     CMD_alpha);
        m_cmdMap.SetAt(L"an",        CMD_an   );
        m_cmdMap.SetAt(L"a",         CMD_a    );
        m_cmdMap.SetAt(L"blur",      CMD_blur );
        m_cmdMap.SetAt(L"bord",      CMD_bord );
        m_cmdMap.SetAt(L"be",        CMD_be   );
        m_cmdMap.SetAt(L"b",         CMD_b    );
        m_cmdMap.SetAt(L"clip",      CMD_clip );
        m_cmdMap.SetAt(L"iclip",     CMD_iclip);
        m_cmdMap.SetAt(L"c",         CMD_c    );
        m_cmdMap.SetAt(L"fade",      CMD_fade );
        m_cmdMap.SetAt(L"fad",       CMD_fad  );
        m_cmdMap.SetAt(L"fax",       CMD_fax  );
        m_cmdMap.SetAt(L"fay",       CMD_fay  );
        m_cmdMap.SetAt(L"fe",        CMD_fe   );
        m_cmdMap.SetAt(L"fn",        CMD_fn   );
        m_cmdMap.SetAt(L"frx",       CMD_frx  );
        m_cmdMap.SetAt(L"fry",       CMD_fry  );
        m_cmdMap.SetAt(L"frz",       CMD_frz  );
        m_cmdMap.SetAt(L"fr",        CMD_fr   );
        m_cmdMap.SetAt(L"fscx",      CMD_fscx );
        m_cmdMap.SetAt(L"fscy",      CMD_fscy );
        m_cmdMap.SetAt(L"fsc",       CMD_fsc  );
        m_cmdMap.SetAt(L"fsp",       CMD_fsp  );
        m_cmdMap.SetAt(L"fs",        CMD_fs   );
        m_cmdMap.SetAt(L"i",         CMD_i    );
        m_cmdMap.SetAt(L"kt",        CMD_kt   );
        m_cmdMap.SetAt(L"kf",        CMD_kf   );
        m_cmdMap.SetAt(L"K",         CMD_K    );
        m_cmdMap.SetAt(L"ko",        CMD_ko   );
        m_cmdMap.SetAt(L"k",         CMD_k    );
        m_cmdMap.SetAt(L"move",      CMD_move );
        m_cmdMap.SetAt(L"org",       CMD_org  );
        m_cmdMap.SetAt(L"pbo",       CMD_pbo  );
        m_cmdMap.SetAt(L"pos",       CMD_pos  );
        m_cmdMap.SetAt(L"p",         CMD_p    );
        m_cmdMap.SetAt(L"q",         CMD_q    );
        m_cmdMap.SetAt(L"r",         CMD_r    );
        m_cmdMap.SetAt(L"shad",      CMD_shad );
        m_cmdMap.SetAt(L"s",         CMD_s    );
        m_cmdMap.SetAt(L"t",         CMD_t    );
        m_cmdMap.SetAt(L"u",         CMD_u    );
        m_cmdMap.SetAt(L"xbord",     CMD_xbord);
        m_cmdMap.SetAt(L"xshad",     CMD_xshad);
        m_cmdMap.SetAt(L"ybord",     CMD_ybord);
        m_cmdMap.SetAt(L"yshad",     CMD_yshad);
    }
    m_cmd_pos_level.SetCount(CMD_COUNT+1);
    m_cmd_pos_level[CMD_1c   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_2c   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_3c   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_4c   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_1a   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_2a   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_3a   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_4a   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_alpha] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_an   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_a    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_blur ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_bord ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_be   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_b    ] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_clip ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_iclip] = POS_LVL_HARD;

    m_cmd_pos_level[CMD_c    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_fade ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_fad  ] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_fax  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fay  ] = POS_LVL_SOFT;

    m_cmd_pos_level[CMD_fe   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_fn   ] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_frx  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fry  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_frz  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fr   ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fscx ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fscy ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fsc  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fsp  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fs   ] = POS_LVL_SOFT;

    m_cmd_pos_level[CMD_i    ] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_kt   ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_kf   ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_K    ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_ko   ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_k    ] = POS_LVL_SOFT;

    m_cmd_pos_level[CMD_move ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_org  ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_pbo  ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_pos  ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_p    ] = POS_LVL_HARD;

    m_cmd_pos_level[CMD_q    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_r    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_shad ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_s    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_t    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_u    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_xbord] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_xshad] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_ybord] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_yshad] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_COUNT] = POS_LVL_NONE;
}

void CRenderedTextSubtitle::Copy(CSimpleTextSubtitle& sts)
{
    __super::Copy(sts);
    m_size = CSize(0, 0);
    if(CRenderedTextSubtitle* pRTS = dynamic_cast<CRenderedTextSubtitle*>(&sts))
    {
        m_size = pRTS->m_size;
    }
}

void CRenderedTextSubtitle::OnSourceLoaded(CTextFile* file, ULONGLONG start)
{
    if (m_simple || m_mode != TIME || (m_subtitleType != EXTSSA && m_subtitleType != EXTASS)) return;
    CStringA source;
    CStringW line;
    const ULONGLONG position = file->GetPosition();
    file->Seek(start, CFile::begin);
    while (file->ReadString(line)) { source += UTF16To8(line); source += "\n"; }
    file->Seek(position, CFile::begin);
    m_libass.reset(new LibassRenderer());
    m_libass->SetSource(source, false);
    m_libassOriginalStyles.clear();
    POSITION pos = m_styles.GetStartPosition();
    while (pos) {
        CString name; STSStyle* style;
        m_styles.GetNextAssoc(pos,name,style);
        m_libassOriginalStyles[name] = *style;
    }
}

void CRenderedTextSubtitle::SetLibassEmbedded(bool embedded)
{
    // ASS2 carries VSFilter-specific extended fields; keep its native path.
    if (!embedded) m_libass.reset();
    else if (m_libass) m_libass->SetEmbedded(true);
}

void CRenderedTextSubtitle::AddLibassPacket(const BYTE* data, int length, REFERENCE_TIME start, REFERENCE_TIME stop)
{
    if (m_libass) m_libass->AddPacket(data, length, start, stop);
}

void CRenderedTextSubtitle::FlushLibass()
{
    if (m_libass) m_libass->Flush();
}

void CRenderedTextSubtitle::LoadLibassGraphFonts(IFilterGraph* graph)
{
    if (!graph || !m_libass || m_libassFontsLoaded) return;
    CComPtr<IEnumFilters> filters;
    if (FAILED(graph->EnumFilters(&filters))) return;
    CComPtr<IBaseFilter> filter;
    while (filters->Next(1, &filter, NULL) == S_OK) {
        CComQIPtr<IDSMResourceBag> resources(filter);
        if (resources) for (DWORD i = 0; i < resources->ResGetCount(); ++i) {
            CComBSTR name, mime;
            BYTE* bytes = NULL;
            DWORD size = 0;
            if (SUCCEEDED(resources->ResGet(i, &name, NULL, &mime, &bytes, &size, NULL))) {
                CStringW filename(name), type(mime), ext = filename.Right(4);
                if (size && size <= INT_MAX && (!ext.CompareNoCase(L".ttf") || !ext.CompareNoCase(L".otf") ||
                    type.Find(L"font") >= 0)) {
                    try { m_libass->AddFont(UTF16To8(filename), bytes, static_cast<int>(size)); }
                    catch (...) { CoTaskMemFree(bytes); throw; }
                }
            }
            CoTaskMemFree(bytes);
        }
        filter.Release();
    }
    m_libassFontsLoaded = true;
}

void CRenderedTextSubtitle::Empty()
{
    m_libass.reset();
    m_libassOriginalStyles.clear();
    m_libassFontsLoaded = false;
    Deinit();
    __super::Empty();
}

void CRenderedTextSubtitle::OnChanged()
{
    __super::OnChanged();
    m_scalarParameters.reset();
    ClearParsedTags();
    m_subtitlePool.Clear();
    POSITION pos = m_subtitleCacheEntry.GetHeadPosition();
    while(pos)
    {
        int i = m_subtitleCacheEntry.GetNext(pos);
        delete m_subtitleCache.GetAt(i);
        m_subtitleCache.SetAt(i, NULL);
    }
    m_subtitleCacheEntry.RemoveAll();
    m_subtitleCache.ReleaseMemory(m_entries.GetCount());

    m_sla.Empty();
}


bool CRenderedTextSubtitle::Init( const CRectCoor2& video_rect, const CRectCoor2& subtitle_target_rect,
    const SIZE& layout_size )
{
    XY_LOG_INFO(_T(""));
    Deinit();

    m_video_rect = CRect(video_rect.left*MAX_SUB_PIXEL, 
                         video_rect.top*MAX_SUB_PIXEL, 
                         video_rect.right*MAX_SUB_PIXEL, 
                         video_rect.bottom*MAX_SUB_PIXEL);
    m_subtitle_target_rect = CRect(subtitle_target_rect.left*MAX_SUB_PIXEL, subtitle_target_rect.top*MAX_SUB_PIXEL, 
        subtitle_target_rect.right*MAX_SUB_PIXEL, subtitle_target_rect.bottom*MAX_SUB_PIXEL);
    m_size = CSize(layout_size.cx*MAX_SUB_PIXEL, layout_size.cy*MAX_SUB_PIXEL);

    ASSERT(layout_size.cx!=0 && layout_size.cy!=0);

    m_target_scale_x = video_rect.Width()  * 1.0 / layout_size.cx;
    m_target_scale_y = video_rect.Height() * 1.0 / layout_size.cy;

    return(true);
}

void CRenderedTextSubtitle::Deinit()
{
    if (m_libass) m_libass->Invalidate();
    ClearParsedTags();
    m_subtitlePool.Clear();
    POSITION pos = m_subtitleCacheEntry.GetHeadPosition();
    while(pos)
    {
        int i = m_subtitleCacheEntry.GetNext(pos);
        delete m_subtitleCache.GetAt(i);
    }
    m_subtitleCache.ReleaseMemory();
    m_subtitleCacheEntry.RemoveAll();
    m_sla.Empty();

    m_video_rect.SetRectEmpty();
    m_subtitle_target_rect.SetRectEmpty();
    m_size = CSize(0, 0);

    m_target_scale_x = m_target_scale_y = 1.0;

    CacheManager::GetBitmapMruCache()->RemoveAll();

    CacheManager::GetClipperAlphaMaskMruCache()->RemoveAll();
    CacheManager::GetTextInfoCache           ()->RemoveAll();
    CacheManager::GetAssTagListMruCache      ()->RemoveAll();

    CacheManager::GetScanLineDataMruCache   ()->RemoveAll();
    CacheManager::GetOverlayNoOffsetMruCache()->RemoveAll();

    CacheManager::GetSubpixelVarianceCache()->RemoveAll();
    CacheManager::GetOverlayMruCache      ()->RemoveAll();
    CacheManager::GetOverlayNoBlurMruCache()->RemoveAll();
    CacheManager::GetScanLineData2MruCache()->RemoveAll();
    CacheManager::GetPathDataMruCache     ()->RemoveAll();

    //The ids generated by XyFlyWeight is ever increasing.
    //That is good for ids generated after re-init won't conflict with ids generated before re-init.
    XyFwGroupedDrawItemsHashKey::GetCacher()->RemoveAll();
}

void CRenderedTextSubtitle::ParseEffect(CSubtitle* sub, const CStringW& str)
{
    CStringW::PCXSTR str_start = str.GetString();
    CStringW::PCXSTR str_end   = str.GetLength() + str_start;
    str_start = SkipWhiteSpaceLeft(str_start, str_end);

    if(!sub || *str_start==0)
        return;

    str_end = FastSkipWhiteSpaceRight(str_start, str_end);

    const WCHAR* s = FindChar(str_start, str_end, L';');
    if(*s==L';') {
        s++;
    }
    
    const CStringW effect(str_start, s-str_start);
    if(!effect.CompareNoCase( L"Banner;" ) )
    {
        int delay, lefttoright = 0, fadeawaywidth = 0;
        if(swscanf(s, L"%d;%d;%d", &delay, &lefttoright, &fadeawaywidth) < 1) return;
        Effect* e = sub->CreateEffect(EF_BANNER);
        if(!e) return;
        e->param[0] = (int)(max(1.0*delay/sub->m_scalex, 1));
        e->param[1] = lefttoright;
        e->param[2] = (int)(sub->m_scalex*fadeawaywidth);
        sub->m_wrapStyle = 2;

        sub->m_hard_position_level = sub->m_hard_position_level > POS_LVL_NONE ? 
                                     sub->m_hard_position_level : POS_LVL_NONE;
    }
    else if(!effect.CompareNoCase(L"Scroll up;") || !effect.CompareNoCase(L"Scroll down;"))
    {
        int top, bottom, delay, fadeawayheight = 0;
        if(swscanf(s, L"%d;%d;%d;%d", &top, &bottom, &delay, &fadeawayheight) < 3) return;
        if(top > bottom) {int tmp = top; top = bottom; bottom = tmp;}
        Effect* e = sub->CreateEffect(EF_SCROLL);
        if(!e) return;
        e->param[0] = (int)(sub->m_scaley*top*MAX_SUB_PIXEL);
        e->param[1] = (int)(sub->m_scaley*bottom*MAX_SUB_PIXEL);
        e->param[2] = (int)(max(1.0*delay/sub->m_scaley, 1));
        e->param[3] = (effect.GetLength() == 12);
        e->param[4] = (int)(sub->m_scaley*fadeawayheight);

        sub->m_hard_position_level = POS_LVL_HARD;
    }
}

void CRenderedTextSubtitle::ParseString(CSubtitle* sub, CStringW str, const FwSTSStyle& style)
{
    if(!sub) return;
    str.Replace(L"\\N", L"\n");
    str.Replace(L"\\n", (sub->m_wrapStyle < 2 || sub->m_wrapStyle == 3) ? L" " : L"\n");
    str.Replace(L"\\h", L"\x00A0");
    for(int ite = 0, j = 0, len = str.GetLength(); j <= len; j++)
    {
        WCHAR c = str[j];
        if(c != L'\n' && c != L' ' && c != 0) continue;
        if(ite < j)
        {
            sub->m_words.AddTail(boost::make_shared<CText>(style, str.Mid(ite, j-ite), m_ktype, m_kstart, m_kend,
                m_target_scale_x, m_target_scale_y));
            m_kstart = m_kend;
        }
        if(c == L'\n' || c == L' ')
        {
            const CStringW text = c == L'\n' ? CStringW() : CStringW(c);
            sub->m_words.AddTail(boost::make_shared<CText>(style, text, m_ktype, m_kstart, m_kend,
                m_target_scale_x, m_target_scale_y));
            m_kstart = m_kend;
        }
        ite = j+1;
    }
}

void CRenderedTextSubtitle::ParsePolygon(CSubtitle* sub, const CStringW& str, const FwSTSStyle& style)
{
    if (!sub || !str.GetLength() || !m_nPolygon) return;
    sub->m_words.AddTail(boost::make_shared<CPolygon>(style, str, m_ktype, m_kstart, m_kend,
        sub->m_scalex/(1<<(m_nPolygon-1)), sub->m_scaley/(1<<(m_nPolygon-1)), m_polygonBaselineOffset,
        m_target_scale_x, m_target_scale_y));
    m_kstart = m_kend;
}

CRenderedTextSubtitle::AssCmdType CRenderedTextSubtitle::LookupAssCommand(
    const WCHAR* text, int length, int& commandLength)
{
    // Build once from the existing command vocabulary. Reads allocate no strings
    // and retain the longest case-sensitive prefix, including legacy short tags.
    struct CommandTrie
    {
        struct Node
        {
            WCHAR character;
            unsigned child, sibling;
            AssCmdType type;
            Node(WCHAR c, unsigned next) : character(c), child(0), sibling(next), type(CMD_COUNT) {}
        };
        unsigned roots[128];
        std::vector<Node> nodes;
        CommandTrie(const CAtlMap<CStringW, AssCmdType, CStringElementTraits<CStringW>>& commands)
        {
            memset(roots, 0, sizeof(roots));
            nodes.reserve(1 + commands.GetCount()*MAX_CMD_LENGTH);
            nodes.push_back(Node(0, 0));
            POSITION position = commands.GetStartPosition();
            while (position)
            {
                const auto* command = commands.GetNext(position);
                unsigned parent = 0;
                for (int i=0; i<command->m_key.GetLength(); ++i)
                {
                    const WCHAR c = command->m_key[i];
                    unsigned child = nodes[parent].child;
                    while (child && nodes[child].character != c) child = nodes[child].sibling;
                    if (!child)
                    {
                        child = static_cast<unsigned>(nodes.size());
                        nodes.push_back(Node(c, nodes[parent].child));
                        nodes[parent].child = child;
                        if (!parent && c < _countof(roots)) roots[c] = child;
                    }
                    parent = child;
                }
                nodes[parent].type = command->m_value;
            }
        }
        AssCmdType Find(const WCHAR* text, int length, int& matchedLength) const
        {
            matchedLength = 0;
            if (length <= 0) return CMD_COUNT;
            unsigned node = text[0] < _countof(roots) ? roots[text[0]] : nodes[0].child;
            if (text[0] >= _countof(roots))
                while (node && nodes[node].character != text[0]) node = nodes[node].sibling;
            AssCmdType result = CMD_COUNT;
            for (int i=0; node && i<min(length, MAX_CMD_LENGTH); ++i)
            {
                if (nodes[node].type != CMD_COUNT)
                {
                    result = nodes[node].type;
                    matchedLength = i+1;
                }
                if (i+1 >= min(length, MAX_CMD_LENGTH)) break;
                node = nodes[node].child;
                while (node && nodes[node].character != text[i+1]) node = nodes[node].sibling;
            }
            return result;
        }
    };
    if (m_cmdMap.IsEmpty()) InitCmdMap();
    static const CommandTrie commands(m_cmdMap);
    return commands.Find(text, length, commandLength);
}

bool CRenderedTextSubtitle::ParseSSATag( AssTagList *assTags, const CStringW& str )
{
    if (!assTags) return(false);
    int nTags = 0, nUnrecognizedTags = 0;
    for(int i = 0, j; (j = str.Find(L'\\', i)) >= 0; i = j)
    {
        POSITION pos   = assTags->AddTail();
        AssTag& assTag = assTags->GetAt(pos);
        assTag.cmdType = CMD_COUNT;

        j++;
        CStringW::PCXSTR str_start = str.GetString() + j;
        CStringW::PCXSTR pc = str_start;
        while( iswspace(*pc) ) 
        {
            pc++;
        }
        j += pc-str_start;
        str_start = pc;
        while( *pc && *pc != L'(' && *pc != L'\\' )
        {
            pc++;
        }
        j += pc-str_start;
        if( pc-str_start>0 )
        {
            while( iswspace(*--pc) );
            pc++;
        }

        const int commandSize = static_cast<int>(pc-str_start);
        if (!commandSize) continue;

        AssTagParams& params = assTag.strParams;
        const bool parenthesized=str[j] == L'(';
        if(parenthesized)
        {
            j++;
            CStringW::PCXSTR str_start = str.GetString() + j;
            CStringW::PCXSTR pc = str_start;
            while( iswspace(*pc) ) 
            {
                pc++;
            }
            j += pc-str_start;
            str_start = pc;
            while( *pc && *pc != L')' )
            {
                pc++;
            }
            j += pc-str_start;
            if (pc-str_start>0)
            {
                while( iswspace(*--pc) );
                pc++;
            }

            CStringW::PCXSTR param_start = str_start;
            CStringW::PCXSTR param_end = pc;
            while( param_start<param_end )
            {
                param_start = SkipWhiteSpaceLeft(param_start, param_end);

                CStringW::PCXSTR newstart = FindChar(param_start, param_end, L',');
                CStringW::PCXSTR newend = FindChar(param_start, param_end, L'\\');
                if(newstart < newend)
                {
                    if(newstart > param_start)
                    {
                        newend = FastSkipWhiteSpaceRight(param_start, newstart);
                        CStringW s(param_start, newend - param_start);
                        if(!s.IsEmpty()) params.Add(s);
                    }
                    param_start = newstart + 1;
                }
                else if(param_start<param_end)
                {
                    CStringW s(param_start, param_end - param_start);

                    params.Add(s);
                    param_start = param_end;
                }
            }
        }

        int cmd_length;
        const AssCmdType cmd_type = LookupAssCommand(str_start, commandSize, cmd_length);
        ScalarParameterCache::Entry* scalarSlot=NULL;
        bool scalarHit=false;
        const WCHAR* scalarText=str_start+cmd_length;
        const int scalarLength=commandSize-cmd_length;
        const auto copyScalar=[&]() {
            if(!m_scalarParametersEnabled || parenthesized || scalarLength<0
                || scalarLength>ScalarParameterCache::MAX_CHARACTERS) return false;
            if(!m_scalarParameters) m_scalarParameters.reset(new ScalarParameterCache());
            scalarSlot=m_scalarParameters->Lookup(cmd_type,scalarText,scalarLength,scalarHit);
            if(scalarHit) { params.Add(scalarSlot->parameter); if(scalarSlot->invariant) assTag.firstNumber=scalarSlot->number; }
            return scalarHit;
        };
        switch( cmd_type )
        {
        case CMD_fax  :
        case CMD_fay  :
        case CMD_fe   :
        case CMD_fn   :
        case CMD_frx  :
        case CMD_fry  :
        case CMD_frz  :
        case CMD_fr   :
        case CMD_fscx :
        case CMD_fscy :
        case CMD_fsc  :
        case CMD_fsp  :
        case CMD_fs   :
        case CMD_i    :
        case CMD_kt   :
        case CMD_kf   :
        case CMD_K    :
        case CMD_ko   :
        case CMD_k    :
        case CMD_pbo  :
        case CMD_p    :
        case CMD_q    :
        case CMD_r    :
        case CMD_shad :
        case CMD_s    :
        case CMD_an   :
        case CMD_a    :
        case CMD_blur :
        case CMD_bord :
        case CMD_be   :
        case CMD_b    :
        case CMD_u    :
        case CMD_xbord:
        case CMD_xshad:
        case CMD_ybord:
        case CMD_yshad:
            //        default:
            if(!copyScalar()) params.Add(CStringW(str_start+cmd_length, commandSize-cmd_length));
            break;
        case CMD_c    :
        case CMD_1c   :
        case CMD_2c   :
        case CMD_3c   :
        case CMD_4c   :
        case CMD_1a   :
        case CMD_2a   :
        case CMD_3a   :
        case CMD_4a   :
        case CMD_alpha:
            if(!copyScalar()) params.Add(CStringW(str_start+cmd_length, commandSize-cmd_length).Trim(L"&H"));
            break;
        case CMD_clip :
        case CMD_iclip:
        case CMD_fade :
        case CMD_fad  :
        case CMD_move :
        case CMD_org  :
        case CMD_pos  :
            break;
        case CMD_t:
            if (!params.IsEmpty() && params.GetCount()<=4)
                ParseSSATag(&assTag.embeded, params[params.GetCount()-1]);
            break;
        case CMD_COUNT:
            nUnrecognizedTags++;
            break;
        }

        assTag.cmdType = cmd_type;
        // Fractional, large real and noncanonical forms keep the CRT call in
        // the current locale/FP environment, even when their owned text is shared.
        if(!scalarHit || !scalarSlot->invariant) CacheTagNumbers(assTag);
        if(scalarSlot && !scalarHit) m_scalarParameters->Store(*scalarSlot,cmd_type,scalarText,scalarLength,assTag);
        nTags++;
    }
    return(true);
}

void CRenderedTextSubtitle::CacheTagNumbers(AssTag& tag)
{
    // Omitted arguments used to pass an empty string to the CRT and yield zero.
    tag.firstNumber.real = 0;
    const AssTagParams& p = tag.strParams;
    if (p.IsEmpty()) return;
    // Use the same CRT conversions, bases, and argument-count guards as the
    // evaluator. In particular, three-argument \t truncates doubles, whereas
    // four-argument \t reads its times with wcstol.
    switch (tag.cmdType)
    {
    case CMD_c: case CMD_1c: case CMD_2c: case CMD_3c: case CMD_4c:
    case CMD_1a: case CMD_2a: case CMD_3a: case CMD_4a: case CMD_alpha:
        tag.GetNumber(0).integer = wcstol(p[0], NULL, 16);
        break;
    case CMD_an: case CMD_a: case CMD_b: case CMD_fe: case CMD_i:
    case CMD_pbo: case CMD_p: case CMD_q: case CMD_s: case CMD_u:
        tag.GetNumber(0).integer = wcstol(p[0], NULL, 10);
        break;
    case CMD_blur: case CMD_bord: case CMD_be: case CMD_fax: case CMD_fay:
    case CMD_frx: case CMD_fry: case CMD_frz: case CMD_fr: case CMD_fscx:
    case CMD_fscy: case CMD_fsp: case CMD_fs: case CMD_kt: case CMD_kf:
    case CMD_K: case CMD_ko: case CMD_k: case CMD_shad: case CMD_xbord:
    case CMD_xshad: case CMD_ybord: case CMD_yshad:
        tag.GetNumber(0).real = wcstod(p[0], NULL);
        break;
    case CMD_clip: case CMD_iclip:
        if (p.GetCount() == 2) tag.GetNumber(0).integer = wcstol(p[0], NULL, 10);
        else if (p.GetCount() == 4)
        {
            tag.AllocateNumbers(4);
            for (int i = 0; i < 4; ++i) tag.GetNumber(i).real = wcstod(p[i], NULL);
        }
        break;
    case CMD_fade: case CMD_fad:
        if (p.GetCount() == 7 || p.GetCount() == 2)
        {
            tag.AllocateNumbers(p.GetCount());
            for (size_t i = 0; i < p.GetCount(); ++i) tag.GetNumber(i).integer = wcstol(p[i], NULL, 10);
        }
        break;
    case CMD_move:
        if (p.GetCount() == 4 || p.GetCount() == 6)
        {
            tag.AllocateNumbers(p.GetCount());
            for (int i = 0; i < 4; ++i) tag.GetNumber(i).real = wcstod(p[i], NULL);
            if (p.GetCount() == 6)
                for (int i = 4; i < 6; ++i) tag.GetNumber(i).integer = wcstol(p[i], NULL, 10);
        }
        break;
    case CMD_org: case CMD_pos:
        if (p.GetCount() == 2)
        {
            tag.AllocateNumbers(2);
            for (int i = 0; i < 2; ++i) tag.GetNumber(i).real = wcstod(p[i], NULL);
        }
        break;
    case CMD_t:
        if (p.GetCount() == 2) tag.GetNumber(0).real = wcstod(p[0], NULL);
        else if (p.GetCount() == 3)
        {
            tag.AllocateNumbers(2);
            for (int i = 0; i < 2; ++i) tag.GetNumber(i).real = wcstod(p[i], NULL);
        }
        else if (p.GetCount() == 4)
        {
            tag.AllocateNumbers(3);
            tag.GetNumber(0).integer = wcstol(p[0], NULL, 10);
            tag.GetNumber(1).integer = wcstol(p[1], NULL, 10);
            tag.GetNumber(2).real = wcstod(p[2], NULL);
        }
        break;
    default:
        break;
    }
}

bool CRenderedTextSubtitle::ParseSSATag( CSubtitle* sub, const AssTagList& assTags, STSStyle& style, const STSStyle& org, bool fAnimate /*= false*/, bool paintOnly /*= false*/ )
{
    if(!sub) return(false);
    
    static const CStringW empty;
    POSITION pos = assTags.GetHeadPosition();
    while(pos)
    {
        const AssTag& assTag = assTags.GetNext(pos);
        AssCmdType cmd_type = assTag.cmdType;
        if (paintOnly)
        {
            switch (cmd_type)
            {
            case CMD_c: case CMD_1c: case CMD_2c: case CMD_3c: case CMD_4c:
            case CMD_alpha: case CMD_1a: case CMD_2a: case CMD_3a: case CMD_4a:
            case CMD_clip: case CMD_iclip: case CMD_t:
            case CMD_blur: case CMD_be: case CMD_shad: case CMD_xshad: case CMD_yshad:
            case CMD_fr: case CMD_frx: case CMD_fry: case CMD_frz: case CMD_fax: case CMD_fay:
            case CMD_p: case CMD_pbo:
                break;
            default:
                continue;
            }
        }
        const AssTagParams& params = assTag.strParams;

        sub->m_hard_position_level = sub->m_hard_position_level > m_cmd_pos_level[cmd_type] ?
                                     sub->m_hard_position_level : m_cmd_pos_level[cmd_type];
        // TODO: call ParseStyleModifier(cmd, params, ..) and move the rest there
        const CStringW& p = params.GetCount() > 0 ? params[0] : empty;
        switch ( cmd_type )
        {
        case CMD_1c:
        case CMD_2c:
        case CMD_3c:
        case CMD_4c:
            {
                const int i = 
                      cmd_type==CMD_1c ? 0 :
                      cmd_type==CMD_2c ? 1 :
                      cmd_type==CMD_3c ? 2 :
                    /*cmd_type==CMD_4c ?*/ 3;
                DWORD c = assTag.GetNumber(0).integer;
                style.colors[i] = !p.IsEmpty()
                    ? (((int)CalcAnimation(c&0x0000ff, style.colors[i]&0x0000ff, fAnimate))&0x0000ff
                      |((int)CalcAnimation(c&0x00ff00, style.colors[i]&0x00ff00, fAnimate))&0x00ff00
                      |((int)CalcAnimation(c&0xff0000, style.colors[i]&0xff0000, fAnimate))&0xff0000)
                    : org.colors[i];
                break;
            }
        case CMD_1a :
        case CMD_2a :
        case CMD_3a :
        case CMD_4a :
            {
                const int i = 
                      cmd_type==CMD_1a ? 0 : 
                      cmd_type==CMD_2a ? 1 :
                      cmd_type==CMD_3a ? 2 :
                    /*cmd_type==CMD_4a ?*/ 3;
                style.alpha[i] = !p.IsEmpty()
                    ? (BYTE)CalcAnimation(assTag.GetNumber(0).integer, style.alpha[i], fAnimate)
                    : org.alpha[i];
                break;
            }
        case CMD_alpha:
            {
                for(int i = 0; i < 4; i++)
                {
                    style.alpha[i] = !p.IsEmpty()
                                     ? (BYTE)CalcAnimation(assTag.GetNumber(0).integer, style.alpha[i], fAnimate)
                                     : org.alpha[i];
                }
                break;
            }
        case CMD_an:
            {
                int n = assTag.GetNumber(0).integer;
                if (sub->m_scrAlignment < 0)
                    sub->m_scrAlignment = (n > 0 && n < 10) ? n : org.scrAlignment;
                break;
            }
        case CMD_a:
            {
                int n = assTag.GetNumber(0).integer;
                if (sub->m_scrAlignment < 0)
                    sub->m_scrAlignment = (n > 0 && n < 12) ? ((((n-1)&3)+1)+((n&4)?6:0)+((n&8)?3:0)) : org.scrAlignment;
                break;
            }
        case CMD_blur:
            {
                double n = CalcAnimation(assTag.GetNumber(0).real, style.fGaussianBlur, fAnimate);
                style.fGaussianBlur = !p.IsEmpty()
                                      ? (n < 0 ? 0 : n)
                                          : org.fGaussianBlur;
                break;
            }
        case CMD_bord:
            {
                double dst = assTag.GetNumber(0).real;
                double nx = CalcAnimation(dst, style.outlineWidthX, fAnimate);
                style.outlineWidthX = !p.IsEmpty()
                                      ? (nx < 0 ? 0 : nx)
                                          : org.outlineWidthX;
                double ny = CalcAnimation(dst, style.outlineWidthY, fAnimate);
                style.outlineWidthY = !p.IsEmpty()
                                      ? (ny < 0 ? 0 : ny)
                                          : org.outlineWidthY;
                break;
            }
        case CMD_be:
            {
                double d = CalcAnimation(assTag.GetNumber(0).real, style.fBlur, fAnimate);
                style.fBlur = !p.IsEmpty()
                              ? d
                              : org.fBlur;
                break;
            }
        case CMD_b:
            {
                int n = assTag.GetNumber(0).integer;
                style.fontWeight = !p.IsEmpty()
                                   ? (n == 0 ? FW_NORMAL : n == 1 ? FW_BOLD : n >= 100 ? n : org.fontWeight)
                                       : org.fontWeight;
                break;
            }
        case CMD_clip:
        case CMD_iclip:
            {
                bool invert = (cmd_type == CMD_iclip);
                if(params.GetCount() == 1 && !sub->m_pClipper)
                {
                    sub->m_pClipper.reset( DEBUG_NEW CClipper(params[0],
                        CSize(m_video_rect.Width()>>3, m_video_rect.Height()>>3),
                        sub->m_scalex, sub->m_scaley, invert, m_target_scale_x, m_target_scale_y) );
                }
                else if(params.GetCount() == 2 && !sub->m_pClipper)
                {
                    int scale = max(assTag.GetNumber(0).integer, 1);
                    sub->m_pClipper.reset( DEBUG_NEW CClipper(params[1],
                        CSize(m_video_rect.Width()>>3, m_video_rect.Height()>>3),
                        sub->m_scalex/(1<<(scale-1)), sub->m_scaley/(1<<(scale-1)), invert, m_target_scale_x, m_target_scale_y) );
                }
                else if(params.GetCount() == 4)
                {
                    CRect r;
                    sub->m_clipInverse = invert;
                    r.SetRect(
                        assTag.GetNumber(0).real+0.5,
                        assTag.GetNumber(1).real+0.5,
                        assTag.GetNumber(2).real+0.5,
                        assTag.GetNumber(3).real+0.5);
                    sub->m_clip.SetRect(
                        (int)CalcAnimation(sub->m_scalex*r.left  , sub->m_clip.left  , fAnimate),
                        (int)CalcAnimation(sub->m_scaley*r.top   , sub->m_clip.top   , fAnimate),
                        (int)CalcAnimation(sub->m_scalex*r.right , sub->m_clip.right , fAnimate),
                        (int)CalcAnimation(sub->m_scaley*r.bottom, sub->m_clip.bottom, fAnimate));
                }
                break;
            }
        case CMD_c:
            {
                DWORD c = assTag.GetNumber(0).integer;
                style.colors[0] = !p.IsEmpty()
                                  ? (((int)CalcAnimation(c&0x0000ff, style.colors[0]&0x0000ff, fAnimate))&0x0000ff
                                    |((int)CalcAnimation(c&0x00ff00, style.colors[0]&0x00ff00, fAnimate))&0x00ff00
                                    |((int)CalcAnimation(c&0xff0000, style.colors[0]&0xff0000, fAnimate))&0xff0000)
                                  : org.colors[0];
                break;
            }
        case CMD_fade:
        case CMD_fad:
            {
                sub->m_fAnimated2 = true;
                if(params.GetCount() == 7 && !sub->m_effects[EF_FADE])// {\fade(a1=param[0], a2=param[1], a3=param[2], t1=t[0], t2=t[1], t3=t[2], t4=t[3])
                {
                    if(Effect* e = sub->CreateEffect(EF_FADE))
                    {
                        for(int i = 0; i < 3; i++)
                            e->param[i] = assTag.GetNumber(i).integer;
                        for(int i = 0; i < 4; i++)
                            e->t[i] = assTag.GetNumber(3+i).integer;
                        sub->m_effects[EF_FADE] = e;
                    }
                }
                else if(params.GetCount() == 2 && !sub->m_effects[EF_FADE]) // {\fad(t1=t[1], t2=t[2])
                {
                    if(Effect* e = sub->CreateEffect(EF_FADE))
                    {
                        e->param[0] = e->param[2] = 0xff;
                        e->param[1] = 0x00;
                        for(int i = 1; i < 3; i++)
                            e->t[i] = assTag.GetNumber(i-1).integer;
                        e->t[0] = e->t[3] = -1; // will be substituted with "start" and "end"
                        sub->m_effects[EF_FADE] = e;
                    }
                }
                break;
            }
        case CMD_fax:
            {
                style.fontShiftX = !p.IsEmpty()
                                   ? CalcAnimation(assTag.GetNumber(0).real, style.fontShiftX, fAnimate)
                                   : org.fontShiftX;
                break;
            }
        case CMD_fay:
            {
                style.fontShiftY = !p.IsEmpty()
                                   ? CalcAnimation(assTag.GetNumber(0).real, style.fontShiftY, fAnimate)
                                   : org.fontShiftY;
                break;
            }
        case CMD_fe:
            {
                int n = assTag.GetNumber(0).integer;
                if (n < 0) n = DEFAULT_CHARSET;
                style.charSet = !p.IsEmpty()
                                ? n
                                : org.charSet;
                break;
            }
        case CMD_fn:
            {
                if(!p.IsEmpty() && p != L'0')
                    style.fontName = CString(p).Trim();
                else
                    style.fontName = org.fontName;
                break;
            }
        case CMD_frx:
            {
                style.fontAngleX = !p.IsEmpty()
                                   ? CalcAnimation(assTag.GetNumber(0).real, style.fontAngleX, fAnimate)
                                   : org.fontAngleX;
                break;
            }
        case CMD_fry:
            {
                style.fontAngleY = !p.IsEmpty()
                                   ? CalcAnimation(assTag.GetNumber(0).real, style.fontAngleY, fAnimate)
                                   : org.fontAngleY;
                break;
            }
        case CMD_frz:
        case CMD_fr:
            {
                style.fontAngleZ = !p.IsEmpty()
                                   ? CalcAnimation(assTag.GetNumber(0).real, style.fontAngleZ, fAnimate)
                                   : org.fontAngleZ;
                break;
            }
        case CMD_fscx:
            {
                double n = CalcAnimation(assTag.GetNumber(0).real, style.fontScaleX, fAnimate);
                style.fontScaleX = !p.IsEmpty()
                                   ? ((n < 0) ? 0 : n)
                                       : org.fontScaleX;
                break;
            }
        case CMD_fscy:
            {
                double n = CalcAnimation(assTag.GetNumber(0).real, style.fontScaleY, fAnimate);
                style.fontScaleY = !p.IsEmpty()
                                   ? ((n < 0) ? 0 : n)
                                       : org.fontScaleY;
                break;
            }
        case CMD_fsc:
            {
                style.fontScaleX = org.fontScaleX;
                style.fontScaleY = org.fontScaleY;
                break;
            }
        case CMD_fsp:
            {
                style.fontSpacing = !p.IsEmpty()
                                    ? CalcAnimation(assTag.GetNumber(0).real, style.fontSpacing, fAnimate)
                                    : org.fontSpacing;
                break;
            }
        case CMD_fs:
            {
                if(!p.IsEmpty())
                {
                    if(p[0] == L'-' || p[0] == L'+')
                    {
                        double n = CalcAnimation(style.fontSize + style.fontSize*assTag.GetNumber(0).real/10, style.fontSize, fAnimate);
                        style.fontSize = (n > 0) ? n : org.fontSize;
                    }
                    else
                    {
                        double n = CalcAnimation(assTag.GetNumber(0).real, style.fontSize, fAnimate);
                        style.fontSize = (n > 0) ? n : org.fontSize;
                    }
                }
                else
                {
                    style.fontSize = org.fontSize;
                }
                break;
            }
        case CMD_i:
            {
                int n = assTag.GetNumber(0).integer;
                style.fItalic = !p.IsEmpty()
                                ? (n == 0 ? false : n == 1 ? true : org.fItalic)
                                    : org.fItalic;
                break;
            }
        case CMD_kt:
            {
                m_kstart = !p.IsEmpty()
                           ? assTag.GetNumber(0).real*10
                           : 0;
                m_kend = m_kstart;
                sub->m_fAnimated2 = true;//fix me: define m_fAnimated m_fAnimated2 strictly
                break;
            }
        case CMD_kf:
        case CMD_K:
            {
                m_ktype = 1;
                m_kstart = m_kend;
                m_kend += !p.IsEmpty()
                          ? assTag.GetNumber(0).real*10
                          : 1000;
                sub->m_fAnimated2 = true;//fix me: define m_fAnimated m_fAnimated2 strictly
                break;
            }
        case CMD_ko:
            {
                m_ktype = 2;
                m_kstart = m_kend;
                m_kend += !p.IsEmpty()
                          ? assTag.GetNumber(0).real*10
                          : 1000;
                sub->m_fAnimated2 = true;//fix me: define m_fAnimated m_fAnimated2 strictly
                break;
            }
        case CMD_k:
            {
                m_ktype = 0;
                m_kstart = m_kend;
                m_kend += !p.IsEmpty()
                          ? assTag.GetNumber(0).real*10
                          : 1000;
                sub->m_fAnimated2 = true;//fix me: define m_fAnimated m_fAnimated2 strictly
                break;
            }
        case CMD_move: // {\move(x1=param[0], y1=param[1], x2=param[2], y2=param[3][, t1=t[0], t2=t[1]])}
            {
                if((params.GetCount() == 4 || params.GetCount() == 6) && !sub->m_effects[EF_MOVE])
                {
                    if(Effect* e = sub->CreateEffect(EF_MOVE))
                    {
                        e->param[0] = (int)(sub->m_scalex*assTag.GetNumber(0).real*MAX_SUB_PIXEL);
                        e->param[1] = (int)(sub->m_scaley*assTag.GetNumber(1).real*MAX_SUB_PIXEL);
                        e->param[2] = (int)(sub->m_scalex*assTag.GetNumber(2).real*MAX_SUB_PIXEL);
                        e->param[3] = (int)(sub->m_scaley*assTag.GetNumber(3).real*MAX_SUB_PIXEL);
                        e->t[0] = e->t[1] = -1;
                        if(params.GetCount() == 6)
                        {
                            for(int i = 0; i < 2; i++)
                                e->t[i] = assTag.GetNumber(4+i).integer;
                        }
                        sub->m_effects[EF_MOVE] = e;
                        sub->m_fAnimated2 = true;
                    }
                }
                break;
            }
        case CMD_org: // {\org(x=param[0], y=param[1])}
            {
                if(params.GetCount() == 2 && !sub->m_effects[EF_ORG])
                {
                    if(Effect* e = sub->CreateEffect(EF_ORG))
                    {
                        e->param[0] = (int)(sub->m_scalex*assTag.GetNumber(0).real*MAX_SUB_PIXEL);
                        e->param[1] = (int)(sub->m_scaley*assTag.GetNumber(1).real*MAX_SUB_PIXEL);
                        sub->m_effects[EF_ORG] = e;
                    }
                }
                break;
            }
        case CMD_pbo:
            {
                m_polygonBaselineOffset = assTag.GetNumber(0).integer;
                break;
            }
        case CMD_pos:
            {
                if(params.GetCount() == 2 && !sub->m_effects[EF_MOVE])
                {
                    if(Effect* e = sub->CreateEffect(EF_MOVE))
                    {
                        e->param[0] = e->param[2] = (int)(sub->m_scalex*assTag.GetNumber(0).real*MAX_SUB_PIXEL);
                        e->param[1] = e->param[3] = (int)(sub->m_scaley*assTag.GetNumber(1).real*MAX_SUB_PIXEL);
                        e->t[0] = e->t[1] = 0;
                        sub->m_effects[EF_MOVE] = e;
                    }
                }
                break;
            }
        case CMD_p:
            {
                int n = assTag.GetNumber(0).integer;
                m_nPolygon = (n <= 0 ? 0 : n);
                break;
            }
        case CMD_q:
            {
                int n = assTag.GetNumber(0).integer;
                sub->m_wrapStyle = !p.IsEmpty() && (0 <= n && n <= 3)
                                   ? n
                                   : m_defaultWrapStyle;
                break;
            }
        case CMD_r:
            {
                STSStyle* val;
                style = (!p.IsEmpty() && m_styles.Lookup(p, val) && val) ? *val : org;
                break;
            }
        case CMD_shad:
            {
                double dst = assTag.GetNumber(0).real;
                double nx = CalcAnimation(dst, style.shadowDepthX, fAnimate);
                style.shadowDepthX = !p.IsEmpty()
                                     ? (nx < 0 ? 0 : nx)
                                         : org.shadowDepthX;
                double ny = CalcAnimation(dst, style.shadowDepthY, fAnimate);
                style.shadowDepthY = !p.IsEmpty()
                                     ? (ny < 0 ? 0 : ny)
                                         : org.shadowDepthY;
                break;
            }
        case CMD_s:
            {
                int n = assTag.GetNumber(0).integer;
                style.fStrikeOut = !p.IsEmpty()
                                   ? (n == 0 ? false : n == 1 ? true : org.fStrikeOut)
                                       : org.fStrikeOut;
                break;
            }
        case CMD_t: // \t([<t1>,<t2>,][<accel>,]<style modifiers>)
            {
                m_animStart = m_animEnd = 0;
                m_animAccel = 1;
                if(params.GetCount() == 2)
                {
                    m_animAccel = assTag.GetNumber(0).real;
                }
                else if(params.GetCount() == 3)
                {
                    m_animStart = (int)assTag.GetNumber(0).real;
                    m_animEnd = (int)assTag.GetNumber(1).real;
                }
                else if(params.GetCount() == 4)
                {
                    m_animStart = assTag.GetNumber(0).integer;
                    m_animEnd = assTag.GetNumber(1).integer;
                    m_animAccel = assTag.GetNumber(2).real;
                }
                ParseSSATag(sub, assTag.embeded, style, org, true, paintOnly);
                sub->m_fAnimated = true;
                sub->m_fAnimated2 = true;
                break;
            }
        case CMD_u:
            {
                int n = assTag.GetNumber(0).integer;
                style.fUnderline = !p.IsEmpty()
                                   ? (n == 0 ? false : n == 1 ? true : org.fUnderline)
                                       : org.fUnderline;
                break;
            }
        case CMD_xbord:
            {
                double dst = assTag.GetNumber(0).real;
                double nx = CalcAnimation(dst, style.outlineWidthX, fAnimate);
                style.outlineWidthX = !p.IsEmpty()
                                      ? (nx < 0 ? 0 : nx)
                                          : org.outlineWidthX;
                break;
            }
        case CMD_xshad:
            {
                double dst = assTag.GetNumber(0).real;
                double nx = CalcAnimation(dst, style.shadowDepthX, fAnimate);
                style.shadowDepthX = !p.IsEmpty()
                                     ? nx
                                     : org.shadowDepthX;
                break;
            }
        case CMD_ybord:
            {
                double dst = assTag.GetNumber(0).real;
                double ny = CalcAnimation(dst, style.outlineWidthY, fAnimate);
                style.outlineWidthY = !p.IsEmpty()
                                      ? (ny < 0 ? 0 : ny)
                                          : org.outlineWidthY;
                break;
            }
        case CMD_yshad:
            {
                double dst = assTag.GetNumber(0).real;
                double ny = CalcAnimation(dst, style.shadowDepthY, fAnimate);
                style.shadowDepthY = !p.IsEmpty()
                                     ? ny
                                     : org.shadowDepthY;
                break;
            }
        default:
            break;
        }
    }
    return(true);
}

CRenderedTextSubtitle::SharedPtrConstAssTagList CRenderedTextSubtitle::GetParsedSSATags(const CStringW& str)
{
    SharedPtrConstAssTagList assTags;
    AssTagListMruCache *ass_tag_cache = CacheManager::GetAssTagListMruCache();
    POSITION pos = ass_tag_cache->Lookup(str);
    if (pos==NULL)
    {
        boost::shared_ptr<AssTagList> tmp=boost::make_shared<AssTagList>();
        ParseSSATag(tmp.get(), str);
        assTags=tmp;
        ass_tag_cache->UpdateCache(str, assTags);
    }
    else
    {
        assTags = ass_tag_cache->GetAt(pos);
        ass_tag_cache->UpdateCache( pos );
    }
    return assTags;
}

void CRenderedTextSubtitle::ClearReconstructedSubtitles()
{
    POSITION pos = m_subtitleCacheEntry.GetHeadPosition();
    while (pos)
    {
        const POSITION previous = pos;
        const int entry = m_subtitleCacheEntry.GetNext(pos);
        CSubtitle* subtitle = m_subtitleCache.GetAt(entry);
        if (subtitle->m_fAnimated || subtitle->m_preparedContext)
        {
            delete subtitle;
            m_subtitleCache.SetAt(entry, NULL);
            m_subtitleCacheEntry.RemoveAt(previous);
        }
    }
    m_reconstructionCount = 0;
    m_reconstructionBytes = 0;
    m_nextSubtitleExpiry = _I64_MIN;
}

void CRenderedTextSubtitle::SetReconstructionCacheLimit(size_t entries)
{
    ClearReconstructedSubtitles();
    m_reconstructionLimit = min(entries, size_t(16384));
}

bool CRenderedTextSubtitle::HasLayoutAnimation(const AssTagList& tags, bool inTransform, unsigned depth)
{
    if (depth > 32) return true;
    POSITION position = tags.GetHeadPosition();
    while (position)
    {
        const AssTag& tag = tags.GetNext(position);
        if (tag.cmdType == CMD_r) return true;
        if (tag.cmdType == CMD_t)
        {
            if (HasLayoutAnimation(tag.embeded, true, depth+1)) return true;
        }
        else if (inTransform)
        {
            switch (tag.cmdType)
            {
            case CMD_c: case CMD_1c: case CMD_2c: case CMD_3c: case CMD_4c:
            case CMD_alpha: case CMD_1a: case CMD_2a: case CMD_3a: case CMD_4a:
            case CMD_clip: case CMD_iclip:
            case CMD_blur: case CMD_be: case CMD_shad: case CMD_xshad: case CMD_yshad:
            case CMD_fr: case CMD_frx: case CMD_fry: case CMD_frz: case CMD_fax: case CMD_fay:
            case CMD_p: case CMD_pbo:
                break;
            default:
                return true;
            }
        }
    }
    return false;
}

void CRenderedTextSubtitle::DropPreparedLayout(ParsedEventTags& event)
{
    if (event.layout)
    {
        ASSERT(m_layoutBytes >= event.layout->bytes);
        m_layoutBytes -= event.layout->bytes;
        event.layout.reset();
    }
}

void CRenderedTextSubtitle::SetPreparedLayoutCacheLimit(size_t bytes)
{
    ClearParsedTags();
    m_layoutLimit = min(bytes, size_t(32*1024*1024));
}

CRenderedTextSubtitle::PreparedLayoutStats CRenderedTextSubtitle::GetPreparedLayoutStats() const
{
    size_t count = 0;
    for (const auto& event : m_parsedEventTags) if (event.second->layout) ++count;
    PreparedLayoutStats result = {m_layoutHits, m_layoutBuilds, m_layoutFallbacks, m_layoutUpdates,
        m_layoutBytes, m_layoutPeakBytes, count};
    return result;
}

void CRenderedTextSubtitle::ClearParsedTags()
{
    ClearReconstructedSubtitles();
    m_parsedEventTags.clear();
    m_parsedTagBytes = 0;
    m_layoutBytes = 0;
    m_parsedTagTime = _I64_MIN;
    m_nextParsedExpiry = _I64_MAX;
    m_nextSubtitleExpiry = _I64_MIN;
    m_orderSource.RemoveAll();
    m_orderSorted.RemoveAll();
    std::vector<BYTE>().swap(m_lookaheadAttempted);
    m_lookaheadCursorValid = false;
}

void CRenderedTextSubtitle::SetParsedTagCacheLimit(size_t bytes)
{
    ClearParsedTags();
    m_parsedTagLimit = bytes;
}

CRenderedTextSubtitle::ParsedTagCacheStats CRenderedTextSubtitle::GetParsedTagCacheStats() const
{
    ParsedTagCacheStats result = {m_parsedTagHits, m_parsedTagMisses, m_parsedTagBytes,
        m_parsedTagPeakBytes, m_parsedEventTags.size()};
    return result;
}

void CRenderedTextSubtitle::ExpireParsedTags(REFERENCE_TIME rt, double fps)
{
    if (rt < m_parsedTagTime) ClearParsedTags();
    m_parsedTagTime = rt;
    // TIME end points are stable until OnChanged. Embedded appends do not alter
    // old entries; newly admitted entries lower the deadline below. FRAME mode
    // keeps the original scan so changing FPS is observed immediately.
    if (m_maintenanceCacheEnabled && m_mode == TIME && rt < m_nextParsedExpiry) return;
    ++m_parsedExpiryScans;
    m_nextParsedExpiry = _I64_MAX;
    for (ParsedEventTagMap::iterator it = m_parsedEventTags.begin(); it != m_parsedEventTags.end(); )
    {
        if (static_cast<size_t>(it->first) >= m_entries.GetCount() || TranslateEnd(it->first, fps) <= rt)
        {
            m_parsedTagBytes -= it->second->bytes;
            DropPreparedLayout(*it->second);
            m_parsedEventTags.erase(it++);
        }
        else
        {
            m_nextParsedExpiry = min(m_nextParsedExpiry, m_entries.GetAt(it->first).end);
            ++it;
        }
    }
}

void CRenderedTextSubtitle::SetMaintenanceCacheEnabled(bool enabled)
{
    m_maintenanceCacheEnabled = enabled;
    m_orderSource.RemoveAll(); m_orderSorted.RemoveAll();
    m_nextParsedExpiry = m_nextSubtitleExpiry = _I64_MIN;
}

size_t CRenderedTextSubtitle::EstimateTagMemory(const AssTagList& tags, bool root)
{
    // Charge packed capacity, spill headers, string buffers and conservative
    // allocator overhead. Shared programs remain charged to every event.
    size_t bytes=tags.GetStorageBytes()+(root ? sizeof(AssTagList)+128 : 0);
    POSITION pos = tags.GetHeadPosition();
    while (pos)
    {
        const AssTag& tag = tags.GetNext(pos);
        if (tag.extraNumbers.get())
            bytes += (min(static_cast<size_t>(7), tag.strParams.GetCount()) - 1) * sizeof(AssTag::Number) + 64;
        bytes+=tag.strParams.GetStorageBytes();
        for (size_t i = 0; i < tag.strParams.GetCount(); ++i)
            bytes += (tag.strParams[i].GetAllocLength() + 1) * sizeof(WCHAR) + 64;
        bytes += EstimateTagMemory(tag.embeded, false);
    }
    return bytes;
}

bool CRenderedTextSubtitle::ParseHtmlTag(CSubtitle* sub, CStringW str, STSStyle& style, STSStyle& org)
{
    if(str.Find(L"!--") == 0)
        return(true);
    bool fClosing = str[0] == L'/';
    str.Trim(L" /");
    int i = str.Find(L' ');
    if(i < 0) i = str.GetLength();
    CStringW tag = str.Left(i).MakeLower();
    str = str.Mid(i).Trim();
    CAtlArray<CStringW> attribs, params;
    while((i = str.Find(L'=')) > 0)
    {
        attribs.Add(str.Left(i).Trim().MakeLower());
        str = str.Mid(i+1);
        for(i = 0; _istspace(str[i]); i++);
        str = str.Mid(i);
        if(str[0] == L'\"') {str = str.Mid(1); i = str.Find(L'\"');}
        else i = str.Find(L' ');
        if(i < 0) i = str.GetLength();
        params.Add(str.Left(i).Trim().MakeLower());
        str = str.Mid(i+1);
    }
    if(tag == L"text")
        ;
    else if(tag == L"b" || tag == L"strong")
        style.fontWeight = !fClosing ? FW_BOLD : org.fontWeight;
    else if(tag == L"i" || tag == L"em")
        style.fItalic = !fClosing ? true : org.fItalic;
    else if(tag == L"u")
        style.fUnderline = !fClosing ? true : org.fUnderline;
    else if(tag == L"s" || tag == L"strike" || tag == L"del")
        style.fStrikeOut = !fClosing ? true : org.fStrikeOut;
    else if(tag == L"font")
    {
        if(!fClosing)
        {
            for(size_t i = 0; i < attribs.GetCount(); i++)
            {
                if(params[i].IsEmpty()) continue;
                int nColor = -1;
                if(attribs[i] == L"face")
                {
                    style.fontName = params[i];
                }
                else if(attribs[i] == L"size")
                {
                    if(params[i][0] == L'+')
                        style.fontSize += wcstol(params[i], NULL, 10);
                    else if(params[i][0] == L'-')
                        style.fontSize -= wcstol(params[i], NULL, 10);
                    else
                        style.fontSize = wcstol(params[i], NULL, 10);
                }
                else if(attribs[i] == L"color")
                {
                    nColor = 0;
                }
                else if(attribs[i] == L"outline-color")
                {
                    nColor = 2;
                }
                else if(attribs[i] == L"outline-level")
                {
                    style.outlineWidthX = style.outlineWidthY = wcstol(params[i], NULL, 10);
                }
                else if(attribs[i] == L"shadow-color")
                {
                    nColor = 3;
                }
                else if(attribs[i] == L"shadow-level")
                {
                    style.shadowDepthX = style.shadowDepthY = wcstol(params[i], NULL, 10);
                }
                if(nColor >= 0 && nColor < 4)
                {
                    CString key = params[i].TrimLeft(L'#');
                    DWORD val;
                    if(g_colors.Lookup(key, val))
                        style.colors[nColor] = val;
                    else if((style.colors[nColor] = _tcstol(key, NULL, 16)) == 0)
                        style.colors[nColor] = 0x00ffffff;  // default is white
                    style.colors[nColor] = ((style.colors[nColor]>>16)&0xff)|((style.colors[nColor]&0xff)<<16)|(style.colors[nColor]&0x00ff00);
                }
            }
        }
        else
        {
            style.fontName = org.fontName;
            style.fontSize = org.fontSize;
            memcpy(style.colors, org.colors, sizeof(style.colors));
        }
    }
    else if(tag == L"k" && attribs.GetCount() == 1 && attribs[0] == L"t")
    {
        m_ktype = 1;
        m_kstart = m_kend;
        m_kend += wcstol(params[0], NULL, 10);
    }
    else
        return(false);
    return(true);
}

double CRenderedTextSubtitle::CalcAnimation(double dst, double src, bool fAnimate)
{
    int s = m_animStart ? m_animStart : 0;
    int e = m_animEnd ? m_animEnd : m_delay;
    if(fabs(dst-src) >= 0.0001 && fAnimate)
    {
        if(m_time < s)
        {
            m_reconstructionUntil = min(m_reconstructionUntil, s);
            dst = src;
        }
        else if(s <= m_time && m_time < e)
        {
            m_reconstructionStable = false;
            double t = pow(1.0 * (m_time - s) / (e - s), m_animAccel);
            dst = (1 - t) * src + t * dst;
        }
        else m_reconstructionFrom = max(m_reconstructionFrom, max(s, e));
    }
    return(dst);
}

bool CRenderedTextSubtitle::MatchesPreparedContext(int entry, CSubtitle* sub, const CStringW& sourceText)
{
    const PreparedEventContext& context = *sub->m_preparedContext;
    const STSEntry& data = m_entries.GetAt(entry);
    if (m_delay != sub->m_reuseDelay || context.sourceStyle != *GetStyle(entry)
        || context.effect != data.effect || context.margins != data.marginRect
        || context.screen != m_dstScreenSize || context.par != m_dPARCompensation
        || context.parType != m_ePARCompensationType || context.wrap != m_defaultWrapStyle
        || context.scaledBorder != m_fScaledBAS
        || (sub->m_sourceText.GetString() != sourceText.GetString() && sub->m_sourceText != sourceText))
        return false;
    return true;
}

bool CRenderedTextSubtitle::UpdatePreparedSubtitle(int entry, CSubtitle* sub, const CStringW& sourceText)
{
    if (!MatchesPreparedContext(entry,sub,sourceText)) return false;
    const PreparedEventContext& context = *sub->m_preparedContext;
    if (m_time >= sub->m_reuseFrom && m_time < sub->m_reuseUntil)
    {
        ++m_reconstructionHits;
        return true;
    }
    auto prepared = m_parsedEventTags.find(entry);
    if (prepared == m_parsedEventTags.end() || !prepared->second->layout) return false;
    const PreparedSubtitleLayout& layout = *prepared->second->layout;
    STSStyle appearance(context.sourceStyle);
    sub->m_clip.SetRect(0,0,m_size.cx>>3,m_size.cy>>3);
    sub->m_clipInverse = false;
    sub->m_pClipper.reset();
    m_animStart = m_animEnd = 0; m_animAccel = 1;
    m_nPolygon = m_polygonBaselineOffset = 0;
    m_reconstructionFrom = INT_MIN; m_reconstructionUntil = INT_MAX; m_reconstructionStable = true;
    for (const auto& block : prepared->second->blocks)
    {
        ParseSSATag(sub, *block.tags, appearance, context.sourceStyle, false, true);
        ++m_parsedTagHits;
    }
    if (m_nPolygon != layout.polygon || m_polygonBaselineOffset != layout.baseline) return false;
    STSStyle current(layout.style.get());
    memcpy(current.colors, appearance.colors, sizeof(current.colors));
    memcpy(current.alpha, appearance.alpha, sizeof(current.alpha));
    current.fBlur = appearance.fBlur; current.fGaussianBlur = appearance.fGaussianBlur;
    current.shadowDepthX = appearance.shadowDepthX;
    current.shadowDepthY = appearance.shadowDepthY;
    current.shadowDepthX *= (m_fScaledBAS ? sub->m_scalex : 1) * MAX_SUB_PIXEL;
    current.shadowDepthY *= (m_fScaledBAS ? sub->m_scaley : 1) * MAX_SUB_PIXEL;
    current.fontAngleX = appearance.fontAngleX; current.fontAngleY = appearance.fontAngleY;
    current.fontAngleZ = appearance.fontAngleZ;
    current.fontShiftX = appearance.fontShiftX; current.fontShiftY = appearance.fontShiftY;
    sub->UpdateLayoutAppearance(layout, FwSTSStyle(current));
    sub->CreateClippers(m_size, m_video_rect.Size());
    // Hand small constant states back to the existing interval cache instead
    // of replaying their program after an animation has stopped changing.
    const CWord* word = sub->m_words.GetCount() == 1 ? sub->m_words[0].get() : NULL;
    const CPolygon* polygon = dynamic_cast<const CPolygon*>(word);
    const CText* text = dynamic_cast<const CText*>(word);
    if (m_reconstructionStable && ((polygon && polygon->HasSmallRetainedPath() && sourceText.GetLength() <= 512)
        || (text && text->HasSmallRetainedText() && sourceText.GetLength() <= 2048))
        && !sub->m_pClipper && !sub->m_effects[EF_ORG] && !sub->m_effects[EF_BANNER] && !sub->m_effects[EF_SCROLL])
    {
        sub->m_reuseFrom = m_reconstructionFrom; sub->m_reuseUntil = m_reconstructionUntil;
        // Retain the exact context guard even while reusing a constant interval.
    }
    ++m_layoutHits; ++m_layoutUpdates;
    return true;
}

CSubtitle* CRenderedTextSubtitle::GetSubtitle(int entry)
{
    CSubtitle* sub = m_subtitleCache.GetAt(entry);
    if (sub && !sub->m_fAnimated && !sub->m_preparedContext) return sub;
    const STSEntry& entryData = m_entries.GetAt(entry);
    // Each branch calls separately: a CString-valued conditional expression
    // would copy the Unicode string even when bound to a const reference.
    if (entryData.fUnicode) return GetSubtitle(entry, sub, entryData.str);
    return GetSubtitle(entry, sub, GetStrW(entry, true));
}

CSubtitle* CRenderedTextSubtitle::GetSubtitle(int entry, CSubtitle* sub, const CStringW& sourceText)
{
    // Keep cache hits out of the large construction frame. The source is either
    // an entry-owned Unicode string or an ANSI conversion alive for this call.
    if (sub)
    {
        if (sub->m_preparedContext)
        {
            if (!sub->m_fAnimated)
            {
                if (MatchesPreparedContext(entry,sub,sourceText))
                {
                    // Activation returns this to the ordinary static cache.
                    // Collision placement still happens below in ParseScript.
                    --m_reconstructionCount; m_reconstructionBytes-=sub->m_reuseCharge;
                    sub->m_reuseCharge=0; sub->m_preparedContext.reset();
                    sub->m_sourceText.Empty();
                    return sub;
                }
            }
            else if (UpdatePreparedSubtitle(entry, sub, sourceText)) return sub;
            return CreateSubtitle(entry, sub, sourceText);
        }
        if (m_time >= sub->m_reuseFrom && m_time < sub->m_reuseUntil && m_delay == sub->m_reuseDelay
                && (sub->m_sourceText.GetString() == sourceText.GetString() || sub->m_sourceText == sourceText))
        {
            ++m_reconstructionHits;
            return sub;
        }
    }
    return CreateSubtitle(entry, sub, sourceText);
}

static CRect ResolveSubtitleMargins(const STSEntry& entry, const STSStyle& style, double sx, double sy)
{
    CRect margin = entry.marginRect;
    if (!margin.left) margin.left = style.marginRect.get().left;
    if (!margin.top) margin.top = style.marginRect.get().top;
    if (!margin.right) margin.right = style.marginRect.get().right;
    if (!margin.bottom) margin.bottom = style.marginRect.get().bottom;
    margin.left = (int)(sx*margin.left*MAX_SUB_PIXEL);
    margin.top = (int)(sy*margin.top*MAX_SUB_PIXEL);
    margin.right = (int)(sx*margin.right*MAX_SUB_PIXEL);
    margin.bottom = (int)(sy*margin.bottom*MAX_SUB_PIXEL);
    return margin;
}

CSubtitle* CRenderedTextSubtitle::CreateSubtitle(int entry, CSubtitle* sub, const CStringW& sourceText)
{
    if (sub)
    {
        m_subtitleCacheEntry.RemoveAt(sub->m_cachePosition);
        m_subtitleCache.SetAt(entry, NULL);
        --m_reconstructionCount;
        m_reconstructionBytes -= sub->m_reuseCharge;
        m_subtitlePool.Recycle(sub);
    }
    sub = m_subtitlePool.Acquire();
    if(!sub) return(NULL);
    m_reconstructionFrom = INT_MIN;
    m_reconstructionUntil = INT_MAX;
    m_reconstructionStable = true;
    ParsedEventTagMap::iterator cached = m_parsedEventTags.find(entry);
    if (cached != m_parsedEventTags.end() &&
        cached->second->text.GetString() != sourceText.GetString() && cached->second->text != sourceText)
    {
        m_parsedTagBytes -= cached->second->bytes;
        DropPreparedLayout(*cached->second);
        m_parsedEventTags.erase(cached);
        cached = m_parsedEventTags.end();
    }
    SharedParsedEventTags parsed = cached != m_parsedEventTags.end() ? cached->second : SharedParsedEventTags();
    const bool cacheHit = !!parsed;
    bool retain = !cacheHit && m_parsedTagBytes < m_parsedTagLimit;
    size_t blockIndex = 0;
    STSStyle stss(*GetStyle(entry));
    if (stss.fontScaleX == stss.fontScaleY && m_dPARCompensation != 1.0)
    {
        switch(m_ePARCompensationType)
        {
        case EPCTUpscale:
            if (m_dPARCompensation < 1.0)
                stss.fontScaleY /= m_dPARCompensation;
            else
                stss.fontScaleX *= m_dPARCompensation;
            break;
        case EPCTDownscale:
            if (m_dPARCompensation < 1.0)
                stss.fontScaleX *= m_dPARCompensation;
            else
                stss.fontScaleY /= m_dPARCompensation;
            break;
        case EPCTAccurateSize:
            stss.fontScaleX *= m_dPARCompensation;
            break;
        }
    }
    STSStyle orgstss(stss);
    sub->m_clip.SetRect(0, 0, m_size.cx>>3, m_size.cy>>3);
    sub->m_scrAlignment = -stss.scrAlignment;
    sub->m_wrapStyle    = m_defaultWrapStyle;
    sub->m_fAnimated    = false;
    sub->m_relativeTo   = stss.relativeTo;
    sub->m_scalex       = m_dstScreenSize.cx > 0 ? 1.0 * m_size.cx / (m_dstScreenSize.cx*MAX_SUB_PIXEL) : 1.0;
    sub->m_scaley       = m_dstScreenSize.cy > 0 ? 1.0 * m_size.cy / (m_dstScreenSize.cy*MAX_SUB_PIXEL) : 1.0;

    sub->m_target_scale_x = m_target_scale_x;
    sub->m_target_scale_y = m_target_scale_y;

    m_animStart             =
    m_animEnd               = 0;
    m_animAccel             = 1;
    m_ktype                 = 
    m_kstart                = 
    m_kend                  = 0;
    m_nPolygon              = 0;
    m_polygonBaselineOffset = 0;
    ParseEffect(sub, m_entries.GetAt(entry).effect);
    const int textLength = sourceText.GetLength();
    int offset = 0, textSpans = 0;
    bool hadHtml = false, layoutCandidate = false, layoutReused = false, marginsResolved = false;
    CRect marginRect;
    while(offset < textLength)
    {
        bool fParsed = false;
        bool recordText = false;
        const ParsedTagBlock* cachedBlock = cacheHit && blockIndex < parsed->blocks.size() &&
            parsed->blocks[blockIndex].offset == offset ? &parsed->blocks[blockIndex] : NULL;
        int i;
        if(sourceText[offset] == L'{' &&
            (i = cachedBlock ? cachedBlock->tagEnd-1 : sourceText.Find(L'}', offset)) > offset)
        {
            SharedPtrConstAssTagList tags;
            if (cachedBlock)
            {
                tags = cachedBlock->tags;
                ++m_parsedTagHits;
            }
            else
            {
                tags = GetParsedSSATags(sourceText.Mid(offset+1, i-offset-1));
                ++m_parsedTagMisses;
                if (retain)
                {
                    if (!parsed)
                    {
                        parsed.reset(DEBUG_NEW ParsedEventTags());
                        parsed->text = sourceText;
                        parsed->bytes = sizeof(ParsedEventTags) + 128 + (sourceText.GetLength() + 1) * sizeof(WCHAR);
                    }
                    const ParsedTagBlock block = {offset, i+1, i+1, CStringW(), tags};
                    parsed->blocks.push_back(block);
                    parsed->bytes += EstimateTagMemory(*tags) + 2 * sizeof(ParsedTagBlock);
                    if (parsed->bytes > m_parsedTagLimit - m_parsedTagBytes)
                    {
                        retain = false;
                        parsed.reset();
                    }
                    else recordText = true;
                }
            }
            ++blockIndex;
            fParsed = ParseSSATag(sub, *tags, stss, orgstss);
            if(fParsed)
                offset = i+1;
        }
        else if(sourceText[offset] == L'<' && (i = sourceText.Find(L'>', offset)) > offset)
        {
            hadHtml = true;
            fParsed = ParseHtmlTag(sub, sourceText.Mid(offset+1, i-offset-1), stss, orgstss);
            if(fParsed)
                offset = i+1;
        }
        if (offset == textLength) break;
        int end;
        CStringW text;
        if (cachedBlock && fParsed)
        {
            end = cachedBlock->textEnd;
            text = cachedBlock->text;
        }
        else
        {
            const WCHAR* next = wcspbrk(sourceText.GetString() + offset + (fParsed ? 0 : 1), L"{<");
            end = next ? static_cast<int>(next - sourceText.GetString()) : textLength;
            if (end != offset) text = sourceText.Mid(offset, end-offset);
            if (recordText)
            {
                ParsedTagBlock& block = parsed->blocks.back();
                block.textEnd = end;
                block.text = text;
                if (!text.IsEmpty()) parsed->bytes += (text.GetLength()+1)*sizeof(WCHAR) + 64;
                if (parsed->bytes > m_parsedTagLimit - m_parsedTagBytes)
                {
                    retain = false;
                    parsed.reset();
                }
            }
        }
        if (end == offset) continue;
        STSStyle tmp       = stss;
        tmp.fontSpacing   *=                 sub->m_scalex * 64;
        tmp.fontSize      *=                 sub->m_scaley * 64;
        tmp.outlineWidthX *= (m_fScaledBAS ? sub->m_scalex : 1) * MAX_SUB_PIXEL;
        tmp.outlineWidthY *= (m_fScaledBAS ? sub->m_scaley : 1) * MAX_SUB_PIXEL;
        tmp.shadowDepthX  *= (m_fScaledBAS ? sub->m_scalex : 1) * MAX_SUB_PIXEL;
        tmp.shadowDepthY  *= (m_fScaledBAS ? sub->m_scaley : 1) * MAX_SUB_PIXEL;
        FwSTSStyle fw_tmp(tmp);
        ++textSpans;
        // Uniform text/drawing spans preserve compaction boundaries under
        // color/alpha changes. Mixed override/HTML runs use the legacy path.
        layoutCandidate = textSpans == 1 && end == textLength && !hadHtml;
        if (layoutCandidate && parsed && parsed->layout && m_layoutLimit)
        {
            marginRect = ResolveSubtitleMargins(m_entries.GetAt(entry), orgstss, sub->m_scalex, sub->m_scaley);
            marginsResolved = true;
            layoutReused = sub->ReuseLayout(*parsed->layout, fw_tmp, m_size, marginRect,
                m_nPolygon, m_polygonBaselineOffset, m_ktype, m_kstart, m_kend);
            if (layoutReused)
            {
                ++m_layoutHits;
                m_kstart = m_kend;
            }
            else
            {
                DropPreparedLayout(*parsed);
                parsed->layoutDisabled = true;
                ++m_layoutFallbacks;
            }
        }
        if (!layoutReused)
        {
            if(m_nPolygon) ParsePolygon(sub, text, fw_tmp);
            else ParseString(sub, text, fw_tmp);
        }
        offset = end;
    }
    if( sub->m_effects[EF_BANNER] || sub->m_effects[EF_SCROLL] )
        sub->m_fAnimated2 = true;
    // just a "work-around" solution... in most cases nobody will want to use \org together with moving but without rotating the subs
    if(sub->m_effects[EF_ORG] && (sub->m_effects[EF_MOVE] || sub->m_effects[EF_BANNER] || sub->m_effects[EF_SCROLL]))
        sub->m_fAnimated = true;
    sub->m_scrAlignment = abs(sub->m_scrAlignment);
    if (!marginsResolved)
        marginRect = ResolveSubtitleMargins(m_entries.GetAt(entry), orgstss, sub->m_scalex, sub->m_scaley);
    sub->CreateClippers(m_size, m_video_rect.Size());
    if (!layoutReused) sub->MakeLines(m_size, marginRect);
    if (sub->m_fAnimated && retain && parsed && !parsed->blocks.empty())
    {
        m_parsedEventTags.insert(std::make_pair(entry, parsed));
        m_nextParsedExpiry = min(m_nextParsedExpiry, m_entries.GetAt(entry).end);
        m_parsedTagBytes += parsed->bytes;
        m_parsedTagPeakBytes = max(m_parsedTagPeakBytes, m_parsedTagBytes);
    }
    // Bound both ownership shape and retained source/word data. Charge extra
    // for text (including a possible opaque box) under a shared 64 MiB budget.
    const CWord* word = sub->m_words.GetCount() == 1 ? sub->m_words[0].get() : NULL;
    const CPolygon* polygon = dynamic_cast<const CPolygon*>(word);
    const bool smallPolygon = polygon && polygon->HasSmallRetainedPath() && sourceText.GetLength() <= 512;
    const CText* textWord = dynamic_cast<const CText*>(word);
    const bool smallText = textWord && textWord->HasSmallRetainedText()
        && sourceText.GetLength() <= 2048;
    size_t charge = smallPolygon ? 4096 : 8192;
    const bool reuse = sub->m_fAnimated && !m_preparingAhead && m_reconstructionStable && m_mode == TIME
        && m_reconstructionCount < m_reconstructionLimit && (smallPolygon || smallText)
        && m_reconstructionBytes + charge <= 64*1024*1024 && word->m_style.get().fontName.GetLength() <= 128
        && !sub->m_pClipper && !sub->m_effects[EF_ORG] && !sub->m_effects[EF_BANNER] && !sub->m_effects[EF_SCROLL];
    if (sub->m_fAnimated && !reuse && !layoutReused && parsed && (cacheHit || retain)
        && m_layoutLimit && !parsed->layoutDisabled)
    {
        if (layoutCandidate)
            for (const auto& block : parsed->blocks)
                if (HasLayoutAnimation(*block.tags)) { layoutCandidate = false; break; }
        if (layoutCandidate)
            parsed->layout = sub->PrepareLayout(m_layoutLimit-m_layoutBytes, m_size, marginRect,
                m_nPolygon, m_polygonBaselineOffset);
        if (parsed->layout)
        {
            m_layoutBytes += parsed->layout->bytes;
            m_layoutPeakBytes = max(m_layoutPeakBytes, m_layoutBytes);
            ++m_layoutBuilds;
        }
        else
        {
            parsed->layoutDisabled = true;
            ++m_layoutFallbacks;
        }
    }
    const size_t preparedMinimum = smallPolygon ? 4096 : 8192;
    const size_t staticCharge = m_preparingAhead && !sub->m_fAnimated && m_mode==TIME && m_layoutLimit
        && m_reconstructionCount<m_reconstructionLimit
        ? sub->PrepareStaticDrawing(64*1024*1024-m_reconstructionBytes,sourceText.GetLength()) : 0;
    const bool stagedStatic = staticCharge != 0;
    const bool preparedReuse = sub->m_fAnimated && !reuse && parsed && parsed->layout && m_mode == TIME
        && m_reconstructionCount < m_reconstructionLimit
        && m_reconstructionBytes + max(preparedMinimum, parsed->layout->bytes) + sizeof(PreparedEventContext) <= 64*1024*1024;
    if (stagedStatic) charge = staticCharge;
    if (preparedReuse) charge = max(preparedMinimum, parsed->layout->bytes) + sizeof(PreparedEventContext);
    if (!sub->m_fAnimated || reuse || preparedReuse || stagedStatic)
    {
        if (!m_subtitleCache.SetAt(entry, sub))
        {
            XY_LOG_FATAL("Out of Memory!");
            delete sub;
            return NULL;
        }
        sub->m_cachePosition = m_subtitleCacheEntry.AddTail(entry);
        m_nextSubtitleExpiry = min(m_nextSubtitleExpiry, m_entries.GetAt(entry).end);
        if (reuse || preparedReuse || stagedStatic)
        {
            if (preparedReuse || stagedStatic)
            {
                boost::shared_ptr<PreparedEventContext> context = boost::make_shared<PreparedEventContext>(*GetStyle(entry));
                context->effect = m_entries.GetAt(entry).effect; context->margins = m_entries.GetAt(entry).marginRect;
                context->screen = m_dstScreenSize; context->par = m_dPARCompensation;
                context->parType = m_ePARCompensationType; context->wrap = m_defaultWrapStyle;
                context->scaledBorder = m_fScaledBAS;
                sub->m_preparedContext = context;
                // A failed context check must reconstruct, never use an old interval.
                m_reconstructionFrom = 1; m_reconstructionUntil = 0;
            }
            sub->m_sourceText = sourceText;
            sub->m_reuseFrom = m_reconstructionFrom;
            sub->m_reuseUntil = m_reconstructionUntil;
            sub->m_reuseDelay = m_delay;
            sub->m_reuseCharge = charge;
            m_reconstructionBytes += charge;
            m_reconstructionPeakBytes = max(m_reconstructionPeakBytes, m_reconstructionBytes);
            ++m_reconstructionCount;
            m_reconstructionPeak = max(m_reconstructionPeak, m_reconstructionCount);
        }
    }
    return(sub);
}

void CRenderedTextSubtitle::ClearUnCachedSubtitle( CSubtitle2List& sub2List )
{
    POSITION pos = sub2List.GetHeadPosition();
    while(pos)
    {
        CSubtitle2 & sub2 = sub2List.GetNext(pos);
        if (sub2.s->m_fAnimated && !sub2.s->m_cachePosition)
        {
            m_subtitlePool.Recycle(sub2.s);
        }
    }
}

void CRenderedTextSubtitle::ShrinkCache()
{
    OverlayNoBlurMruCache *cache1 = CacheManager::GetOverlayNoBlurMruCache();
    OverlayMruCache       *cache2 = CacheManager::GetOverlayMruCache();
    BitmapMruCache        *cache3 = CacheManager::GetBitmapMruCache();
    while (g_xy_malloc_used_size > s_max_cache_size && 
        (cache1->GetCurItemNum()>0 || cache2->GetCurItemNum()>0 || cache3->GetCurItemNum()>0))
    {
        cache1->RemoveTail();
        cache2->RemoveTail();
        cache3->RemoveTail();
    }
}

//

STDMETHODIMP CRenderedTextSubtitle::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);
    *ppv = NULL;
    return
        QI(IPersist)
        QI(ISubStream)
        QI(ISubPicProviderEx2)
        QI(ISubPicProvider)
        QI(ISubPicProviderEx)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPicProvider

STDMETHODIMP_(POSITION) CRenderedTextSubtitle::GetStartPosition(REFERENCE_TIME rt, double fps)
{
    m_fps = fps;

    int iSegment;
    const STSSegment* stss = SearchSubs(rt, fps, &iSegment, NULL);
    if(stss==NULL) {
        TRACE_PARSER("No subtitle at "<<XY_LOG_VAR_2_STR(RT2MS(rt)));
        return NULL;
    }
    return (POSITION)(iSegment + 1);
}

STDMETHODIMP_(POSITION) CRenderedTextSubtitle::GetNext(POSITION pos)
{
    int iSegment = (int)pos;
    const STSSegment *stss = GetSegment(iSegment);
    while(stss && stss->subs.GetCount() == 0) {
        iSegment++;
        stss = GetSegment(iSegment);
    }
    return(stss ? (POSITION)(iSegment+1) : NULL);
}

STDMETHODIMP_(REFERENCE_TIME) CRenderedTextSubtitle::GetStart(POSITION pos, double fps)
{
    return TranslateSegmentStart((int)pos - 1, fps);
}

STDMETHODIMP_(REFERENCE_TIME) CRenderedTextSubtitle::GetStop(POSITION pos, double fps)
{
    return TranslateSegmentEnd((int)pos - 1, fps);
}

//@start, @stop: -1 if segment not found; @stop may < @start if subIndex exceed uppper bound
STDMETHODIMP_(VOID) CRenderedTextSubtitle::GetStartStop(POSITION pos, double fps, /*out*/REFERENCE_TIME &start, /*out*/REFERENCE_TIME &stop)
{
    int iSegment = (int)pos-1;
    REFERENCE_TIME tempStart, tempEnd;
    TranslateSegmentStartEnd(iSegment, fps, tempStart, tempEnd);
    start = tempStart;
    stop = tempEnd;
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsAnimated(POSITION pos)
{
    if (IsUsingLibass()) return true; // Re-evaluate libass animation at every requested frame.
    unsigned int iSegment = (int)pos-1;
    if(iSegment<m_segments.GetCount())
        return m_segments[iSegment].animated;
    else
        return false;
    //return(true);
}

int CRenderedTextSubtitle::CompareOrderedEvents(const void* a, const void* b)
{
    const OrderedEvent& lhs = *static_cast<const OrderedEvent*>(a);
    const OrderedEvent& rhs = *static_cast<const OrderedEvent*>(b);
    // Preserve qsort and its input sequence for equal layer/read-order pairs.
    if (lhs.layer != rhs.layer) return lhs.layer < rhs.layer ? -1 : 1;
    return lhs.readorder == rhs.readorder ? 0 : lhs.readorder < rhs.readorder ? -1 : 1;
}


void CRenderedTextSubtitle::PrepareUpcoming(REFERENCE_TIME rt, double fps, LONGLONG renderStart)
{
    // Offline and embedded ASS use the same segment traversal. No event is
    // rendered early, and no raster/bitmap is cached at a different time.
    if (!m_lookaheadBudget || m_mode != TIME || !(fps>0 && fps<1000) ||
        m_entries.GetCount()>1024*1024 || m_parsedTagBytes>=m_parsedTagLimit) return;
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&now);
    const LONGLONG deadline=min(now.QuadPart+frequency.QuadPart*m_lookaheadBudget/1000000,
        renderStart+LONGLONG(frequency.QuadPart*(0.75/fps)));
    if (now.QuadPart>=deadline) return;
    const LONGLONG began=now.QuadPart;
    if (!m_lookaheadCursorValid || m_lookaheadSourceSegments!=m_segments.GetCount() ||
        m_lookaheadSourceEntries!=m_entries.GetCount())
    {
        // Locate the next boundary without scanning the source or sorting an
        // untimed index. A streaming insertion can split the current segment.
        size_t lo=0,hi=m_segments.GetCount();
        while(lo<hi) { const size_t mid=lo+(hi-lo)/2; if(m_segments[mid].start<=rt) lo=mid+1; else hi=mid; }
        m_lookaheadSegment=lo; m_lookaheadOffset=0;
        m_lookaheadAttempted.resize(m_entries.GetCount(),0);
        m_lookaheadSourceSegments=m_segments.GetCount(); m_lookaheadSourceEntries=m_entries.GetCount();
        m_lookaheadCursorValid=true;
    }
    const int savedTime=m_time,savedDelay=m_delay;
    while(m_lookaheadSegment<m_segments.GetCount())
    {
        const STSSegment& next=m_segments[m_lookaheadSegment];
        if(next.start>rt+UNITS) break; // General one-second horizon.
        if(next.start<=rt || m_lookaheadOffset>=next.subs.GetCount())
        { ++m_lookaheadSegment; m_lookaheadOffset=0; continue; }
        QueryPerformanceCounter(&now); if(now.QuadPart>=deadline) break;
        const int entry=next.subs[m_lookaheadOffset++];
        const STSEntry& source=m_entries[entry];
        if(source.start<=rt || source.end<=rt || m_lookaheadAttempted[entry]) continue;
        m_lookaheadAttempted[entry]=1;
        // Reverse seeks can leave static states cached for future events.
        // Preparation must not discard/rebuild any existing cached state.
        if (m_subtitleCache.GetAt(entry)) continue;
        // One event is the non-preemptible unit. Avoid large scripts/strings in
        // this path; arbitrary complex events always retain on-demand behavior.
        if(source.str.GetLength()>8192 || !source.fUnicode) { ++m_lookaheadStats.skipped; continue; }
        ++m_lookaheadStats.attempted;
        const LONGLONG unitStart=now.QuadPart;
        m_time=0; m_delay=int(RT2MS(source.end-source.start));
        struct PreparingScope
        {
            bool& flag;
            PreparingScope(bool& value) : flag(value) { flag=true; }
            ~PreparingScope() { flag=false; }
        };
        CSubtitle* subtitle;
        { PreparingScope preparing(m_preparingAhead); subtitle=GetSubtitle(entry); }
        if(subtitle)
        {
            // Only the prepared replay has a complete source/style/context
            // guard. Other states are discarded; immutable syntax/path/layout
            // work can still be reused at the actual activation timestamp.
            if(subtitle->m_preparedContext) ++m_lookaheadStats.retained;
            else
            {
                if(m_subtitleCache.GetAt(entry)==subtitle)
                {
                    m_subtitleCacheEntry.RemoveAt(subtitle->m_cachePosition);
                    m_subtitleCache.SetAt(entry,NULL);
                    if(subtitle->m_fAnimated || subtitle->m_preparedContext) { --m_reconstructionCount; m_reconstructionBytes-=subtitle->m_reuseCharge; }
                }
                m_subtitlePool.Recycle(subtitle);
            }
        }
        QueryPerformanceCounter(&now);
        m_lookaheadStats.largestUnit=max(m_lookaheadStats.largestUnit,now.QuadPart-unitStart);
    }
    m_time=savedTime; m_delay=savedDelay;
    QueryPerformanceCounter(&now); m_lookaheadStats.ticks+=now.QuadPart-began;
}

HRESULT CRenderedTextSubtitle::ParseScript(REFERENCE_TIME rt, double fps, CSubtitle2List *outputSub2List )
{
    ExpireParsedTags(rt, fps);
    TRACE_RENDERER_REQUEST("Begin search subtitle segment");
    //fix me: check input and log error
    int segment;
    //const
    STSSegment* stss = SearchSubs2(rt, fps, &segment);
    // A deadline avoids revisiting every retained subtitle on unchanged frames.
    if (!m_maintenanceCacheEnabled || m_mode != TIME || rt >= m_nextSubtitleExpiry)
    {
        ++m_subtitleExpiryScans;
        m_nextSubtitleExpiry = _I64_MAX;
        TRACE_RENDERER_REQUEST("Begin clear parsed subtitle cache. m_subtitleCache.size:"<<m_subtitleCacheEntry.GetCount());
        POSITION pos = m_subtitleCacheEntry.GetHeadPosition();
        while(pos)
        {
            POSITION pos_old = pos;
            int key = m_subtitleCacheEntry.GetNext(pos);
            STSEntry& stse = m_entries.GetAt(key);
            if(stse.end <= rt)
            {
                CSubtitle* expired = m_subtitleCache.GetAt(key);
                if (expired->m_fAnimated || expired->m_preparedContext)
                {
                    --m_reconstructionCount;
                    m_reconstructionBytes -= expired->m_reuseCharge;
                }
                delete expired;
                m_subtitleCache.SetAt(key, NULL);
                m_subtitleCacheEntry.RemoveAt(pos_old);
            }
            else m_nextSubtitleExpiry = min(m_nextSubtitleExpiry, stse.end);
        }
    }
    if(!stss) return S_FALSE;
    m_sla.AdvanceToSegment(segment, stss->subs);
    TRACE_RENDERER_REQUEST("Begin copy LSub. subs.size:"<<stss->subs.GetCount());
    CAtlArray<OrderedEvent> scratch;
    const size_t count = stss->subs.GetCount();
    // Two trivial arrays retain less than 1 MiB, including ATL growth slack. Large sets use
    // call-local storage, released after this request.
    const bool retainOrder = m_maintenanceCacheEnabled && count <= 32768;
    bool same = retainOrder && m_orderSource.GetCount() == count;
    for (size_t i=0; same && i<count; ++i)
    {
        const OrderedEvent& old = m_orderSource[i];
        const STSEntry& current = m_entries.GetAt(stss->subs[i]);
        same = old.idx == stss->subs[i] && old.layer == current.layer && old.readorder == current.readorder;
    }
    if (!same)
    {
        CAtlArray<OrderedEvent>& source = retainOrder ? m_orderSource : scratch;
        source.SetCount(count);
        for (size_t i=0; i<count; ++i)
        {
            OrderedEvent& item = source[i];
            const STSEntry& current = m_entries.GetAt(stss->subs[i]);
            item.idx = stss->subs[i]; item.layer = current.layer; item.readorder = current.readorder;
        }
        if (retainOrder) m_orderSorted.Copy(source);
        CAtlArray<OrderedEvent>& sorted = retainOrder ? m_orderSorted : scratch;
        if (count > 1) qsort(sorted.GetData(), count, sizeof(OrderedEvent), CompareOrderedEvents);
        ++m_orderBuilds;
    }
    else ++m_orderHits;
    const CAtlArray<OrderedEvent>& subs = retainOrder ? m_orderSorted : scratch;
    TRACE_RENDERER_REQUEST("Begin parse subs.");
    for(int i = 0, j = subs.GetCount(); i < j; i++)
    {
        int entry = subs[i].idx;
        const STSEntry& stse = m_entries.GetAt(entry);
        {
            REFERENCE_TIME start = TranslateStart(entry, fps);
            m_time = (int)RT2MS(rt - start);
            m_delay = (int)RT2MS(TranslateEnd(entry, fps) - start);
        }
        CSubtitle* s = GetSubtitle(entry);
        if(!s) continue;
        stss->animated |= s->m_fAnimated2;
        CRect clipRect  = s->m_clip & CRect(0,0, (m_size.cx>>3), (m_size.cy>>3));
        CRect r         = s->m_rect;
        CSize spaceNeeded = r.Size();
        // apply the effects
        bool fPosOverride = false, fOrgOverride = false;
        int alpha = 0x00;
        CPoint org2;
        for(int k = 0; k < EF_NUMBEROFEFFECTS; k++)
        {
            if(!s->m_effects[k]) continue;
            switch(k)
            {
            case EF_MOVE: // {\move(x1=param[0], y1=param[1], x2=param[2], y2=param[3], t1=t[0], t2=t[1])}
                {
                    CPoint p;
                    CPoint p1(s->m_effects[k]->param[0], s->m_effects[k]->param[1]);
                    CPoint p2(s->m_effects[k]->param[2], s->m_effects[k]->param[3]);
                    int t1 = s->m_effects[k]->t[0];
                    int t2 = s->m_effects[k]->t[1];
                    if(t2 < t1)            {int t = t1; t1 = t2; t2 = t;      }
                    if(t1 <= 0 && t2 <= 0) {            t1 = 0;  t2 = m_delay;}
                    if(m_time <= t1)  p = p1;
                    else if(p1 == p2) p = p1;
                    else if(t1 < m_time && m_time < t2)
                    {
                        double t = 1.0*(m_time-t1)/(t2-t1);
                        p.x = (int)((1-t)*p1.x + t*p2.x);
                        p.y = (int)((1-t)*p1.y + t*p2.y);
                    }
                    else p = p2;
                    int x = (s->m_scrAlignment%3) == 1 ? p.x : 
                            (s->m_scrAlignment%3) == 0 ? p.x - spaceNeeded.cx :
                                                         p.x - (spaceNeeded.cx+1)/2;
                    int y =  s->m_scrAlignment <= 3    ? p.y - spaceNeeded.cy : 
                             s->m_scrAlignment <= 6    ? p.y - (spaceNeeded.cy+1)/2 :
                                                         p.y;
                    r = CRect(CPoint(x,y), spaceNeeded);
                    fPosOverride = true;
                }
                break;
            case EF_ORG: // {\org(x=param[0], y=param[1])}
                {
                    org2 = CPoint(s->m_effects[k]->param[0], s->m_effects[k]->param[1]);
                    fOrgOverride = true;
                }
                break;
            case EF_FADE: // {\fade(a1=param[0], a2=param[1], a3=param[2], t1=t[0], t2=t[1], t3=t[2], t4=t[3]) or {\fad(t1=t[1], t2=t[2])
                {
                    int t1 = s->m_effects[k]->t[0];
                    int t2 = s->m_effects[k]->t[1];
                    int t3 = s->m_effects[k]->t[2];
                    int t4 = s->m_effects[k]->t[3];
                    if(t1 == -1 && t4 == -1) {t1 = 0; t3 = m_delay-t3; t4 = m_delay;}
                    if(m_time < t1) alpha = s->m_effects[k]->param[0];
                    else if(m_time >= t1 && m_time < t2)
                    {
                        double t = 1.0 * (m_time - t1) / (t2 - t1);
                        alpha = (int)(s->m_effects[k]->param[0]*(1-t) + s->m_effects[k]->param[1]*t);
                    }
                    else if(m_time >= t2 && m_time < t3) alpha = s->m_effects[k]->param[1];
                    else if(m_time >= t3 && m_time < t4)
                    {
                        double t = 1.0 * (m_time - t3) / (t4 - t3);
                        alpha = (int)(s->m_effects[k]->param[1]*(1-t) + s->m_effects[k]->param[2]*t);
                    }
                    else if(m_time >= t4) alpha = s->m_effects[k]->param[2];
                }
                break;
            case EF_BANNER: // Banner;delay=param[0][;leftoright=param[1];fadeawaywidth=param[2]]
                {
                    int left = 0,
                        right = m_size.cx;
                    r.left = !!s->m_effects[k]->param[1] 
                        ? (left  /*marginRect.left*/ - spaceNeeded.cx) + (int)(m_time*MAX_SUB_PIXEL_F/s->m_effects[k]->param[0])
                        : (right /*marginRect.right*/)                 - (int)(m_time*MAX_SUB_PIXEL_F/s->m_effects[k]->param[0]);
                    r.right = r.left + spaceNeeded.cx;
                    clipRect &= CRect(left>>3, clipRect.top, right>>3, clipRect.bottom);
                    fPosOverride = true;
                }
                break;
            case EF_SCROLL: // Scroll up/down(toptobottom=param[3]);top=param[0];bottom=param[1];delay=param[2][;fadeawayheight=param[4]]
                {
                    r.top = !!s->m_effects[k]->param[3]
                        ? s->m_effects[k]->param[0] + (int)(m_time*MAX_SUB_PIXEL_F/s->m_effects[k]->param[2]) - spaceNeeded.cy
                        : s->m_effects[k]->param[1] - (int)(m_time*MAX_SUB_PIXEL_F/s->m_effects[k]->param[2]);
                    r.bottom = r.top + spaceNeeded.cy;
                    CRect cr(0, (s->m_effects[k]->param[0] + 4) >> 3, (m_size.cx>>3), (s->m_effects[k]->param[1] + 4) >> 3);
                    clipRect &= cr;
                    fPosOverride = true;
                }
                break;
            default:
                break;
            }
        }
        if(!fPosOverride && !fOrgOverride && !s->m_fAnimated)
            r = m_sla.AllocRect(s, segment, entry, stse.layer, m_collisions);
        CPoint org;
        org.x = (s->m_scrAlignment%3) == 1 ? r.left   : 
                (s->m_scrAlignment%3) == 2 ? r.CenterPoint().x 
                                           : r.right;
        org.y =  s->m_scrAlignment <= 3    ? r.bottom : 
                 s->m_scrAlignment <= 6    ? r.CenterPoint().y 
                                           : r.top;
        if(!fOrgOverride) org2 = org;
        CPoint p2(0, r.top);
        // Rectangles for inverse clip

        CRectCoor2 clipRect_coor2;
        clipRect_coor2.left   = clipRect.left   * m_target_scale_x;
        clipRect_coor2.right  = clipRect.right  * m_target_scale_x;
        clipRect_coor2.top    = clipRect.top    * m_target_scale_y;
        clipRect_coor2.bottom = clipRect.bottom * m_target_scale_y;
        CSubtitle2& sub2 = outputSub2List->GetAt(outputSub2List->AddTail( CSubtitle2(s, clipRect_coor2, org, org2, p2, alpha, m_time) ));
    }

    return (subs.GetCount()) ? S_OK : S_FALSE;
}

STDMETHODIMP CRenderedTextSubtitle::RenderEx(SubPicDesc& spd, REFERENCE_TIME rt, double fps, CAtlList<CRectCoor2>& rectList)
{
    CSize output_size = CSize(spd.w,spd.h);
    CRectCoor2 video_rect = CRect(0,0,spd.w,spd.h);
    rectList.RemoveAll();
    if (spd.vidrect.left!=0 || spd.vidrect.top!=0 || spd.vidrect.right!=spd.w || spd.vidrect.bottom!=spd.h)
    {
        XY_LOG_WARN("Video rectangle is different from window. But support for relative to video rectangle has been removed.");
    }

    CComPtr<IXySubRenderFrame> sub_render_frame;
    HRESULT hr = RenderEx(&sub_render_frame, spd.type, video_rect, video_rect, output_size, rt, fps);
    if (SUCCEEDED(hr) && sub_render_frame)
    {
        int count = 0;
        hr = sub_render_frame->GetBitmapCount(&count);
        if(FAILED(hr))
        {
            return hr;
        }
        int color_space;
        hr = sub_render_frame->GetXyColorSpace(&color_space);
        if(FAILED(hr))
        {
            return hr;
        }
        for (int i=0;i<count;i++)
        {
            POINT pos;
            SIZE size;
            LPCVOID pixels;
            int pitch;
            hr = sub_render_frame->GetBitmap(i, NULL, &pos, &size, &pixels, &pitch );
            if(FAILED(hr))
            {
                return hr;
            }
            rectList.AddTail(CRect(pos, size));
            if (color_space==XY_CS_AYUV_PLANAR)
            {
                XyPlannerFormatExtra plans;
                hr = sub_render_frame->GetBitmapExtra(i, &plans);
                if(FAILED(hr))
                {
                    return hr;
                }
                XyBitmap::AlphaBltPlannar(spd, pos, size, plans, pitch);
            }
            else
            {
                XyBitmap::AlphaBltPack(spd, pos, size, pixels, pitch);
            }
        }
    }
    return (!rectList.IsEmpty()) ? S_OK : S_FALSE;
}

HRESULT CRenderedTextSubtitle::BenchmarkNativePhase(IXySubRenderFrame** output,
    REFERENCE_TIME time, bool parseOnly, LONGLONG& ticks, ULONGLONG& cpuTicks)
{
    if (!output || IsUsingLibass()) return E_INVALIDARG;
    *output = NULL;
    const CRect video(0,0,1920,1080);
    const SIZE storage = m_layout_size.cx > 0 && m_layout_size.cy > 0 ? SIZE(m_layout_size) : SIZE{1920,1080};
    if (m_size.cx == 0 && !Init(video,video,storage)) return E_FAIL;
    XySubRenderFrameCreater* creator = XySubRenderFrameCreater::GetDefaultCreater();
    creator->SetOutputRect(video); creator->SetClipRect(video); creator->SetColorSpace(XY_CS_ARGB);
    const auto cpu = []() -> ULONGLONG {
        FILETIME created, ended, kernel, user;
        if (!GetThreadTimes(GetCurrentThread(),&created,&ended,&kernel,&user)) return 0;
        ULARGE_INTEGER k,u;
        k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
        return k.QuadPart+u.QuadPart;
    };
    HRESULT result = S_OK;
    LARGE_INTEGER start, stop;
    ULONGLONG cpuStart;
    if (parseOnly) {
        cpuStart=cpu(); QueryPerformanceCounter(&start);
        {
            RenderPhaseTimer timer(m_collectDiagnostics ? &m_diagnostics : NULL, RenderDiagnostics::PARSE);
            CSubtitle2List subtitles;
            result=ParseScript(time,24000.0/1001,&subtitles);
            ClearUnCachedSubtitle(subtitles);
        }
        QueryPerformanceCounter(&stop); cpuTicks=cpu()-cpuStart;
    } else {
        CompositeDrawItemListList draws;
        {
            CSubtitle2List subtitles;
            result=ParseScript(time,24000.0/1001,&subtitles);
            if (FAILED(result)) return result;
            if (result==S_FALSE) { ticks=0; cpuTicks=0; return result; }
            const CRect margins(0,0,0,0);
            DoRender(m_video_rect.Size(),m_video_rect.TopLeft(),margins,subtitles,&draws);
            ClearUnCachedSubtitle(subtitles);
        }
        XySubRenderFrame* frame = NULL;
        cpuStart=cpu(); QueryPerformanceCounter(&start);
        CompositeDrawItem::Draw(&frame,draws,m_collectDiagnostics ? &m_diagnostics : NULL);
        QueryPerformanceCounter(&stop); cpuTicks=cpu()-cpuStart;
        if (frame) { frame->AddRef(); *output=frame; }
        ShrinkCache();
    }
    ticks=stop.QuadPart-start.QuadPart;
    if (m_collectDiagnostics) ++m_diagnostics.frames;
    return result;
}

STDMETHODIMP CRenderedTextSubtitle::RenderEx( IXySubRenderFrame**subRenderFrame, int spd_type,
    const RECT& video_rect, const RECT& subtitle_target_rect,
    const SIZE& original_video_size,
    REFERENCE_TIME rt, double fps )
{
    TRACE_RENDERER_REQUEST("Begin RenderEx rt"<<rt);
    if (!subRenderFrame)
    {
        return S_FALSE;
    }
    *subRenderFrame = NULL;
    CRect cvideo_rect = video_rect;

    cvideo_rect &= subtitle_target_rect;
    if (cvideo_rect!=video_rect)
    {
        XY_LOG_WARN("NOT supported yet!");
        return E_NOTIMPL;
    }

    if (cvideo_rect.left!=0 || cvideo_rect.top!=0)
    {
        XY_LOG_WARN("FIXME: supported with hack");
        cvideo_rect.MoveToXY(0,0);
    }

    SIZE layout_size = original_video_size;
    if (m_layout_size.cx > 0 && m_layout_size.cy > 0) {
        layout_size.cx = m_layout_size.cx;
        layout_size.cy = m_layout_size.cy;
    }

    XyColorSpace color_space = XY_CS_ARGB;
    switch(spd_type)
    {
    case MSP_AYUV_PLANAR:
        color_space = XY_CS_AYUV_PLANAR;
        break;
    case MSP_XY_AUYV:
        color_space = XY_CS_AUYV;
        break;
    case MSP_AYUV:
        color_space = XY_CS_AYUV;
        break;
    case MSP_RGBA_F:
        color_space = XY_CS_ARGB_F;
        break;
    default:
        color_space = XY_CS_ARGB;
        break;
    }

    XySubRenderFrameCreater *render_frame_creater = XySubRenderFrameCreater::GetDefaultCreater();
    render_frame_creater->SetColorSpace(color_space);

    if( m_video_rect != CRect(cvideo_rect.left*MAX_SUB_PIXEL,
                              cvideo_rect.top*MAX_SUB_PIXEL,
                              cvideo_rect.right*MAX_SUB_PIXEL,
                              cvideo_rect.bottom*MAX_SUB_PIXEL)
        || m_subtitle_target_rect != CRect(subtitle_target_rect.left*MAX_SUB_PIXEL,
                                           subtitle_target_rect.top*MAX_SUB_PIXEL,
                                           subtitle_target_rect.right*MAX_SUB_PIXEL,
                                           subtitle_target_rect.bottom*MAX_SUB_PIXEL)
        || m_size != CSize(layout_size.cx*MAX_SUB_PIXEL, layout_size.cy*MAX_SUB_PIXEL) )
    {
        if (!Init(cvideo_rect, subtitle_target_rect, layout_size))
        {
            XY_LOG_FATAL("Failed to Init.");
            return E_FAIL;
        }

        render_frame_creater->SetOutputRect(cvideo_rect);
        render_frame_creater->SetClipRect(subtitle_target_rect);
    }

    if (IsUsingLibass()) {
        try {
            m_libass->Configure(m_defaultStyle, m_fForcedDefaultStyle, m_styles, m_libassOriginalStyles);
            return m_libass->Render(subRenderFrame, video_rect, original_video_size, rt, render_frame_creater);
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (const std::exception& error) { XY_LOG_ERROR("libass: " << error.what()); return E_FAIL; }
    }

    LARGE_INTEGER preparationStart;
    QueryPerformanceCounter(&preparationStart);
    TRACE_RENDERER_REQUEST("Begin ParseScript");
    RenderDiagnostics* diagnostics = m_collectDiagnostics ? &m_diagnostics : NULL;
    RenderPhaseTimer renderTimer(diagnostics, RenderDiagnostics::RENDER);
    if (diagnostics) ++diagnostics->frames;
    CSubtitle2List sub2List;
    HRESULT hr;
    {
        RenderPhaseTimer timer(diagnostics, RenderDiagnostics::PARSE);
        hr = ParseScript(rt, fps, &sub2List);
    }
    if(hr!=S_OK)
    {
        if (hr==S_FALSE) { PrepareUpcoming(rt,fps,preparationStart.QuadPart); ShrinkCache(); }
        return hr;
    }

    if (!m_simple)
    {
        bool newMovable = true;
        POSITION pos=sub2List.GetHeadPosition();
        while ( pos!=NULL )
        {
            const CSubtitle2& sub2 = sub2List.GetNext(pos);
            if (sub2.s->m_hard_position_level > POS_LVL_NONE)
            {
              newMovable = false;
              break;
            }
        }
        m_movable = newMovable;
    }

    TRACE_RENDERER_REQUEST("Begin build draw item tree");
    CRectCoor2 margin_rect(
        m_video_rect.left - m_subtitle_target_rect.left,
        m_video_rect.top  - m_subtitle_target_rect.top,
        m_subtitle_target_rect.right - m_video_rect.right,
        m_subtitle_target_rect.bottom - m_video_rect.bottom);
    CompositeDrawItemListList compDrawItemListList;
    {
        RenderPhaseTimer timer(diagnostics, RenderDiagnostics::BUILD_DRAW);
        DoRender(m_video_rect.Size(), m_video_rect.TopLeft(), margin_rect, sub2List, &compDrawItemListList);
        ClearUnCachedSubtitle(sub2List);
    }

    TRACE_RENDERER_REQUEST("Begin Draw");
    XySubRenderFrame *sub_render_frame;
    CompositeDrawItem::Draw(&sub_render_frame, compDrawItemListList, diagnostics);
    sub_render_frame->MoveTo(video_rect.left, video_rect.top);
    (*subRenderFrame = sub_render_frame)->AddRef();

    PrepareUpcoming(rt,fps,preparationStart.QuadPart);
    ShrinkCache();

    TRACE_RENDERER_REQUEST("Finished");
    return hr;
}

void CRenderedTextSubtitle::DoRender( const SIZECoor2& output_size, 
    const POINTCoor2& video_org,
    const RECTCoor2& margin_rect, 
    const CSubtitle2List& sub2List, 
    CompositeDrawItemListList *compDrawItemListList /*output*/)
{
    //check input and log error
    POSITION pos=sub2List.GetHeadPosition();
    while ( pos!=NULL )
    {
        const CSubtitle2& sub2 = sub2List.GetNext(pos);
        CompositeDrawItemList& compDrawItemList = compDrawItemListList->GetAt(compDrawItemListList->AddTail());
        RenderOneSubtitle(output_size, video_org, margin_rect, sub2, &compDrawItemList);
    }
}

void CRenderedTextSubtitle::RenderOneSubtitle( const SIZECoor2& output_size, 
    const POINTCoor2& video_org, const RECTCoor2& margin_rect, 
    const CSubtitle2& sub2, 
    CompositeDrawItemList* compDrawItemList /*output*/)
{
    CSubtitle   * s        = sub2.s;
    CRect         clipRect = sub2.clipRect;
    const CPoint& org      = sub2.org;
    const CPoint& org2     = sub2.org2;
    const CPoint& p2       = sub2.p;
    int           alpha    = sub2.alpha;
    int           time     = sub2.time;
    if(!s) return;

    CPointCoor2 margin;
    if (s->m_hard_position_level > POS_LVL_NONE)
    {
        margin = video_org;
    }
    else
    {
        //set margin to move subtitles to black bars
        switch(s->m_scrAlignment%3)
        {
        case 1: margin.x = video_org.x - margin_rect.left;  break; //move to left
        case 2: margin.x = video_org.x;                     break; //do not move so that it aligns with the middle of the video
        case 3: margin.x = video_org.x + margin_rect.right; break; //move to right
        }
        switch((s->m_scrAlignment-1)/3)
        {
        case 0: margin.y = video_org.y - margin_rect.top;    break; //move to top
        case 1: margin.y = video_org.y;                      break; //do not move so that it aligns with the middle of the video
        case 2: margin.y = video_org.y + margin_rect.bottom; break;//move to bottom
        }
        ASSERT(clipRect.Width() * MAX_SUB_PIXEL == output_size.cx &&
                ( (clipRect.Height() * MAX_SUB_PIXEL == output_size.cy) ||
                  ((clipRect.Height() + 1) & ~1) * MAX_SUB_PIXEL == ((output_size.cy + 1) & ~1)
                ));
        // Do not assert when height is e.g. 413*8 instead of 414*8. YV12 SubSampling? FIXME to check
        clipRect.SetRect(0,0, 
            (output_size.cx + video_org.x+margin_rect.left+margin_rect.right)>>3,
            (output_size.cy + video_org.y+margin_rect.top +margin_rect.bottom)>>3);
    }

    // The no-clipper machine has no mutable paint state and is shared by all
    // unclipped words. Vector clippers retain their individual machines.
    static const SharedPtrCClipperPaintMachine unclipped =
        boost::make_shared<CClipperPaintMachine>(SharedPtrCClipper());
    SharedPtrCClipperPaintMachine clipper = s->m_pClipper
        ? boost::make_shared<CClipperPaintMachine>(s->m_pClipper) : unclipped;

    CRect iclipRect[4];
    iclipRect[0] = CRect(0             , 0              , output_size.cx>>3, clipRect.top     );
    iclipRect[1] = CRect(0             , clipRect.top   , clipRect.left    , clipRect.bottom  );
    iclipRect[2] = CRect(clipRect.right, clipRect.top   , output_size.cx>>3, clipRect.bottom  );
    iclipRect[3] = CRect(0             , clipRect.bottom, output_size.cx>>3, output_size.cy>>3);
    const Effect* move=s->m_effects[EF_MOVE];
    const bool stable=s->m_cachePosition && !s->m_pClipper && !s->m_clipInverse
        && !s->m_effects[EF_FADE] && !s->m_effects[EF_BANNER] && !s->m_effects[EF_SCROLL]
        && (!move || (move->param[0]==move->param[2] && move->param[1]==move->param[3]));
    const boost::shared_ptr<DrawReuseBudget> budget=stable ? m_drawReuse : boost::shared_ptr<DrawReuseBudget>();
    CRect bbox2(0,0,0,0);
    POSITION pos = s->GetHeadLinePosition();
    CPoint p = p2;
    while(pos)
    {
        CLine* l = s->GetNextLine(pos);
        p.x = (s->m_scrAlignment%3) == 1 ? org.x :
              (s->m_scrAlignment%3) == 0 ? org.x -  l->m_width :
                                           org.x - (l->m_width/2);

        // Paint directly into the final list. The former temporary lists
        // copied every shared draw item and allocated a second set of nodes.
        const int regions = s->m_clipInverse ? 4 : 1;
        for (int region = 0; region < regions; ++region)
        {
            POSITION first = NULL;
            for (int i = 0; i < l->GetWordCount(); ++i)
            {
                POSITION added = compDrawItemList->AddTail();
                if (!first) first = added;
            }
            const CRect& regionClip = s->m_clipInverse ? iclipRect[region] : clipRect;
            bbox2 |= l->PaintAll(compDrawItemList, regionClip, margin, clipper, p, org2, time, alpha, first,budget);
        }
        p.y += l->m_ascent + l->m_descent;
    }
}

STDMETHODIMP CRenderedTextSubtitle::Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECTCoor2& bbox)
{
    CAtlList<CRect> rectList;
    HRESULT result = RenderEx(spd, rt, fps, rectList);
    POSITION pos = rectList.GetHeadPosition();
    CRect bbox2(0,0,0,0);
    while(pos!=NULL)
    {
        bbox2 |= rectList.GetNext(pos);
    }
    bbox = bbox2;
    return result;
}

// IPersist

STDMETHODIMP CRenderedTextSubtitle::GetClassID(CLSID* pClassID)
{
    return pClassID ? *pClassID = __uuidof(this), S_OK : E_POINTER;
}

// ISubStream

STDMETHODIMP_(int) CRenderedTextSubtitle::GetStreamCount()
{
    return(1);
}

STDMETHODIMP CRenderedTextSubtitle::GetStreamInfo(int iStream, WCHAR** ppName, LCID* pLCID)
{
    if(iStream != 0) return E_INVALIDARG;
    if(ppName)
    {
        *ppName = (WCHAR*)CoTaskMemAlloc((m_name.GetLength()+1)*sizeof(WCHAR));
        if(!(*ppName))
            return E_OUTOFMEMORY;
        wcscpy(*ppName, CStringW(m_name));
    }
    if(pLCID)
    {
        *pLCID = 0; // TODO
    }
    return S_OK;
}

STDMETHODIMP_(int) CRenderedTextSubtitle::GetStream()
{
    return(0);
}

STDMETHODIMP CRenderedTextSubtitle::SetStream(int iStream)
{
    return iStream == 0 ? S_OK : E_FAIL;
}

STDMETHODIMP CRenderedTextSubtitle::Reload()
{
    CFileStatus s;
    if(!CFile::GetStatus(m_path, s)) return E_FAIL;
    return !m_path.IsEmpty() && Open(m_path, DEFAULT_CHARSET) ? S_OK : E_FAIL;
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsColorTypeSupported( int type )
{
    return type==MSP_AYUV_PLANAR ||
           type==MSP_AYUV ||
           type==MSP_XY_AUYV ||
           type==MSP_RGBA;
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsMovable()
{
    return m_movable;
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsSimple()
{
    return m_simple;
}

STDMETHODIMP CRenderedTextSubtitle::Lock()
{
    return CSubPicProviderImpl::Lock();
}

STDMETHODIMP CRenderedTextSubtitle::Unlock()
{
    return CSubPicProviderImpl::Unlock();
}
