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
"classEventLoop.html#a0b602a0f8f64b33f9adcdcfd119ea0b0",
"classJsonObject.html#abbaa04357a55dc1811a2ed0917b66894",
"classMatrix3x3.html#a239479cc4f0705f48b85bc2b5245c9ad",
"classMidiNote.html#ae35b95311738089287eaa0e86d959ac3abaaa9defb15f703c4bee383f04e1deff",
"classPaintEngine.html#ae28d83d1366150a6a7b6df117b509950",
"classRefCount.html#af1aa42505738cd8f8dc2ca1ec2674c97",
"classStringLatin1Data.html#a6982b54bba5f54c781ec367521b98538",
"classTuiApplication.html#aad7d357e349c28a6da643502f4bc5dc1",
"classVariantImpl.html#ab2d523ea5ddb45e27665551747d0c1bea0137f0321debba0ba63491187a5c6940",
"md_CODING__STANDARDS.html#autotoc_md25",
"structPixelFormat_1_1PlaneDesc.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';