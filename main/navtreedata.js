/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "libpromeki", "index.html", [
    [ "PROfessional MEdia toolKIt", "index.html", "index" ],
    [ "Building and Installing libpromeki", "building.html", [
      [ "Requirements", "building.html#building_requirements", null ],
      [ "Quick Start", "building.html#building_quickstart", null ],
      [ "Build Types", "building.html#building_build_types", null ],
      [ "Feature Flags", "building.html#building_feature_flags", null ],
      [ "Vendored vs. System Dependencies", "building.html#building_system_deps", null ],
      [ "Build Targets", "building.html#building_build_targets", null ],
      [ "Build Performance Options", "building.html#building_perf_options", null ],
      [ "Common Configurations", "building.html#building_configurations", [
        [ "Minimal core-only build", "building.html#building_config_minimal", null ],
        [ "System dependencies", "building.html#building_config_system_deps", null ],
        [ "Release packaging", "building.html#building_config_release", null ]
      ] ],
      [ "Cleaning", "building.html#building_cleaning", null ],
      [ "Running Tests", "building.html#building_tests", null ],
      [ "Code Formatting", "building.html#building_format", null ],
      [ "Pre-commit Verification", "building.html#building_precommit", null ],
      [ "Installing", "building.html#building_installing", null ],
      [ "Using libpromeki in Your Project", "building.html#building_downstream", [
        [ "Include Conventions", "building.html#building_include_conventions", null ]
      ] ],
      [ "Building the Documentation", "building.html#building_docs", null ],
      [ "See Also", "building.html#building_see_also", null ]
    ] ],
    [ "Color Science", "color_science.html", [
      [ "What Is a Color?", "color_science.html#cs_what", null ],
      [ "CIE XYZ: The Universal Color Space", "color_science.html#cs_cie", null ],
      [ "Chromaticity: Separating Color from Brightness", "color_science.html#cs_chromaticity", null ],
      [ "RGB Color Spaces", "color_science.html#cs_rgb", [
        [ "Transfer Functions (Gamma)", "color_science.html#cs_transfer", null ],
        [ "Prime Notation: R'G'B' vs RGB", "color_science.html#cs_prime", null ]
      ] ],
      [ "Derived Color Models", "color_science.html#cs_derived", [
        [ "HSV and HSL", "color_science.html#cs_hsv", null ],
        [ "YCbCr (Luma/Chroma)", "color_science.html#cs_ycbcr", [
          [ "Why YCbCr, not YUV?", "color_science.html#cs_not_yuv", null ]
        ] ]
      ] ],
      [ "CIE Perceptual Models", "color_science.html#cs_cie_models", [
        [ "CIE L*a*b*", "color_science.html#cs_lab", null ]
      ] ],
      [ "Component Normalization", "color_science.html#cs_normalization", null ],
      [ "How Conversion Works", "color_science.html#cs_conversion", null ],
      [ "Library Classes", "color_science.html#cs_classes", [
        [ "Quick Start", "color_science.html#cs_usage", null ]
      ] ],
      [ "Further Reading", "color_science.html#cs_further", null ]
    ] ],
    [ "Color Space Conversion (CSC) Framework", "csc.html", [
      [ "Overview", "csc.html#csc_overview", null ],
      [ "Pipeline Architecture", "csc.html#csc_pipeline", null ],
      [ "Fast-Path Kernels", "csc.html#csc_fastpaths", null ],
      [ "Accuracy Characteristics", "csc.html#csc_accuracy", null ],
      [ "Configuration", "csc.html#csc_config", null ],
      [ "Thread Safety", "csc.html#csc_threading", null ],
      [ "Adding Custom Fast Paths", "csc.html#csc_extending", null ],
      [ "Test Strategy", "csc.html#csc_testing", null ]
    ] ],
    [ "Data Object Categories", "dataobjects.html", [
      [ "Simple", "dataobjects.html#simple", null ],
      [ "Shareable", "dataobjects.html#shareable", null ],
      [ "Thread Safety and Cross-Thread Sharing", "dataobjects.html#dataobj_threading", [
        [ "Pattern 1: Share a single object via Ptr", "dataobjects.html#thread_ptr", null ],
        [ "Pattern 2: Composite structure via Ptr", "dataobjects.html#thread_composite", null ],
        [ "Simple types: just copy", "dataobjects.html#thread_simple", null ],
        [ "Avoid: Mutex around data objects", "dataobjects.html#thread_antipattern", null ]
      ] ]
    ] ],
    [ "Debugging and Diagnostics", "debugging.html", [
      [ "Build Types", "debugging.html#debug_build_types", [
        [ "DevRelease — the recommended default", "debugging.html#debug_build_devrelease", null ],
        [ "Debug", "debugging.html#debug_build_debug", null ],
        [ "Release", "debugging.html#debug_build_release", null ]
      ] ],
      [ "The Logger System", "debugging.html#debug_logging", [
        [ "Convenience Macros", "debugging.html#debug_logging_macros", null ],
        [ "Configuring the Logger", "debugging.html#debug_logging_config", null ],
        [ "Flushing Log Output", "debugging.html#debug_logging_sync", null ]
      ] ],
      [ "Per-Module Debug Logging (promekiDebug)", "debugging.html#debug_promekidebug", [
        [ "How It Works", "debugging.html#debug_promekidebug_how", null ],
        [ "Activating Debug Output", "debugging.html#debug_promekidebug_activate", null ],
        [ "What Happens in Release Builds?", "debugging.html#debug_promekidebug_compiled_out", null ],
        [ "Benchmarking with PROMEKI_BENCHMARK", "debugging.html#debug_promekidebug_benchmarks", null ]
      ] ],
      [ "Crash Handling", "debugging.html#debug_crashhandler", [
        [ "What the Crash Report Contains", "debugging.html#debug_crash_report", null ],
        [ "Automatic Installation via Application", "debugging.html#debug_crash_auto", null ],
        [ "Manual Installation", "debugging.html#debug_crash_manual", null ],
        [ "Refreshing the Snapshot", "debugging.html#debug_crash_refresh", null ],
        [ "Diagnostic Traces Without Crashing", "debugging.html#debug_crash_trace", null ],
        [ "Enabling Core Dumps", "debugging.html#debug_crash_coredumps", null ],
        [ "CrashHandler Library Options", "debugging.html#debug_crash_options", null ]
      ] ],
      [ "Debug HTTP Server", "debugging.html#debug_http_server", [
        [ "Quickstart: env-var activation", "debugging.html#debug_http_envvar", null ],
        [ "Programmatic control", "debugging.html#debug_http_programmatic", null ],
        [ "Using DebugServer directly", "debugging.html#debug_http_direct", null ],
        [ "API endpoints", "debugging.html#debug_http_api", null ],
        [ "Logger listener API", "debugging.html#debug_logging_listeners", null ]
      ] ],
      [ "Quick Reference", "debugging.html#debug_summary", [
        [ "Environment Variables", "debugging.html#debug_summary_env", null ],
        [ "Typical Debugging Workflow", "debugging.html#debug_summary_workflow", null ]
      ] ]
    ] ],
    [ "Demonstration Applications", "demos.html", [
      [ "Building", "demos.html#demos_building", null ],
      [ "Standalone Build", "demos.html#demos_standalone", null ],
      [ "Adding a New Demo", "demos.html#demos_adding", null ],
      [ "Current Demos", "demos.html#demos_list", null ]
    ] ],
    [ "Font Rendering", "fonts.html", [
      [ "Class Hierarchy", "fonts.html#font_hierarchy", null ],
      [ "Choosing a Renderer", "fonts.html#font_choosing", null ],
      [ "Color Semantics", "fonts.html#font_colors", null ],
      [ "Kerning", "fonts.html#font_kerning", null ],
      [ "Font Metrics", "fonts.html#font_metrics", null ],
      [ "PaintEngine and Invalidation", "fonts.html#font_paintengine", null ],
      [ "Examples", "fonts.html#font_examples", [
        [ "FastFont: Timecode Overlay", "fonts.html#font_example_fast", null ],
        [ "BasicFont: Transparent Overlay", "fonts.html#font_example_basic", null ]
      ] ]
    ] ],
    [ "Image Data Encoder Wire Format", "imagedataencoder.html", [
      [ "Overview", "imagedataencoder.html#img_data_overview", null ],
      [ "Bit and byte ordering", "imagedataencoder.html#img_data_bitorder", null ],
      [ "Bit cell width selection", "imagedataencoder.html#img_data_cellwidth", null ],
      [ "Per-format value mapping", "imagedataencoder.html#img_data_value_mapping", null ],
      [ "How to write a decoder", "imagedataencoder.html#img_data_decode", null ],
      [ "Alignment notes", "imagedataencoder.html#img_data_alignment", null ],
      [ "Worked example", "imagedataencoder.html#img_data_example", null ],
      [ "Why not actual VITC?", "imagedataencoder.html#img_data_format_history", null ]
    ] ],
    [ "Inspector — Frame validation and monitoring", "inspector.html", [
      [ "Quick start", "inspector.html#inspector_quickstart", null ],
      [ "The inspector tests", "inspector.html#inspector_checks", [
        [ "Picture data band decode", "inspector.html#inspector_check_picture", null ],
        [ "Audio LTC decode", "inspector.html#inspector_check_ltc", null ],
        [ "A/V Sync", "inspector.html#inspector_check_avsync", null ],
        [ "Continuity tracking", "inspector.html#inspector_check_continuity", null ]
      ] ],
      [ "Consuming the results", "inspector.html#inspector_results", [
        [ "Per-frame callback", "inspector.html#inspector_callback", null ],
        [ "Accumulator snapshot", "inspector.html#inspector_snapshot", null ],
        [ "Periodic log", "inspector.html#inspector_log", null ]
      ] ],
      [ "Construction patterns", "inspector.html#inspector_construct", [
        [ "Standard factory (no callback)", "inspector.html#inspector_construct_factory", null ],
        [ "Direct-construction path (per-frame callback)", "inspector.html#inspector_construct_direct", null ]
      ] ],
      [ "What to look for in CI / QA", "inspector.html#inspector_what_to_look_for", null ],
      [ "Annotated log reference", "inspector.html#inspector_log_reference", [
        [ "At open time — configuration block", "inspector.html#inspector_log_config", null ],
        [ "Periodic report (one block per interval)", "inspector.html#inspector_log_periodic", null ],
        [ "Discontinuity warnings (immediate)", "inspector.html#inspector_log_warnings", null ]
      ] ],
      [ "Known limits", "inspector.html#inspector_known_limits", null ],
      [ "See also", "inspector.html#inspector_see_also", null ]
    ] ],
    [ "Media I/O Subsystem", "mediaio.html", [
      [ "Two guides", "mediaio.html#mediaio_guides", null ],
      [ "See also", "mediaio.html#mediaio_see_also", null ]
    ] ],
    [ "MediaIO — Backend Author Guide", "mediaio_backend_guide.html", [
      [ "Class hierarchy", "mediaio_backend_guide.html#mediaio_backend_hierarchy", [
        [ "Picking a strategy", "mediaio_backend_guide.html#mediaio_backend_picking_strategy", null ]
      ] ],
      [ "The factory contract", "mediaio_backend_guide.html#mediaio_backend_factory", null ],
      [ "Implementing executeCmd", "mediaio_backend_guide.html#mediaio_backend_executecmd", [
        [ "The lifecycle of a command", "mediaio_backend_guide.html#mediaio_backend_lifecycle", null ],
        [ "Open and port construction", "mediaio_backend_guide.html#mediaio_backend_open", null ],
        [ "Reading frames", "mediaio_backend_guide.html#mediaio_backend_read", null ],
        [ "Writing frames", "mediaio_backend_guide.html#mediaio_backend_write", null ],
        [ "Seek", "mediaio_backend_guide.html#mediaio_backend_seek", null ],
        [ "Parameterized commands", "mediaio_backend_guide.html#mediaio_backend_params", null ],
        [ "Stats", "mediaio_backend_guide.html#mediaio_backend_stats", null ]
      ] ],
      [ "Cancellation contract", "mediaio_backend_guide.html#mediaio_backend_cancel", null ],
      [ "Re-entrancy and thread safety", "mediaio_backend_guide.html#mediaio_backend_thread_safety", null ],
      [ "Live capture pattern", "mediaio_backend_guide.html#mediaio_backend_capture", null ],
      [ "Auto-bridge transforms", "mediaio_backend_guide.html#mediaio_backend_bridge", null ],
      [ "Testing", "mediaio_backend_guide.html#mediaio_backend_test", null ],
      [ "Files", "mediaio_backend_guide.html#mediaio_backend_files", null ]
    ] ],
    [ "MediaIO — User Guide", "mediaio_user_guide.html", [
      [ "Concepts", "mediaio_user_guide.html#mediaio_user_concepts", null ],
      [ "Factory entry points", "mediaio_user_guide.html#mediaio_user_factories", null ],
      [ "Always-async API", "mediaio_user_guide.html#mediaio_user_requests", null ],
      [ "Lifecycle", "mediaio_user_guide.html#mediaio_user_lifecycle", null ],
      [ "Ports and port groups", "mediaio_user_guide.html#mediaio_user_ports", [
        [ "Reading frames", "mediaio_user_guide.html#mediaio_user_read", null ],
        [ "Writing frames", "mediaio_user_guide.html#mediaio_user_write", null ],
        [ "Seeking", "mediaio_user_guide.html#mediaio_user_seek", null ],
        [ "Cached state and signals", "mediaio_user_guide.html#mediaio_user_cache", null ]
      ] ],
      [ "Wiring transfers", "mediaio_user_guide.html#mediaio_user_connections", null ],
      [ "Cancellation", "mediaio_user_guide.html#mediaio_user_cancel", null ],
      [ "Parameterized commands", "mediaio_user_guide.html#mediaio_user_params", null ],
      [ "Stats", "mediaio_user_guide.html#mediaio_user_stats", null ],
      [ "Pipeline composition", "mediaio_user_guide.html#mediaio_user_pipeline", null ],
      [ "Available backends", "mediaio_user_guide.html#mediaio_user_backends", null ]
    ] ],
    [ "MediaPipeline", "mediapipeline.html", [
      [ "Overview", "mediapipeline.html#mediapipeline_overview", null ],
      [ "Lifecycle", "mediapipeline.html#mediapipeline_lifecycle", null ],
      [ "JSON schema", "mediapipeline.html#mediapipeline_json", null ],
      [ "Topology rules", "mediapipeline.html#mediapipeline_topology", null ],
      [ "Statistics", "mediapipeline.html#mediapipeline_stats", null ],
      [ "mediaplay CLI integration", "mediapipeline.html#mediapipeline_mediaplay", null ],
      [ "Code walk-through", "mediapipeline.html#mediapipeline_example", null ]
    ] ],
    [ "MediaPipelinePlanner", "mediaplanner.html", [
      [ "When the planner runs", "mediaplanner.html#mediaplanner_when", null ],
      [ "Algorithm", "mediaplanner.html#mediaplanner_algorithm", null ],
      [ "Cost scale", "mediaplanner.html#mediaplanner_costs", null ],
      [ "Tuning the planner — Policy", "mediaplanner.html#mediaplanner_policy", null ],
      [ "Authoring a new bridge backend", "mediaplanner.html#mediaplanner_authoring", null ],
      [ "Diagnostic output", "mediaplanner.html#mediaplanner_diagnostics", null ],
      [ "Worked examples", "mediaplanner.html#mediaplanner_examples", [
        [ "Source produces RGBA8, sink wants NV12", "mediaplanner.html#mediaplanner_ex_pixel_gap", null ],
        [ "Compressed source → uncompressed sink", "mediaplanner.html#mediaplanner_ex_decoder", null ],
        [ "Compressed → different compressed codec", "mediaplanner.html#mediaplanner_ex_codec_transitive", null ]
      ] ],
      [ "Known limits", "mediaplanner.html#mediaplanner_limits", null ]
    ] ],
    [ "NDI setup", "ndi.html", [
      [ "1. Download the SDK", "ndi.html#ndi_sdk", null ],
      [ "2. Point libpromeki at the SDK", "ndi.html#ndi_configure", null ],
      [ "3. Runtime library", "ndi.html#ndi_runtime", null ],
      [ "Verification", "ndi.html#ndi_verify", null ],
      [ "Troubleshooting", "ndi.html#ndi_troubleshoot", [
        [ "CMake cannot find the SDK", "ndi.html#ndi_trouble_sdk", null ],
        [ "libndi not found at runtime", "ndi.html#ndi_trouble_runtime", null ]
      ] ]
    ] ],
    [ "NVIDIA NVENC setup", "nvenc.html", [
      [ "1. Driver and NVENC runtime", "nvenc.html#nvenc_driver", null ],
      [ "2. CUDA toolkit", "nvenc.html#nvenc_cuda", null ],
      [ "3. NVIDIA Video Codec SDK", "nvenc.html#nvenc_sdk", null ],
      [ "Verification", "nvenc.html#nvenc_verify", null ],
      [ "Troubleshooting", "nvenc.html#nvenc_troubleshoot", [
        [ "libnvidia-encode not found at runtime", "nvenc.html#nvenc_trouble_runtime", null ],
        [ "CMake cannot find the SDK", "nvenc.html#nvenc_trouble_sdk", null ],
        [ "Driver / toolkit version skew", "nvenc.html#nvenc_trouble_driver_mismatch", null ]
      ] ]
    ] ],
    [ "Threading and Concurrency", "threading.html", [
      [ "Threading Model", "threading.html#thread_model", null ],
      [ "Thread Safety Categories", "threading.html#thread_categories", null ],
      [ "Sharing Data Objects", "threading.html#thread_data", null ],
      [ "ObjectBase and Thread Affinity", "threading.html#thread_objectbase", [
        [ "Cross-Thread Signals", "threading.html#thread_signals", null ]
      ] ],
      [ "EventLoop", "threading.html#thread_eventloop", null ],
      [ "Concurrency Primitives", "threading.html#thread_primitives", [
        [ "Mutex", "threading.html#thread_mutex", null ],
        [ "ReadWriteLock", "threading.html#thread_rwlock", null ],
        [ "WaitCondition", "threading.html#thread_waitcondition", null ],
        [ "Atomic", "threading.html#thread_atomic", null ],
        [ "Future and Promise", "threading.html#thread_future", null ]
      ] ],
      [ "ThreadPool", "threading.html#thread_pool", null ],
      [ "MediaIO Threading", "threading.html#thread_pipeline", null ]
    ] ],
    [ "TypeRegistry Pattern", "typeregistry.html", [
      [ "Overview", "typeregistry.html#tr_overview", null ],
      [ "Construction and Copying", "typeregistry.html#tr_construction", null ],
      [ "Registering User-Defined Types", "typeregistry.html#tr_extension", null ],
      [ "Design Guidelines", "typeregistry.html#tr_guidelines", null ],
      [ "ID Disambiguation Guards", "typeregistry.html#tr_disambiguation", null ],
      [ "Classes Using This Pattern", "typeregistry.html#tr_classes", null ]
    ] ],
    [ "Utility Applications", "utils.html", [
      [ "Building", "utils.html#utils_building", null ],
      [ "Standalone Build", "utils.html#utils_standalone", null ],
      [ "Adding a New Utility", "utils.html#utils_adding", null ],
      [ "Current Utilities", "utils.html#utils_list", null ]
    ] ],
    [ "Coding Standards", "md_CODING__STANDARDS.html", [
      [ "Design Philosophy", "md_CODING__STANDARDS.html#autotoc_md40", null ],
      [ "Object Categories", "md_CODING__STANDARDS.html#object-categories", [
        [ "Data Objects", "md_CODING__STANDARDS.html#autotoc_md42", null ],
        [ "Functional Objects (ObjectBase)", "md_CODING__STANDARDS.html#autotoc_md43", null ],
        [ "Utility Classes", "md_CODING__STANDARDS.html#autotoc_md44", null ],
        [ "Choosing the Right Category", "md_CODING__STANDARDS.html#autotoc_md45", null ],
        [ "Sharing Data Objects Across Threads", "md_CODING__STANDARDS.html#autotoc_md46", null ]
      ] ],
      [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md48", [
        [ "Header Files", "md_CODING__STANDARDS.html#autotoc_md49", null ],
        [ "File Header Comment", "md_CODING__STANDARDS.html#autotoc_md50", null ],
        [ "Include Guards", "md_CODING__STANDARDS.html#autotoc_md51", null ],
        [ "Includes", "md_CODING__STANDARDS.html#autotoc_md52", null ]
      ] ],
      [ "Naming Conventions", "md_CODING__STANDARDS.html#autotoc_md54", [
        [ "Classes and Types", "md_CODING__STANDARDS.html#autotoc_md55", null ],
        [ "Convenience Type Aliases for Templates", "md_CODING__STANDARDS.html#convenience-type-aliases-for-templates", [
          [ "Where the Alias Lives", "md_CODING__STANDARDS.html#autotoc_md56", null ],
          [ "Class-Scope Ownership Aliases", "md_CODING__STANDARDS.html#autotoc_md57", null ],
          [ "Opaque Forward-Declared Types", "md_CODING__STANDARDS.html#autotoc_md58", null ],
          [ "Numeric Template Aliases", "md_CODING__STANDARDS.html#autotoc_md59", null ]
        ] ],
        [ "Methods", "md_CODING__STANDARDS.html#autotoc_md60", null ],
        [ "Member Variables", "md_CODING__STANDARDS.html#autotoc_md61", null ],
        [ "Enums", "md_CODING__STANDARDS.html#autotoc_md62", null ],
        [ "Standard Library Type Aliases", "md_CODING__STANDARDS.html#autotoc_md63", null ],
        [ "Prefer Library Wrappers Over Raw std:: Types", "md_CODING__STANDARDS.html#autotoc_md64", null ],
        [ "Macros", "md_CODING__STANDARDS.html#autotoc_md65", null ],
        [ "Constants", "md_CODING__STANDARDS.html#autotoc_md66", null ]
      ] ],
      [ "Indentation and Formatting", "md_CODING__STANDARDS.html#autotoc_md68", null ],
      [ "Heap Ownership and Pointer Types", "md_CODING__STANDARDS.html#heap-ownership-and-pointer-types", [
        [ "The Three Tools", "md_CODING__STANDARDS.html#autotoc_md70", null ],
        [ "Never Use Raw <tt>new</tt> / <tt>delete</tt> for Ownership", "md_CODING__STANDARDS.html#autotoc_md71", null ],
        [ "Choosing Between SharedPtr and UniquePtr", "md_CODING__STANDARDS.html#autotoc_md72", null ],
        [ "Type Aliases for Heap-Managed Types", "md_CODING__STANDARDS.html#autotoc_md73", null ],
        [ "Conditional Ownership (Dual-Pointer Pattern)", "md_CODING__STANDARDS.html#autotoc_md74", null ],
        [ "Caveats and Limitations", "md_CODING__STANDARDS.html#caveats-and-limitations", null ],
        [ "Accessors on UniquePtr / SharedPtr", "md_CODING__STANDARDS.html#autotoc_md75", null ]
      ] ],
      [ "The SharedPtr / Copy-on-Write Pattern", "md_CODING__STANDARDS.html#the-sharedptr--copy-on-write-pattern", [
        [ "Shareable Data Objects Support SharedPtr", "md_CODING__STANDARDS.html#autotoc_md77", null ],
        [ "No Internal SharedPtr<Data>", "md_CODING__STANDARDS.html#autotoc_md78", null ],
        [ "How to Implement (Shareable Classes)", "md_CODING__STANDARDS.html#autotoc_md79", null ]
      ] ],
      [ "Error Handling", "md_CODING__STANDARDS.html#autotoc_md81", [
        [ "The Error Class", "md_CODING__STANDARDS.html#autotoc_md82", null ],
        [ "Return Patterns", "md_CODING__STANDARDS.html#autotoc_md83", null ],
        [ "Avoid <tt>bool</tt> for Error Reporting", "md_CODING__STANDARDS.html#autotoc_md84", null ]
      ] ],
      [ "Container and Wrapper Types", "md_CODING__STANDARDS.html#autotoc_md86", [
        [ "String", "md_CODING__STANDARDS.html#autotoc_md87", null ],
        [ "List<T>", "md_CODING__STANDARDS.html#autotoc_md88", null ],
        [ "Array<T, N>", "md_CODING__STANDARDS.html#autotoc_md89", null ],
        [ "Buffer", "md_CODING__STANDARDS.html#autotoc_md90", null ]
      ] ],
      [ "TypeRegistry Types", "md_CODING__STANDARDS.html#autotoc_md92", [
        [ "Pass the Wrapper, Not the ID", "md_CODING__STANDARDS.html#autotoc_md93", null ],
        [ "ID Disambiguation Guards", "md_CODING__STANDARDS.html#autotoc_md94", null ]
      ] ],
      [ "Well-Known Enums", "md_CODING__STANDARDS.html#autotoc_md96", [
        [ "Use the <tt>TypedEnum<Derived></tt> CRTP Pattern", "md_CODING__STANDARDS.html#autotoc_md97", null ],
        [ "Where Well-Known Enums Live", "md_CODING__STANDARDS.html#autotoc_md98", null ],
        [ "Function Signatures", "md_CODING__STANDARDS.html#autotoc_md99", null ],
        [ "Backward Compatibility", "md_CODING__STANDARDS.html#autotoc_md100", null ]
      ] ],
      [ "ObjectBase and Signals/Slots", "md_CODING__STANDARDS.html#autotoc_md102", null ],
      [ "Namespace", "md_CODING__STANDARDS.html#autotoc_md104", null ],
      [ "Stream Operator Support (TextStream / DataStream)", "md_CODING__STANDARDS.html#autotoc_md106", [
        [ "TextStream", "md_CODING__STANDARDS.html#autotoc_md107", null ],
        [ "DataStream", "md_CODING__STANDARDS.html#autotoc_md108", null ],
        [ "When to Omit", "md_CODING__STANDARDS.html#autotoc_md109", null ]
      ] ],
      [ "Documentation (Doxygen)", "md_CODING__STANDARDS.html#autotoc_md111", [
        [ "Class Documentation", "md_CODING__STANDARDS.html#autotoc_md112", null ],
        [ "Method Documentation", "md_CODING__STANDARDS.html#autotoc_md113", null ],
        [ "Thread Safety Documentation", "md_CODING__STANDARDS.html#autotoc_md114", null ],
        [ "What Not to Document", "md_CODING__STANDARDS.html#autotoc_md115", null ]
      ] ],
      [ "Testing", "md_CODING__STANDARDS.html#autotoc_md117", [
        [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md118", null ],
        [ "Structure", "md_CODING__STANDARDS.html#autotoc_md119", null ],
        [ "Assertions", "md_CODING__STANDARDS.html#autotoc_md120", null ],
        [ "Build Integration", "md_CODING__STANDARDS.html#autotoc_md121", null ]
      ] ],
      [ "Logging", "md_CODING__STANDARDS.html#autotoc_md123", [
        [ "Use the Right Level", "md_CODING__STANDARDS.html#autotoc_md124", null ],
        [ "Register Every Source File for Debug Logging", "md_CODING__STANDARDS.html#autotoc_md125", null ],
        [ "Capture All Relevant State", "md_CODING__STANDARDS.html#autotoc_md126", null ],
        [ "Identify the Object Instance", "md_CODING__STANDARDS.html#autotoc_md127", null ],
        [ "Instrument the Library with promekiDebug", "md_CODING__STANDARDS.html#autotoc_md128", null ]
      ] ],
      [ "Miscellaneous", "md_CODING__STANDARDS.html#autotoc_md130", null ]
    ] ],
    [ "Signal List", "signal.html", null ],
    [ "Todo List", "todo.html", null ],
    [ "Topics", "topics.html", "topics" ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", null ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Variables", "namespacemembers_vars.html", null ]
      ] ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", "functions_type" ],
        [ "Enumerations", "functions_enum.html", null ],
        [ "Enumerator", "functions_eval.html", "functions_eval" ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"abstractsocket_8h.html",
"classpromeki_1_1AnsiStream.html#a1520153b85ff3163bc6edf7d3f6c7159a3a617735ac16c6674665800a23531b7c",
"classpromeki_1_1AnsiStream.html#ae615039a0b00334b7daaf40e18336cc7",
"classpromeki_1_1AudioDesc.html#a3b186fc8b442dcf6cb9ff892d1d54625",
"classpromeki_1_1AudioPayload.html#adfe2350594b854a06b279beefe420380",
"classpromeki_1_1BufferPool.html#a8bb654d6d90606a4f1baf969bf516fa1",
"classpromeki_1_1Color.html#a346b2ee986a276eb63f9a4327615f9ea",
"classpromeki_1_1DataStream.html#a6f42a25d18c0819b66a14f6a1a4a947f",
"classpromeki_1_1Deque.html#a83f37df894c83e381b032daba87b8df9",
"classpromeki_1_1EventLoop.html#a1eb08211d520dd1eaf1931401a62d23eacf318e03b6a0a02c4ba96a5a191bbba1",
"classpromeki_1_1FrameBridgeMediaIO.html#aa9736476793aca6b6ef19a6cdb2feba1",
"classpromeki_1_1Histogram.html#a5789dbf8e73eeed5e853fc6c5d401ca1",
"classpromeki_1_1IODevice.html#a3afa7ed7733b8b9de76b0b78f623b292acdb3774fd138fdb72b5308a92c9b13e1",
"classpromeki_1_1JpegVideoEncoder.html#a5db27cfceda98b4a9258e429338775c1",
"classpromeki_1_1List.html#ac415520b92ecb036d7e5482abb5c0e04",
"classpromeki_1_1MediaDesc.html#a5175f90b4068f7884a5448d66115e6f6",
"classpromeki_1_1MediaIOFactory.html#adb88ef73d5ba1483a5e41ce06cf4fc34",
"classpromeki_1_1MediaPipeline.html#ae9f31a1651b7b189891ae65fdc005db4",
"classpromeki_1_1MidiNote.html#a7f331ab9ade9dcb8c59a678f9da30201a6d9b8d7cfa0b639a11d5bf6ae2603395",
"classpromeki_1_1MulticastReceiver.html#ad00ef58f396c9575db437ca6c56b3a6b",
"classpromeki_1_1PaintEngine.html#ad124f893591922595f9698289a299f01",
"classpromeki_1_1PixelFormat.html#a4612eae6db7110e291d37ac9b9a23207ade20cff84d765443617ea0d606c25951",
"classpromeki_1_1Promise.html#ace4d5287bbdbbfd4f60c5d3725a16ae6",
"classpromeki_1_1Rect.html#af60d4aab81459d2e180c39b10541c893",
"classpromeki_1_1SDLWindow.html#ab8e510656daea630907ed300e7015d46",
"classpromeki_1_1SrcFactory.html",
"classpromeki_1_1StringData.html#aa3b286612b0fc128ecc7a3a1fb55e7b6",
"classpromeki_1_1Terminal.html#a9b75fcd79debf22ed5795bf1ca191559",
"classpromeki_1_1TuiLineEdit.html#a01119c1674f3eb3b31bf04e5022a4aec",
"classpromeki_1_1UdpSocketTransport.html#a2fc8412a02fc806fbf2ed74bec30a2fd",
"classpromeki_1_1VideoCodec.html#a49d614967b7178e73a36429c7ffbe3e6",
"classpromeki_1_1VideoPayload.html#a944ec3f4995ad33075a783f2b8079107",
"crc_8h.html#a829c771f8e9f7710fabc3578ea831022",
"functions_func_k.html",
"imagefilemediaio_8h.html",
"mediaioclock_8h_source.html",
"screen_8h.html",
"structpromeki_1_1Enum_1_1Definition.html#a0e9aad16af4a74860221cf705c08f493",
"structpromeki_1_1MemSpace_1_1Ops.html#a011763b0226cdeedb21e4e0da8538cf1",
"structstd_1_1hash_3_01promeki_1_1ChannelRole_01_4.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';