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
      [ "ObjectBase and Signals/Slots", "md_CODING__STANDARDS.html#autotoc_md58", null ],
      [ "Namespace", "md_CODING__STANDARDS.html#autotoc_md60", null ],
      [ "Stream Operator Support (TextStream / DataStream)", "md_CODING__STANDARDS.html#autotoc_md62", [
        [ "TextStream", "md_CODING__STANDARDS.html#autotoc_md63", null ],
        [ "DataStream", "md_CODING__STANDARDS.html#autotoc_md64", null ],
        [ "When to Omit", "md_CODING__STANDARDS.html#autotoc_md65", null ]
      ] ],
      [ "Documentation (Doxygen)", "md_CODING__STANDARDS.html#autotoc_md67", [
        [ "Class Documentation", "md_CODING__STANDARDS.html#autotoc_md68", null ],
        [ "Method Documentation", "md_CODING__STANDARDS.html#autotoc_md69", null ],
        [ "Thread Safety Documentation", "md_CODING__STANDARDS.html#autotoc_md70", null ],
        [ "What Not to Document", "md_CODING__STANDARDS.html#autotoc_md71", null ]
      ] ],
      [ "Testing", "md_CODING__STANDARDS.html#autotoc_md73", [
        [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md74", null ],
        [ "Structure", "md_CODING__STANDARDS.html#autotoc_md75", null ],
        [ "Assertions", "md_CODING__STANDARDS.html#autotoc_md76", null ],
        [ "Build Integration", "md_CODING__STANDARDS.html#autotoc_md77", null ]
      ] ],
      [ "Miscellaneous", "md_CODING__STANDARDS.html#autotoc_md79", null ]
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
        [ "Variables", "functions_vars.html", null ],
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
        [ "Variables", "globals_vars.html", null ],
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
"classAnsiStream.html#a338b08f6aae49641c1f62523260304dca84aa28ec9a3a754e94020ed72d80080d",
"classAudioBlock.html#a72f11a9fce8cb8e7c13e15744e6d5dfc",
"classCmdLineParser_1_1Option.html#af36860253d6946da6c3a49af658f9d41",
"classEncodedDesc.html#a3d11f0db93f503a293908d76ba184eca",
"classFrameRate.html",
"classIpv4Address.html#ad0cea1e85bf178d3e8f3405d8e72a308",
"classLogger.html#ab8bdb095bc42de4845060b26282f2714",
"classMediaPipeline.html#a48591532723de50ab0988a6e59317f33",
"classMidiNote.html#ae35b95311738089287eaa0e86d959ac3a82915af763c85459d5a3264ad9acc515",
"classNetworkAddress.html#ad454c715759d0275a4a99de4b0958d06",
"classPixelFormat.html#aa47cca27864c57b6e7b820564294390ba063c33785fb635fdf722480186a82cf3",
"classRefCount.html#a871f21de0112bc4ca80b47d81e1be6e4",
"classSize2DTemplate.html#a1258357cb4c5d46ffdb5217e9393250f",
"classStringData.html#adf5b573840a1912309d2e692e365cc34",
"classTestPatternNode.html#acadbee74845f882d210f9b2fc84b3273",
"classTuiButton.html#a53de611e4078be71d22106e902674788",
"classUUID.html#a3629d821d3ee36fbae26548e153061a7",
"functions_func_~.html",
"musicalnote_8h_source.html",
"structPixelFormat_1_1Data.html#a8d47fc3ebc2dc41e67b25386ff0b4e70"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';