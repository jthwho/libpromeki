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
    [ "Media I/O Subsystem", "mediaio.html", [
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
      [ "Pipeline Threading", "threading.html#thread_pipeline", null ]
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
      [ "Design Philosophy", "md_CODING__STANDARDS.html#autotoc_md15", null ],
      [ "Object Categories", "md_CODING__STANDARDS.html#object-categories", [
        [ "Data Objects", "md_CODING__STANDARDS.html#autotoc_md17", null ],
        [ "Functional Objects (ObjectBase)", "md_CODING__STANDARDS.html#autotoc_md18", null ],
        [ "Utility Classes", "md_CODING__STANDARDS.html#autotoc_md19", null ],
        [ "Choosing the Right Category", "md_CODING__STANDARDS.html#autotoc_md20", null ],
        [ "Sharing Data Objects Across Threads", "md_CODING__STANDARDS.html#autotoc_md21", null ]
      ] ],
      [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md23", [
        [ "Header Files", "md_CODING__STANDARDS.html#autotoc_md24", null ],
        [ "File Header Comment", "md_CODING__STANDARDS.html#autotoc_md25", null ],
        [ "Include Guards", "md_CODING__STANDARDS.html#autotoc_md26", null ],
        [ "Includes", "md_CODING__STANDARDS.html#autotoc_md27", null ]
      ] ],
      [ "Naming Conventions", "md_CODING__STANDARDS.html#autotoc_md29", [
        [ "Classes and Types", "md_CODING__STANDARDS.html#autotoc_md30", null ],
        [ "Convenience Type Aliases for Templates", "md_CODING__STANDARDS.html#autotoc_md31", null ],
        [ "Methods", "md_CODING__STANDARDS.html#autotoc_md32", null ],
        [ "Member Variables", "md_CODING__STANDARDS.html#autotoc_md33", null ],
        [ "Enums", "md_CODING__STANDARDS.html#autotoc_md34", null ],
        [ "Standard Library Type Aliases", "md_CODING__STANDARDS.html#autotoc_md35", null ],
        [ "Prefer Library Wrappers Over Raw std:: Types", "md_CODING__STANDARDS.html#autotoc_md36", null ],
        [ "Macros", "md_CODING__STANDARDS.html#autotoc_md37", null ],
        [ "Constants", "md_CODING__STANDARDS.html#autotoc_md38", null ]
      ] ],
      [ "Indentation and Formatting", "md_CODING__STANDARDS.html#autotoc_md40", null ],
      [ "The SharedPtr / Copy-on-Write Pattern", "md_CODING__STANDARDS.html#autotoc_md42", [
        [ "Shareable Data Objects Support SharedPtr", "md_CODING__STANDARDS.html#autotoc_md43", null ],
        [ "No Internal SharedPtr<Data>", "md_CODING__STANDARDS.html#autotoc_md44", null ],
        [ "How to Implement (Shareable Classes)", "md_CODING__STANDARDS.html#autotoc_md45", null ]
      ] ],
      [ "Error Handling", "md_CODING__STANDARDS.html#autotoc_md47", [
        [ "The Error Class", "md_CODING__STANDARDS.html#autotoc_md48", null ],
        [ "Return Patterns", "md_CODING__STANDARDS.html#autotoc_md49", null ],
        [ "Avoid <tt>bool</tt> for Error Reporting", "md_CODING__STANDARDS.html#autotoc_md50", null ]
      ] ],
      [ "Container and Wrapper Types", "md_CODING__STANDARDS.html#autotoc_md52", [
        [ "String", "md_CODING__STANDARDS.html#autotoc_md53", null ],
        [ "List<T>", "md_CODING__STANDARDS.html#autotoc_md54", null ],
        [ "Array<T, N>", "md_CODING__STANDARDS.html#autotoc_md55", null ],
        [ "Buffer", "md_CODING__STANDARDS.html#autotoc_md56", null ]
      ] ],
      [ "TypeRegistry Types", "md_CODING__STANDARDS.html#autotoc_md58", [
        [ "Pass the Wrapper, Not the ID", "md_CODING__STANDARDS.html#autotoc_md59", null ],
        [ "ID Disambiguation Guards", "md_CODING__STANDARDS.html#autotoc_md60", null ]
      ] ],
      [ "ObjectBase and Signals/Slots", "md_CODING__STANDARDS.html#autotoc_md62", null ],
      [ "Namespace", "md_CODING__STANDARDS.html#autotoc_md64", null ],
      [ "Stream Operator Support (TextStream / DataStream)", "md_CODING__STANDARDS.html#autotoc_md66", [
        [ "TextStream", "md_CODING__STANDARDS.html#autotoc_md67", null ],
        [ "DataStream", "md_CODING__STANDARDS.html#autotoc_md68", null ],
        [ "When to Omit", "md_CODING__STANDARDS.html#autotoc_md69", null ]
      ] ],
      [ "Documentation (Doxygen)", "md_CODING__STANDARDS.html#autotoc_md71", [
        [ "Class Documentation", "md_CODING__STANDARDS.html#autotoc_md72", null ],
        [ "Method Documentation", "md_CODING__STANDARDS.html#autotoc_md73", null ],
        [ "Thread Safety Documentation", "md_CODING__STANDARDS.html#autotoc_md74", null ],
        [ "What Not to Document", "md_CODING__STANDARDS.html#autotoc_md75", null ]
      ] ],
      [ "Testing", "md_CODING__STANDARDS.html#autotoc_md77", [
        [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md78", null ],
        [ "Structure", "md_CODING__STANDARDS.html#autotoc_md79", null ],
        [ "Assertions", "md_CODING__STANDARDS.html#autotoc_md80", null ],
        [ "Build Integration", "md_CODING__STANDARDS.html#autotoc_md81", null ]
      ] ],
      [ "Miscellaneous", "md_CODING__STANDARDS.html#autotoc_md83", null ]
    ] ],
    [ "Signal List", "signal.html", null ],
    [ "Deprecated List", "deprecated.html", null ],
    [ "Topics", "topics.html", "topics" ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Enumerations", "functions_enum.html", null ],
        [ "Enumerator", "functions_eval.html", "functions_eval" ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
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
"classAnsiStream.html#a338b08f6aae49641c1f62523260304dca74ecc283316a8e346a06a1b341db8987",
"classAudio.html#a55558ab750f8d19bc9e068a40a1814b5",
"classBuffer.html#a59b8743e4a5f731bdd0c4185c9ef263b",
"classColorModel.html#a421e6efa9c86c51eca8171d50e67b3eea5fc098b14be50d988f4aa9064cda0878",
"classDeque.html#af40009710b3c3cfd63eac95bcd66850b",
"classFileFormatFactory.html#a8dabd06c6ca8f9895403f17ae3fb148b",
"classHashSet.html#aa0883bc59820dba4bc6794b9429862ff",
"classJsonArray.html#a307c1bb8083bd9279bac468ba743a4c1",
"classLtcEncoder.html#a43fe5ab20bf8032320f1bb51521c37f5",
"classMediaIOTask__QuickTime.html#a22a8f086dd9a78c91d2a641e522e904b",
"classMidiNote.html#ae35b95311738089287eaa0e86d959ac3a1f1ae2c71f7f17491298ca98add28cc1",
"classMouseEvent.html#ac4f87f74cb9c1e9e4a0aa90566edb0a3aa7c37746fd19c7e7cb0d3247a998df09",
"classPixelDesc.html#a140cd1bb7d0493925dacc1920aa4cf4f",
"classPixelFormat.html#a7ae7d8a6cf46a5c9947b72d5ec9d4dedac769a931521157e10558665d1deb0e38",
"classRandom.html#a95fc7c64406684d9bd25cc959a1e4427",
"classSDLPlayerTask.html#a13c7e17c195b16fa2ab521d69e69c29b",
"classStreamString.html#a14c26f2adcda09c0f9762cf0c0c78109",
"classStringRegistry.html#ad5cc09e289e8a7c7f62ed579fd48d63d",
"classTimeStamp.html#afe174c0f23d337e02c7e5fdacbf2e73c",
"classTuiStyle.html#a6bc827a282a6cda728940488d6e02ddd",
"classWidget.html#a918bb0ea9555dc7b408c6a92d9d0b637",
"functions_func_y.html",
"md_CODING__STANDARDS.html#autotoc_md52",
"sharedptr_8h.html#a19c77a1148cf694c53a7d0d6608c0cd9",
"structPaintEngine_1_1Pixel.html#abb02de952987de672276c843cdf9996b"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';