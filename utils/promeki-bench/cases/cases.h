/**
 * @file      cases.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Per-suite registration hooks for promeki-bench case files.  Each
 * suite source file declares a `registerCases()` function here and
 * main.cpp calls every hook after parsing command-line arguments.
 *
 * Suites read `BenchOptions` to decide what to register (e.g. the CSC
 * suite switches between the standard pair matrix and a user-supplied
 * cross product based on `customSources` / `customDestinations`).
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

/**
 * @brief Registers CSC pair cases into the BenchmarkRunner registry.
 *
 * Reads `csc.src` and `csc.dst` from BenchParams to decide whether to
 * register the standard conversion matrix or a user-supplied cross
 * product.  Must be called after BenchParams is populated (main.cpp
 * does that from `-p` arguments) and before
 * `BenchmarkRunner::runAll()`.
 */
void registerCscCases();

/**
 * @brief Returns per-suite help text for the CSC suite.
 *
 * Printed by main.cpp when the user passes `--help`.  Describes the
 * `csc.*` parameter keys each case reads from `BenchParams` and the
 * default values applied when a key is missing.
 */
String cscParamHelp();

/**
 * @brief Registers ImageDataEncoder + ImageDataDecoder cases.
 *
 * Reads `imagedata.format` and `imagedata.size` from BenchParams to
 * decide which (PixelDesc, dimensions) pairs to register.
 */
void registerImageDataCases();

/** @brief Returns per-suite help text for the imagedata suite. */
String imageDataParamHelp();

/**
 * @brief Registers MediaIOTask_Inspector full-pipeline cases.
 *
 * Reads `inspector.format` and `inspector.size` from BenchParams to
 * decide which TPG → Inspector pipeline configurations to register.
 */
void registerInspectorCases();

/** @brief Returns per-suite help text for the inspector suite. */
String inspectorParamHelp();

} // namespace benchutil
PROMEKI_NAMESPACE_END
