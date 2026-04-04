/**
 * @file      tui/layout.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/layout.h>

PROMEKI_NAMESPACE_BEGIN

// Backward-compatible type aliases so existing TUI code compiles
// unchanged.  The actual types now live in core/layout.h.
using TuiLayout = Layout;
using TuiBoxLayout = BoxLayout;
using TuiBoxDirection = BoxDirection;
using TuiHBoxLayout = HBoxLayout;
using TuiVBoxLayout = VBoxLayout;
using TuiGridLayout = GridLayout;

PROMEKI_NAMESPACE_END
