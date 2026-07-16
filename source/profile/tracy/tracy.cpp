/*****************************************************************************
 * Copyright (C) 2026 MulticoreWare, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *****************************************************************************/

#include "common.h"

namespace X265_NS {

void tracySetThreadName(const char* name, int id)
{
    char threadName[128];
    snprintf(threadName, sizeof(threadName), "%s %d", name, id);
    TracyCSetThreadName(threadName);
}

void tracyAppInfo(const char* text)
{
    TracyCAppInfo(text, strlen(text));
}

void tracyPlot(const char* name, int64_t value)
{
    TracyCPlotI(name, value);
}

void tracyPlot(const char* name, double value)
{
    TracyCPlot(name, value);
}

}
