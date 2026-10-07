/*
 * Named phases inside a frame.
 *
 * Nothing in this renderer is instrumented by the system: the whole paint shows up in a trace as
 * one anonymous `ReceiveVsync name:breezy` slice, which tells you a frame took 28 ms and nothing
 * about where the 28 ms went. These markers give the phases names, so hitrace attributes them the
 * way it attributes ArkUI's own measure and layout passes.
 *
 * They are ordinary hitrace slices, so they cost nothing unless a trace is running with the `app`
 * tag. Keep them: this project's whole method has been to measure rather than guess, and every
 * round of that has needed them back.
 */
#ifndef BREEZY_RENDER_FRAMETRACE_H
#define BREEZY_RENDER_FRAMETRACE_H

#include <hitrace/trace.h>

namespace breezy {

/** Opens a trace slice for the enclosing scope. */
class TraceScope {
public:
    explicit TraceScope(const char* name) { OH_HiTrace_StartTrace(name); }
    ~TraceScope() { OH_HiTrace_FinishTrace(); }
    TraceScope(const TraceScope&) = delete;
    TraceScope& operator=(const TraceScope&) = delete;
};

#define BREEZY_TRACE(name) ::breezy::TraceScope breezyTraceScope_(name)

}  // namespace breezy

#endif  // BREEZY_RENDER_FRAMETRACE_H
