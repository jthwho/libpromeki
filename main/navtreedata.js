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
      [ "Shareable", "dataobjects.html#shareable", null ]
    ] ],
    [ "Demonstration Applications", "demos.html", [
      [ "Building", "demos.html#demos_building", null ],
      [ "Standalone Build", "demos.html#demos_standalone", null ],
      [ "Adding a New Demo", "demos.html#demos_adding", null ],
      [ "Current Demos", "demos.html#demos_list", null ]
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
        [ "Choosing the Right Category", "md_CODING__STANDARDS.html#autotoc_md19", null ]
      ] ],
      [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md21", [
        [ "Header Files", "md_CODING__STANDARDS.html#autotoc_md22", null ],
        [ "File Header Comment", "md_CODING__STANDARDS.html#autotoc_md23", null ],
        [ "Include Guards", "md_CODING__STANDARDS.html#autotoc_md24", null ],
        [ "Includes", "md_CODING__STANDARDS.html#autotoc_md25", null ]
      ] ],
      [ "Naming Conventions", "md_CODING__STANDARDS.html#autotoc_md27", [
        [ "Classes and Types", "md_CODING__STANDARDS.html#autotoc_md28", null ],
        [ "Convenience Type Aliases for Templates", "md_CODING__STANDARDS.html#autotoc_md29", null ],
        [ "Methods", "md_CODING__STANDARDS.html#autotoc_md30", null ],
        [ "Member Variables", "md_CODING__STANDARDS.html#autotoc_md31", null ],
        [ "Enums", "md_CODING__STANDARDS.html#autotoc_md32", null ],
        [ "Standard Library Type Aliases", "md_CODING__STANDARDS.html#autotoc_md33", null ],
        [ "Macros", "md_CODING__STANDARDS.html#autotoc_md34", null ],
        [ "Constants", "md_CODING__STANDARDS.html#autotoc_md35", null ]
      ] ],
      [ "Indentation and Formatting", "md_CODING__STANDARDS.html#autotoc_md37", null ],
      [ "The SharedPtr / Copy-on-Write Pattern", "md_CODING__STANDARDS.html#autotoc_md39", [
        [ "Shareable Data Objects Support SharedPtr", "md_CODING__STANDARDS.html#autotoc_md40", null ],
        [ "No Internal SharedPtr<Data>", "md_CODING__STANDARDS.html#autotoc_md41", null ],
        [ "How to Implement (Shareable Classes)", "md_CODING__STANDARDS.html#autotoc_md42", null ]
      ] ],
      [ "Error Handling", "md_CODING__STANDARDS.html#autotoc_md44", [
        [ "The Error Class", "md_CODING__STANDARDS.html#autotoc_md45", null ],
        [ "Return Patterns", "md_CODING__STANDARDS.html#autotoc_md46", null ],
        [ "Avoid <tt>bool</tt> for Error Reporting", "md_CODING__STANDARDS.html#autotoc_md47", null ]
      ] ],
      [ "Container and Wrapper Types", "md_CODING__STANDARDS.html#autotoc_md49", [
        [ "String", "md_CODING__STANDARDS.html#autotoc_md50", null ],
        [ "List<T>", "md_CODING__STANDARDS.html#autotoc_md51", null ],
        [ "Array<T, N>", "md_CODING__STANDARDS.html#autotoc_md52", null ],
        [ "Buffer", "md_CODING__STANDARDS.html#autotoc_md53", null ]
      ] ],
      [ "ObjectBase and Signals/Slots", "md_CODING__STANDARDS.html#autotoc_md55", null ],
      [ "Namespace", "md_CODING__STANDARDS.html#autotoc_md57", null ],
      [ "Documentation (Doxygen)", "md_CODING__STANDARDS.html#autotoc_md59", [
        [ "Class Documentation", "md_CODING__STANDARDS.html#autotoc_md60", null ],
        [ "Method Documentation", "md_CODING__STANDARDS.html#autotoc_md61", null ],
        [ "What Not to Document", "md_CODING__STANDARDS.html#autotoc_md62", null ]
      ] ],
      [ "Testing", "md_CODING__STANDARDS.html#autotoc_md64", [
        [ "File Layout", "md_CODING__STANDARDS.html#autotoc_md65", null ],
        [ "Structure", "md_CODING__STANDARDS.html#autotoc_md66", null ],
        [ "Assertions", "md_CODING__STANDARDS.html#autotoc_md67", null ],
        [ "Build Integration", "md_CODING__STANDARDS.html#autotoc_md68", null ]
      ] ],
      [ "Miscellaneous", "md_CODING__STANDARDS.html#autotoc_md70", null ]
    ] ],
    [ "Signal List", "signal.html", null ],
    [ "Deprecated List", "deprecated.html", null ],
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
"annotated.html",
"classAudioFile_1_1Impl.html#a82a1d7bd0ab27275b399ea5c73396656",
"classEvent.html#a125c246d8ea0315dcf6d8e56cd414c1c",
"classJsonObject.html#aa41d053fc27e4fcf4f1c6f55ef28f4a9",
"classMatrix3x3.html",
"classMidiNote.html#ae35b95311738089287eaa0e86d959ac3ab2f4960fe742b0f9499b84d09fbc5b54",
"classPaintEngine.html#a8a5c11dd17dd3d3d1ef10a087dad5d9e",
"classRefCount.html#a871f21de0112bc4ca80b47d81e1be6e4",
"classStringLatin1Data.html#a2d3e8ed9ce0042d3198489e71004a1f4",
"classTuiApplication.html",
"classTuiWidget.html#a9d728fc9be701fcb79199b7875571a41",
"index.html#autotoc_md8",
"structColorSpace_1_1Data.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';