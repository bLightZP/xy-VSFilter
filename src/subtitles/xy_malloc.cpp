/************************************************************************/
/* author: xy                                                           */
/* date: 20110514                                                       */
/************************************************************************/
#include "xy_malloc.h"
#include <stdint.h>
#include <malloc.h>
#include <string.h>

extern std::size_t g_xy_malloc_used_size = 0;
static XyAllocationStats* s_allocation_stats = NULL;
static thread_local long s_failure_countdown = -1;
long xy_set_allocation_failure_countdown(long count)
{
    const long previous = s_failure_countdown;
    s_failure_countdown = count;
    return previous;
}

XyAllocationStats* xy_set_allocation_stats(XyAllocationStats* stats)
{
    XyAllocationStats* previous = s_allocation_stats;
    s_allocation_stats = stats;
    return previous;
}

//gathered from x264
/****************************************************************************
 * xy_malloc:
 ****************************************************************************/
void *xy_malloc( std::size_t i_size, int align_shift )
{
    if (s_allocation_stats) {
        ++s_allocation_stats->allocCalls;
        s_allocation_stats->requestedBytes += i_size;
    }
    if (i_size > std::size_t(-1) - 15 - sizeof(void*) - sizeof(i_size) || s_failure_countdown == 0) {
        if (s_allocation_stats) ++s_allocation_stats->failures;
        return NULL;
    }
    if (s_failure_countdown > 0) --s_failure_countdown;
    const int ALIGNMENT = 16;
    const int MASK = ALIGNMENT - 1;
    uint8_t * buf;
    uint8_t * align_buf;
    align_shift &= MASK;
    buf = (uint8_t *) malloc( i_size + MASK + sizeof( void ** ) +
              sizeof( i_size ) );
    if (!buf)
    {
        if (s_allocation_stats) ++s_allocation_stats->failures;
        return NULL;
    }
    align_buf = buf + MASK + sizeof( void ** ) + sizeof( i_size );
    align_buf -= (ALIGNMENT + ((intptr_t) align_buf & MASK) - align_shift) & MASK;
    *( (void **) ( align_buf - sizeof( void ** ) ) ) = buf;
    *( (std::size_t *) ( align_buf - sizeof( void ** ) - sizeof( i_size ) ) ) = i_size;
    g_xy_malloc_used_size += i_size;
    if (s_allocation_stats && g_xy_malloc_used_size > s_allocation_stats->peakUsed)
        s_allocation_stats->peakUsed = g_xy_malloc_used_size;
    return align_buf;
}

/****************************************************************************
 * xy_free:
 ****************************************************************************/
void xy_free( void *p )
{
    if( p )
    {
        if (s_allocation_stats) ++s_allocation_stats->freeCalls;
        g_xy_malloc_used_size -= *( (std::size_t*) ( (uint8_t*) p - sizeof( void ** ) -
            sizeof( std::size_t ) ) );
        free( *( ( ( void **) p ) - 1 ) );
    }
}

/****************************************************************************
 * xy_realloc:
 ****************************************************************************/
void *xy_realloc( void *p, std::size_t i_size, int align_shift )
{
    std::size_t i_old_size = 0;
    uint8_t    *p_new = NULL;
    p_new = (uint8_t*)xy_malloc( i_size, align_shift );
    if (p_new && p)
    {
        i_old_size = *( (std::size_t*) ( (uint8_t*) p - sizeof( void ** ) -
            sizeof( i_size ) ) );
        memcpy( p_new, p, ( i_old_size < i_size ) ? i_old_size : i_size );
        xy_free(p);
    }
    return p_new;
}


