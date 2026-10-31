#pragma once

#include <atlsync.h>
#include <limits>
#include "SubRenderIntf.h"

// Queue bookkeeping only. External callbacks must run without this lock or the
// filter/provider locks: Clear may synchronously request replacement frames.
class ConsumerInvalidation
{
public:
    struct Delivery {
        Delivery* next;
        ULONGLONG epoch;
        REFERENCE_TIME stop, boundary;
        bool stale;
        Delivery() : next(NULL), epoch(0), stop(0), boundary(0), stale(false) {}
    };
private:
    CCritSec m_lock;
    CEvent m_event;
    ULONGLONG m_epoch;
    bool m_pending, m_requested;
    REFERENCE_TIME m_boundary, m_requestedStop;
    Delivery* m_deliveries;

    void Pending(REFERENCE_TIME boundary) {
        if (!m_pending || boundary < m_boundary) m_boundary = boundary;
        m_pending = true;
        m_event.Set();
    }
    void ScheduleLocked(REFERENCE_TIME boundary) {
        Pending(boundary);
        for (Delivery* d = m_deliveries; d; d = d->next) {
            if (d->epoch == m_epoch && (boundary == 0 || d->stop > boundary)) {
                if (!d->stale || boundary < d->boundary) d->boundary = boundary;
                d->stale = true;
            }
        }
    }
public:
    ConsumerInvalidation() : m_epoch(0), m_pending(false), m_requested(false),
        m_boundary(0), m_requestedStop(0), m_deliveries(NULL) {
        m_event.Create(NULL, FALSE, FALSE, NULL);
    }
    HANDLE Event() const { return m_event; }
    ULONGLONG Epoch() {
        CAutoLock lock(&m_lock);
        return m_epoch;
    }
    // Old deliveries may finish, but cannot clear a replacement consumer.
    void Reset() {
        CAutoLock lock(&m_lock);
        ++m_epoch;
        m_pending = m_requested = false;
    }
    void Begin(Delivery& delivery, REFERENCE_TIME videoStop, REFERENCE_TIME subtitleStop) {
        CAutoLock lock(&m_lock);
        delivery.epoch = m_epoch;
        delivery.stop = videoStop;
        delivery.next = m_deliveries;
        m_deliveries = &delivery;
        if (!m_requested || subtitleStop > m_requestedStop) m_requestedStop = subtitleStop;
        m_requested = true;
    }
    void Finish(Delivery& delivery, bool delivered) {
        CAutoLock lock(&m_lock);
        Delivery** link = &m_deliveries;
        while (*link && *link != &delivery) link = &(*link)->next;
        if (*link) *link = delivery.next;
        // Only an affected delivery can require a second partial clear.
        if (delivered && delivery.epoch == m_epoch && delivery.stale) Pending(delivery.boundary);
    }
    void Schedule() {
        CAutoLock lock(&m_lock);
        m_requested = false;
        ScheduleLocked(0);
    }
    // Inverse of subtitle = (video - delay) * mul / div. Round down and
    // include equality for consumers interpreting Clear as strictly newer.
    // Unrepresentable or non-forward mappings conservatively use a full clear.
    static REFERENCE_TIME VideoBoundary(REFERENCE_TIME subtitle, int delay, int mul, int div) {
        if (subtitle < 0 || mul <= 0 || div <= 0) return 0;
        const REFERENCE_TIME limit = (std::numeric_limits<REFERENCE_TIME>::max)();
        const REFERENCE_TIME q = subtitle / mul, r = subtitle % mul;
        const REFERENCE_TIME tail = r * div / mul;
        if (q > (limit - tail) / div) return 0;
        REFERENCE_TIME video = q * div + tail;
        const REFERENCE_TIME offset = REFERENCE_TIME(delay) * 10000;
        if (offset > 0 && video > limit - offset) return 0;
        video += offset;
        return video > 0 ? video - 1 : 0;
    }
    void Packet(REFERENCE_TIME subtitleStart, int delay, int mul, int div) {
        CAutoLock lock(&m_lock);
        if (!m_requested || subtitleStart >= m_requestedStop) return;
        ScheduleLocked(VideoBoundary(subtitleStart, delay, mul, div));
    }
    HRESULT Dispatch(ISubRenderConsumer* consumer, ULONGLONG epoch) {
        REFERENCE_TIME boundary;
        {
            CAutoLock lock(&m_lock);
            if (epoch != m_epoch || !m_pending) return S_FALSE;
            boundary = m_boundary;
            m_pending = false;
        }
        CComQIPtr<ISubRenderConsumer2> extended(consumer);
        return extended ? extended->Clear(boundary) : S_FALSE;
    }
};
