# libpromeki
<img src="./docs/promeki_logo.jpg" alt="ProMEKI Logo" width="100" height="100">

PROfessional MEdia toolKIt - A C++ library targeted towards professional audio / video media needs.

## History
The bulk of the code that makes up libpromeki started out life as code that existed across multiple libraries originally developed
by my (now defunct) company, [SpectSoft](https://en.wikipedia.org/wiki/SpectSoft), for our video disk recorder product 
called RaveHD. During its run, it was used in post production, vfx, animation, and other 
media related sectors. At that time, the entire application stack was built on top of the [Qt](https://www.qt.io/)
framework.  The Qt design patterns, aesthetic, and general way of doing things has certainly informed
much of how this library was architected.

What you see now is a library that takes the good bits from that old code base and 
has removed the Qt coupling and replaced it with modern C++ native STL.  Hopefully you'll find it as useful
as I have over the years.  --jth

## Dependencies
1. C++20
1. C++ filesystem library

1. (optional) libpng - Used to read/write PNG files  
  http://www.libpng.org/pub/png/libpng.html

1. (optional) libsndfile - Used to read/write wav, aiff, ogg soundfiles.  
  http://www.mega-nerd.com/libsndfile/

1. (optional) libfreetype - Used to draw fonts on image surfaces.  
  https://freetype.org/

## Building
The library uses cmake for building, so building for your platform should be as easy as
executing the following from within the root of the source tree:
1. `mkdir build`
1. `cd build`
1. `cmake -DCMAKE_BUILD_TYPE=Release ..`
1. And then running the generated build system (e.g. make, ninja, xcode, visual studio, etc)

## Use Tips
Here's some tips on how to best utilize the library in your application:
1. Add the /path/to/promeki_codebase/include to your compiler's include path
1. Use the `#include <promeki/header.h>` method to include any promeki files
1. Everything in promeki is in the `promeki` namespace.  Often you can get away with using the entire namespace with:
```c++
using namespace promeki;
```

## Documentation
You can build the API documentation by using the `docs` target in your cmake generated build.  So, for example, if you've
generated a make based build system, you should be able to type `make docs` and doxygen will be run to generate the docs.
NOTE: This target will only exist if you've got doxygen installed (and cmake can find it).

Additionally, you can find a copy of the documentation here:
[API Documentation](https://howardlogic.com/libpromeki/api/)

## Debugging
If you build the library with `PROMEKI_DEBUG_ENABLE` defined, this will enable the `promekiDebug()` function
to write debug information to the log.  Each source file that wants to use `promekiDebug()` must also call
`PROMEKI_DEBUG(<DEBUG_NAME>)` at the top of the source file.  This creates a debug channel by this name and
assigns this source scope to this debug channel.  You can then use the `PROMEKI_DEBUG` environment variable
to enable debug output of certain debug channels.  You can provide a comma separated list for debugging
multiple channels at once.  So, for example:

In your source file:
```c++
PROMEKI_DEBUG(MyChannel)

// elsewhere in your code
promekiDebug("This is a debug log entry")
```

And then when running your application:
```sh
export PROMEKI_DEBUG=MyChannel
./yourapp
```

And the promeki log output will now have all debug messages from the MyChannel debug channel.

