/**
 * @file      tests/mediaio_stats.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for MediaIO's live telemetry layer — the standard
 * MediaIOStats keys populated by the base class regardless of which
 * backend is running.  These cover BytesPerSecond / FramesPerSecond
 * derived from the RateTracker, the drop / repeat / late counters
 * routed through MediaIOTask::noteFrame* helpers, and the latency
 * keys derived from an attached BenchmarkReporter.  The TPG backend
 * is used for the rate tests because it needs no input files.  A
 * small purpose-built task exercises the drop / repeat / late path.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
