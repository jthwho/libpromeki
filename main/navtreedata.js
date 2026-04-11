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
      [ "Design Philosophy", "md_CODING__STANDARDS.html#autotoc_md23", null ],
      [ "Object Categories", "md_CODING__STANDARDS.html#object-categories", [
        [ "Data Objects", "md_CODING__STANDARDS.html#autotoc_md25", null ],
        [ "Functional Objects (ObjectBase)", "md_CODING__STANDARDS.html#autotoc_md26", null ],
        [ "Utility Classes", "md_CODING__STANDARDS.html#autotoc_md27", null ],
        [ "Choosing the Right Category", "md_CODING__STANDARDS.html#autotoc_md28", null ],
        [ "Sharing Data Objects Across Threads", "md_CODING__STANDARDS.html#autotoc_md29", null ]
      ] ],
      [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md31", [
        [ "Header Files", "md_CODING__STANDARDS.html#autotoc_md32", null ],
        [ "File Header Comment", "md_CODING__STANDARDS.html#autotoc_md33", null ],
        [ "Include Guards", "md_CODING__STANDARDS.html#autotoc_md34", null ],
        [ "Includes", "md_CODING__STANDARDS.html#autotoc_md35", null ]
      ] ],
      [ "Naming Conventions", "md_CODING__STANDARDS.html#autotoc_md37", [
        [ "Classes and Types", "md_CODING__STANDARDS.html#autotoc_md38", null ],
        [ "Convenience Type Aliases for Templates", "md_CODING__STANDARDS.html#autotoc_md39", null ],
        [ "Methods", "md_CODING__STANDARDS.html#autotoc_md40", null ],
        [ "Member Variables", "md_CODING__STANDARDS.html#autotoc_md41", null ],
        [ "Enums", "md_CODING__STANDARDS.html#autotoc_md42", null ],
        [ "Standard Library Type Aliases", "md_CODING__STANDARDS.html#autotoc_md43", null ],
        [ "Prefer Library Wrappers Over Raw std:: Types", "md_CODING__STANDARDS.html#autotoc_md44", null ],
        [ "Macros", "md_CODING__STANDARDS.html#autotoc_md45", null ],
        [ "Constants", "md_CODING__STANDARDS.html#autotoc_md46", null ]
      ] ],
      [ "Indentation and Formatting", "md_CODING__STANDARDS.html#autotoc_md48", null ],
      [ "The SharedPtr / Copy-on-Write Pattern", "md_CODING__STANDARDS.html#autotoc_md50", [
        [ "Shareable Data Objects Support SharedPtr", "md_CODING__STANDARDS.html#autotoc_md51", null ],
        [ "No Internal SharedPtr<Data>", "md_CODING__STANDARDS.html#autotoc_md52", null ],
        [ "How to Implement (Shareable Classes)", "md_CODING__STANDARDS.html#autotoc_md53", null ]
      ] ],
      [ "Error Handling", "md_CODING__STANDARDS.html#autotoc_md55", [
        [ "The Error Class", "md_CODING__STANDARDS.html#autotoc_md56", null ],
        [ "Return Patterns", "md_CODING__STANDARDS.html#autotoc_md57", null ],
        [ "Avoid <tt>bool</tt> for Error Reporting", "md_CODING__STANDARDS.html#autotoc_md58", null ]
      ] ],
      [ "Container and Wrapper Types", "md_CODING__STANDARDS.html#autotoc_md60", [
        [ "String", "md_CODING__STANDARDS.html#autotoc_md61", null ],
        [ "List<T>", "md_CODING__STANDARDS.html#autotoc_md62", null ],
        [ "Array<T, N>", "md_CODING__STANDARDS.html#autotoc_md63", null ],
        [ "Buffer", "md_CODING__STANDARDS.html#autotoc_md64", null ]
      ] ],
      [ "TypeRegistry Types", "md_CODING__STANDARDS.html#autotoc_md66", [
        [ "Pass the Wrapper, Not the ID", "md_CODING__STANDARDS.html#autotoc_md67", null ],
        [ "ID Disambiguation Guards", "md_CODING__STANDARDS.html#autotoc_md68", null ]
      ] ],
      [ "Well-Known Enums", "md_CODING__STANDARDS.html#autotoc_md70", [
        [ "Use the <tt>TypedEnum<Derived></tt> CRTP Pattern", "md_CODING__STANDARDS.html#autotoc_md71", null ],
        [ "Where Well-Known Enums Live", "md_CODING__STANDARDS.html#autotoc_md72", null ],
        [ "Function Signatures", "md_CODING__STANDARDS.html#autotoc_md73", null ],
        [ "Backward Compatibility", "md_CODING__STANDARDS.html#autotoc_md74", null ]
      ] ],
      [ "ObjectBase and Signals/Slots", "md_CODING__STANDARDS.html#autotoc_md76", null ],
      [ "Namespace", "md_CODING__STANDARDS.html#autotoc_md78", null ],
      [ "Stream Operator Support (TextStream / DataStream)", "md_CODING__STANDARDS.html#autotoc_md80", [
        [ "TextStream", "md_CODING__STANDARDS.html#autotoc_md81", null ],
        [ "DataStream", "md_CODING__STANDARDS.html#autotoc_md82", null ],
        [ "When to Omit", "md_CODING__STANDARDS.html#autotoc_md83", null ]
      ] ],
      [ "Documentation (Doxygen)", "md_CODING__STANDARDS.html#autotoc_md85", [
        [ "Class Documentation", "md_CODING__STANDARDS.html#autotoc_md86", null ],
        [ "Method Documentation", "md_CODING__STANDARDS.html#autotoc_md87", null ],
        [ "Thread Safety Documentation", "md_CODING__STANDARDS.html#autotoc_md88", null ],
        [ "What Not to Document", "md_CODING__STANDARDS.html#autotoc_md89", null ]
      ] ],
      [ "Testing", "md_CODING__STANDARDS.html#autotoc_md91", [
        [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md92", null ],
        [ "Structure", "md_CODING__STANDARDS.html#autotoc_md93", null ],
        [ "Assertions", "md_CODING__STANDARDS.html#autotoc_md94", null ],
        [ "Build Integration", "md_CODING__STANDARDS.html#autotoc_md95", null ]
      ] ],
      [ "Miscellaneous", "md_CODING__STANDARDS.html#autotoc_md97", null ]
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
"classBuffer.html#a46144112ecb2bd9593ba8767fb140145",
"classColorModel.html#a0571eb00d1b3301da80cf752c090e25a",
"classDeque.html#abdeec467505200791af25ca7f9b25974",
"classFile.html#ad8250f53607cae7768d22f9ec4adda97",
"classHashSet.html#a82499b530638bf1c0c05494b374e52e9",
"classJpegImageCodec.html#a0b2da99c4e73aeaf87d0e7f765ae0075",
"classList.html#aed93ef2f62f18549b6420d7e5bd607e4",
"classMediaIOCommand.html#a16255ac120516e70d467f2c2878071b9a00a9d2595d33f40e26f4ce5fbfc7add5",
"classMidiNote.html#ae35b95311738089287eaa0e86d959ac3a88c4637c3a3bdb5115c5b2f03e3f9c4c",
"classMutex_1_1Locker.html#a0e0da44ad37a318d983385d182c2bde4",
"classPixelDesc.html#ac8b8cdd7bab3e05c21bdb406cee6faafa497d60ff5f5060a57671d9faa5616eac",
"classPoint.html#a2229b3340efbf73fe60364d16040881f",
"classReadWriteLock.html#a82add2ee2df8dc92c0782a27c0d59da9",
"classSDLWindow.html#af2f211fd03c34b001dceff9a5eabad54",
"classString.html#a1106697fe39028af3f5e29f666ff06c9",
"classStringUnicodeData.html#a8ec90eb93c2ba81422194ba80e424fb7",
"classTimecode.html#aeb7f5adf0dbdae49cb38579763b980c2",
"classUMID.html#a8c24509889c956db736f7f049bba3c21",
"classWidget.html#a1c7f7287225de1fc1b284a98a0797f98",
"functions_func_c.html",
"matrix_8h_source.html",
"sdlapplication_8h_source.html",
"structMemSpace_1_1Stats_1_1Snapshot.html#a7598897ea6a37331ffcebede0bead2c0"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';