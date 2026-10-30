#pragma once

#include <atlsync.h>
#include "SubRenderIntf.h"

// Queue state only. The owner dispatches on its worker, with no filter/provider
// locks held. A Clear callback may synchronously request frames or invalidate again.
class ConsumerInvalidation
{
    CCritSec m_lock;
    CEvent m_event;
    ULONGLONG m_generation;
    bool m_pending;
public:
    ConsumerInvalidation() : m_generation(0), m_pending(false) {
        m_event.Create(NULL, FALSE, FALSE, NULL);
    }
    HANDLE Event() const { return m_event; }
    ULONGLONG Generation() {
        CAutoLock lock(&m_lock);
        return m_generation;
    }
    void Schedule() {
        CAutoLock lock(&m_lock);
        ++m_generation;
        m_pending = true;
        m_event.Set();
    }
    // A frame rendered before an invalidation may finish delivery after Clear.
    // Schedule another clear in that case; never mutate the delivered frame.
    void Delivered(ULONGLONG generation) {
        CAutoLock lock(&m_lock);
        if (generation != m_generation) {
            m_pending = true;
            m_event.Set();
        }
    }
    HRESULT Dispatch(ISubRenderConsumer* consumer) {
        {
            CAutoLock lock(&m_lock);
            if (!m_pending) return S_FALSE;
            m_pending = false;
        }
        CComQIPtr<ISubRenderConsumer2> extended(consumer);
        // Full clear also handles subtitle delay/speed and timeline resets.
        return extended ? extended->Clear(0) : S_FALSE;
    }
};
