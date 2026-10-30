/************************************************************************/
/* author: xy                                                           */
/* date: 20110514                                                       */
/************************************************************************/
#ifndef __XY_MALLOC_H_22FA5F7D_A5C3_4D8D_B4E6_5FB954770019__
#define __XY_MALLOC_H_22FA5F7D_A5C3_4D8D_B4E6_5FB954770019__

#include <cstddef>

/* xy_malloc : will do or emulate a memalign
 * you have to use xy_free for buffers allocated with xy_malloc */
void *xy_malloc( std::size_t size, int align_shift=0);
void *xy_realloc( void *p, std::size_t i_size, int align_shift=0 );
void  xy_free( void * );

extern std::size_t g_xy_malloc_used_size;

// Optional serial native-harness probe. Counts xy_malloc traffic only, not
// ATL/STL/shared_ptr allocations. The caller restores the previous probe.
struct XyAllocationStats
{
    unsigned __int64 allocCalls, freeCalls, requestedBytes, failures;
    std::size_t peakUsed;
};
XyAllocationStats* xy_set_allocation_stats(XyAllocationStats* stats);
// Thread-local fault injection for the regression harness; -1 disables it.
long xy_set_allocation_failure_countdown(long count);

#endif // end of __XY_MALLOC_H_22FA5F7D_A5C3_4D8D_B4E6_5FB954770019__

