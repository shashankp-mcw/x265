/*****************************************************************************
 * Copyright (C) 2026 MulticoreWare, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *****************************************************************************/

#ifndef X265_TRACY_H
#define X265_TRACY_H

#include "vendor/public/tracy/TracyC.h"

namespace X265_NS {

class TracyScopeEvent
{
public:
    explicit TracyScopeEvent(TracyCZoneCtx context) : m_context(context) {}
    ~TracyScopeEvent() { TracyCZoneEnd(m_context); }

    void value(uint64_t value) { TracyCZoneValue(m_context, value); }

private:
    TracyScopeEvent(const TracyScopeEvent&);
    TracyScopeEvent& operator=(const TracyScopeEvent&);

    TracyCZoneCtx m_context;
};

void tracySetThreadName(const char* name, int id);
void tracyAppInfo(const char* text);
void tracyPlot(const char* name, int64_t value);
void tracyPlot(const char* name, double value);

}

#if defined(__GNUC__)
#define X265_TRACY_SCOPE_EVENT(x) \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wshadow\"") \
    TracyCZoneN(_x265TracyContext, #x, 1) \
    X265_NS::TracyScopeEvent _x265TracyScope(_x265TracyContext); \
    _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#define X265_TRACY_SCOPE_EVENT(x) \
    __pragma(warning(push)) \
    __pragma(warning(disable: 4456)) \
    TracyCZoneN(_x265TracyContext, #x, 1) \
    X265_NS::TracyScopeEvent _x265TracyScope(_x265TracyContext); \
    __pragma(warning(pop))
#else
#define X265_TRACY_SCOPE_EVENT(x) \
    TracyCZoneN(_x265TracyContext, #x, 1) \
    X265_NS::TracyScopeEvent _x265TracyScope(_x265TracyContext)
#endif

#define X265_TRACY_SCOPE_VALUE(x) _x265TracyScope.value((uint64_t)(x))
#define X265_TRACY_FRAME_MARK() TracyCFrameMarkNamed("x265 output")

#endif // X265_TRACY_H
