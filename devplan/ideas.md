# Random Ideas That Need Exploration

1. All MediaIO should take a "Name" config option.  This name will be used for logging information
   and, in the case the MediaIOTask starts threads (not part of the MediaIO thread pool) it can name
   the threads with this name.  The mediaplay application, and eventual MediaPipeline, should give
   these short default names. (e.g. out1, in1, in2).

2. Put the crash log code in the library.  We're currently using it in the unit tests.
