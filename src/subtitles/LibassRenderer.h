#pragma once

#include "STS.h"
#include <memory>
#include <vector>
#include <map>

struct ass_library;
struct ass_renderer;
struct ass_track;
struct IXySubRenderFrame;
class XySubRenderFrameCreater;

// One instance per subtitle stream. All calls use the stream's existing lock.
class LibassRenderer
{
    struct Packet { CStringA data; REFERENCE_TIME start, stop; };
    CStringA m_source;
    std::vector<Packet> m_packets;
    ass_library* m_library;
    ass_renderer* m_renderer;
    ass_track* m_track;
    bool m_embedded;
    CStringA m_overrides;
    void EnsureLibrary();
    void EnsureTrack();
public:
    LibassRenderer();
    ~LibassRenderer();
    void SetSource(const CStringA& source, bool embedded);
    void SetEmbedded(bool embedded);
    void AddPacket(const BYTE* data, int length, REFERENCE_TIME start, REFERENCE_TIME stop);
    void Flush();
    void Invalidate();
    void AddFont(const char* name, const void* data, int length);
    void LoadFonts(const wchar_t* folder);
    bool HasSource() const { return !m_source.IsEmpty(); }
    typedef std::map<CStringW, STSStyle> StyleBaseline;
    void Configure(const STSStyle& style, bool forceDefault,
        const CSTSStyleMap& styles, const StyleBaseline& originals);
    HRESULT Render(IXySubRenderFrame** output, const RECT& video, const SIZE& storage,
        REFERENCE_TIME time, XySubRenderFrameCreater* creator);
private:
    LibassRenderer(const LibassRenderer&);
    LibassRenderer& operator=(const LibassRenderer&);
};
