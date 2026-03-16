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
      [ "Documentation (Doxygen)", "md_CODING__STANDARDS.html#autotoc_md62", [
        [ "Class Documentation", "md_CODING__STANDARDS.html#autotoc_md63", null ],
        [ "Method Documentation", "md_CODING__STANDARDS.html#autotoc_md64", null ],
        [ "Thread Safety Documentation", "md_CODING__STANDARDS.html#autotoc_md65", null ],
        [ "What Not to Document", "md_CODING__STANDARDS.html#autotoc_md66", null ]
      ] ],
      [ "Testing", "md_CODING__STANDARDS.html#autotoc_md68", [
        [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md69", null ],
        [ "Structure", "md_CODING__STANDARDS.html#autotoc_md70", null ],
        [ "Assertions", "md_CODING__STANDARDS.html#autotoc_md71", null ],
        [ "Build Integration", "md_CODING__STANDARDS.html#autotoc_md72", null ]
      ] ],
      [ "Miscellaneous", "md_CODING__STANDARDS.html#autotoc_md74", null ]
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
        [ "Enumerator", "functions_eval.html", null ],
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
"algorithm_8h.html",
"classAudioFile.html#a4e2d052253dd3ed09a44127ee8ce9153ad304585fb726c590f00dc69879764d97",
"classDataStream.html#a781a61c06aaf1ba83ae8f3ab5aadd369a5aa71e8e56902d0124304063085a64b4",
"classFile.html#a042f525d4beac394675727bcd5cce274",
"classHashSet.html#add11852d1d0ef359a41abe3f9340d94f",
"classList.html#a409ddf197be7e27b12f09c528519d12c",
"classMidiNote.html#a4f5ae63f34e2a142960620f7c9e49c7e",
"classMidiNote.html#ae35b95311738089287eaa0e86d959ac3afbbfb54fadb9d0d0b371821dfeae9d63",
"classPixelFormat.html#a7ae7d8a6cf46a5c9947b72d5ec9d4ded",
"classReadWriteLock.html#a457c5720f5ed9d8632bba2822d7539e4",
"classString.html#a35478c00c321dacf5124d44389ddfd9f",
"classStringUnicodeLiteralData.html#ac561decefb7af22701549bf0bf002bf1",
"classTuiFrame.html#ae09daea690bc4abd73c781f4872ca59b",
"classVariantImpl.html#a95ca32106dbaee20a12d1a3a8c9e48fe",
"image_8h_source.html",
"streamstring_8h_source.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';