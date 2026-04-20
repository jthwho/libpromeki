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
        [ "Adopt-task path (per-frame callback)", "inspector.html#inspector_construct_adopt", null ]
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
      [ "Available backends", "mediaio.html#mediaio_backends", null ],
      [ "Compressed bitstream flow", "mediaio.html#mediaio_compressed", null ],
      [ "Overview", "mediaio.html#mediaio_overview", null ],
      [ "Architecture", "mediaio.html#mediaio_architecture", [
        [ "Command pattern", "mediaio.html#mediaio_command_pattern", null ],
        [ "Strand-based serialization", "mediaio.html#mediaio_strand", null ],
        [ "Lock-free data flow", "mediaio.html#mediaio_data_flow", null ],
        [ "Threading model", "mediaio.html#mediaio_threading", null ]
      ] ],
      [ "User API", "mediaio.html#mediaio_user_api", [
        [ "Creating an instance", "mediaio.html#mediaio_create", null ],
        [ "Open / close lifecycle", "mediaio.html#mediaio_lifecycle", null ],
        [ "Reading frames", "mediaio.html#mediaio_read", null ],
        [ "Writing frames", "mediaio.html#mediaio_write", null ],
        [ "Async close", "mediaio.html#mediaio_async_close", null ],
        [ "Seeking", "mediaio.html#mediaio_seek", null ],
        [ "Parameterized commands", "mediaio.html#mediaio_params", null ]
      ] ],
      [ "Authoring a backend", "mediaio.html#mediaio_authoring", [
        [ "Setup", "mediaio.html#mediaio_authoring_setup", null ],
        [ "Skeleton", "mediaio.html#mediaio_authoring_template", null ],
        [ "Open / close contract", "mediaio.html#mediaio_authoring_open", null ],
        [ "Reading", "mediaio.html#mediaio_authoring_read", null ],
        [ "Threading rules for backends", "mediaio.html#mediaio_authoring_threading", null ],
        [ "Parameterized command dispatch", "mediaio.html#mediaio_authoring_params", null ]
      ] ],
      [ "EOF semantics", "mediaio.html#mediaio_eof", null ],
      [ "Mid-stream descriptor changes", "mediaio.html#mediaio_descchange", null ],
      [ "Backend statistics", "mediaio.html#mediaio_stats", null ],
      [ "Live capture pattern", "mediaio.html#mediaio_capture", null ],
      [ "Per-frame metadata keys", "mediaio.html#mediaio_metadata", null ],
      [ "Thread pool sizing", "mediaio.html#mediaio_pool", null ]
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
      [ "Design Philosophy", "md_CODING__STANDARDS.html#autotoc_md28", null ],
      [ "Object Categories", "md_CODING__STANDARDS.html#object-categories", [
        [ "Data Objects", "md_CODING__STANDARDS.html#autotoc_md30", null ],
        [ "Functional Objects (ObjectBase)", "md_CODING__STANDARDS.html#autotoc_md31", null ],
        [ "Utility Classes", "md_CODING__STANDARDS.html#autotoc_md32", null ],
        [ "Choosing the Right Category", "md_CODING__STANDARDS.html#autotoc_md33", null ],
        [ "Sharing Data Objects Across Threads", "md_CODING__STANDARDS.html#autotoc_md34", null ]
      ] ],
      [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md36", [
        [ "Header Files", "md_CODING__STANDARDS.html#autotoc_md37", null ],
        [ "File Header Comment", "md_CODING__STANDARDS.html#autotoc_md38", null ],
        [ "Include Guards", "md_CODING__STANDARDS.html#autotoc_md39", null ],
        [ "Includes", "md_CODING__STANDARDS.html#autotoc_md40", null ]
      ] ],
      [ "Naming Conventions", "md_CODING__STANDARDS.html#autotoc_md42", [
        [ "Classes and Types", "md_CODING__STANDARDS.html#autotoc_md43", null ],
        [ "Convenience Type Aliases for Templates", "md_CODING__STANDARDS.html#autotoc_md44", null ],
        [ "Methods", "md_CODING__STANDARDS.html#autotoc_md45", null ],
        [ "Member Variables", "md_CODING__STANDARDS.html#autotoc_md46", null ],
        [ "Enums", "md_CODING__STANDARDS.html#autotoc_md47", null ],
        [ "Standard Library Type Aliases", "md_CODING__STANDARDS.html#autotoc_md48", null ],
        [ "Prefer Library Wrappers Over Raw std:: Types", "md_CODING__STANDARDS.html#autotoc_md49", null ],
        [ "Macros", "md_CODING__STANDARDS.html#autotoc_md50", null ],
        [ "Constants", "md_CODING__STANDARDS.html#autotoc_md51", null ]
      ] ],
      [ "Indentation and Formatting", "md_CODING__STANDARDS.html#autotoc_md53", null ],
      [ "The SharedPtr / Copy-on-Write Pattern", "md_CODING__STANDARDS.html#autotoc_md55", [
        [ "Shareable Data Objects Support SharedPtr", "md_CODING__STANDARDS.html#autotoc_md56", null ],
        [ "No Internal SharedPtr<Data>", "md_CODING__STANDARDS.html#autotoc_md57", null ],
        [ "How to Implement (Shareable Classes)", "md_CODING__STANDARDS.html#autotoc_md58", null ]
      ] ],
      [ "Error Handling", "md_CODING__STANDARDS.html#autotoc_md60", [
        [ "The Error Class", "md_CODING__STANDARDS.html#autotoc_md61", null ],
        [ "Return Patterns", "md_CODING__STANDARDS.html#autotoc_md62", null ],
        [ "Avoid <tt>bool</tt> for Error Reporting", "md_CODING__STANDARDS.html#autotoc_md63", null ]
      ] ],
      [ "Container and Wrapper Types", "md_CODING__STANDARDS.html#autotoc_md65", [
        [ "String", "md_CODING__STANDARDS.html#autotoc_md66", null ],
        [ "List<T>", "md_CODING__STANDARDS.html#autotoc_md67", null ],
        [ "Array<T, N>", "md_CODING__STANDARDS.html#autotoc_md68", null ],
        [ "Buffer", "md_CODING__STANDARDS.html#autotoc_md69", null ]
      ] ],
      [ "TypeRegistry Types", "md_CODING__STANDARDS.html#autotoc_md71", [
        [ "Pass the Wrapper, Not the ID", "md_CODING__STANDARDS.html#autotoc_md72", null ],
        [ "ID Disambiguation Guards", "md_CODING__STANDARDS.html#autotoc_md73", null ]
      ] ],
      [ "Well-Known Enums", "md_CODING__STANDARDS.html#autotoc_md75", [
        [ "Use the <tt>TypedEnum<Derived></tt> CRTP Pattern", "md_CODING__STANDARDS.html#autotoc_md76", null ],
        [ "Where Well-Known Enums Live", "md_CODING__STANDARDS.html#autotoc_md77", null ],
        [ "Function Signatures", "md_CODING__STANDARDS.html#autotoc_md78", null ],
        [ "Backward Compatibility", "md_CODING__STANDARDS.html#autotoc_md79", null ]
      ] ],
      [ "ObjectBase and Signals/Slots", "md_CODING__STANDARDS.html#autotoc_md81", null ],
      [ "Namespace", "md_CODING__STANDARDS.html#autotoc_md83", null ],
      [ "Stream Operator Support (TextStream / DataStream)", "md_CODING__STANDARDS.html#autotoc_md85", [
        [ "TextStream", "md_CODING__STANDARDS.html#autotoc_md86", null ],
        [ "DataStream", "md_CODING__STANDARDS.html#autotoc_md87", null ],
        [ "When to Omit", "md_CODING__STANDARDS.html#autotoc_md88", null ]
      ] ],
      [ "Documentation (Doxygen)", "md_CODING__STANDARDS.html#autotoc_md90", [
        [ "Class Documentation", "md_CODING__STANDARDS.html#autotoc_md91", null ],
        [ "Method Documentation", "md_CODING__STANDARDS.html#autotoc_md92", null ],
        [ "Thread Safety Documentation", "md_CODING__STANDARDS.html#autotoc_md93", null ],
        [ "What Not to Document", "md_CODING__STANDARDS.html#autotoc_md94", null ]
      ] ],
      [ "Testing", "md_CODING__STANDARDS.html#autotoc_md96", [
        [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md97", null ],
        [ "Structure", "md_CODING__STANDARDS.html#autotoc_md98", null ],
        [ "Assertions", "md_CODING__STANDARDS.html#autotoc_md99", null ],
        [ "Build Integration", "md_CODING__STANDARDS.html#autotoc_md100", null ]
      ] ],
      [ "Logging", "md_CODING__STANDARDS.html#autotoc_md102", [
        [ "Use the Right Level", "md_CODING__STANDARDS.html#autotoc_md103", null ],
        [ "Register Every Source File for Debug Logging", "md_CODING__STANDARDS.html#autotoc_md104", null ],
        [ "Capture All Relevant State", "md_CODING__STANDARDS.html#autotoc_md105", null ],
        [ "Identify the Object Instance", "md_CODING__STANDARDS.html#autotoc_md106", null ],
        [ "Instrument the Library with promekiDebug", "md_CODING__STANDARDS.html#autotoc_md107", null ]
      ] ],
      [ "Miscellaneous", "md_CODING__STANDARDS.html#autotoc_md109", null ]
    ] ],
    [ "Signal List", "signal.html", null ],
    [ "Deprecated List", "deprecated.html", null ],
    [ "Todo List", "todo.html", null ],
    [ "Topics", "topics.html", "topics" ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
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
        [ "All", "globals.html", "globals_dup" ],
        [ "Functions", "globals_func.html", null ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerations", "globals_enum.html", null ],
        [ "Enumerator", "globals_eval.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"abstractsocket_8h.html",
"classAnsiStream.html#a338b08f6aae49641c1f62523260304dca6ae7c8570c8bb8c4e09aa3d7a3d04b14",
"classAtomic.html#a44907e07c9b466d45f4aa3ff48279efb",
"classAudioResampler.html#aea36a9456ad7ba2c96db1b532e2df215",
"classBufferedIODevice.html#aba8ed1673ddc0ed2246ad8ecdf886976",
"classColorModel.html#aca0059e9fdc0d7c39dec6c523c6cb3baa8e67296f20e43a5761f630b2d8215914",
"classDeque.html#a7dcf80ff0f942e745a86231860c1eddc",
"classEventLoop.html#acd0f2fd1a93194d07d2c9fe0c855b6a8",
"classFrameRate.html#a0b03c0fe2f653ec8479d240690191b9ca53b06ef3844e9cdac3a7915550fdf300",
"classImage.html#aed813848fdebb5874f9f29878cd7e5f1",
"classJpegXsVideoDecoder.html#a9ea854572897cd70ef6362ee2b303657",
"classList.html#ad5b73152c7d881729f8a66d20343cd90",
"classMediaConfig.html#a22edfa26a23de3516da2d4980d07f6eb",
"classMediaIOCommandOpen.html#a0d0b5f356c1896755dc09a049b29d4dc",
"classMediaPipelineStats.html#a374b0c85c8fbb480e574aa9670869764",
"classMidiNote.html#ae35b95311738089287eaa0e86d959ac3a5f7120c16316c63585d4801a5fbd4aa7",
"classMusicalNote.html#a8bb56002e10df5cda9edea4f9108b1d4",
"classPixelDesc.html#a177e81a9ea97518417e3efec00e77b75",
"classPixelFormat.html#a7ae7d8a6cf46a5c9947b72d5ec9d4deda7db3c60f269ab3f6bbfa4754dc647f96",
"classQuickTime_1_1Track.html#a5cb85e6a2a1694375577cda422d0e5fd",
"classRtpSession.html#a982403a5be30d9149c864f55df13eabf",
"classSlot.html#a6f03f41ff89a99870b1561cdb7de2bb3",
"classStringData.html#a8cb7a91035aebb89432af0f53136d634",
"classTerminal.html#ac5402e9e7338b30d030b591e29448f13",
"classTuiMenuBar.html",
"classUdpSocketTransport.html#af8e565b91b7ffdfd8857448f356f0b27",
"classVideoFormat.html#a4662636b759042b41f00b272dc5beabca4c68b0372df8f2611be33c1f5990a9d5",
"cscpipeline_8h_source.html",
"functions_type_v.html",
"line_8h.html#a7a8fa44bfd41c89b35458c6ec1b2f5c1",
"multicastmanager_8h.html",
"structAvcDecoderConfig.html#afbbb4c261c67825038c728611ccaea32",
"structIsSharedObject.html",
"structstd_1_1formatter_3_01promeki_1_1Timecode_01_4.html#ab12772e49ed650e120b926d82af4e651a5f1a5f9baac7b62ac1082576a667533f"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';