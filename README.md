# libpromeki

PROfessional MEdia toolKIt - A C++ library targeted towards professional audio / video media needs.

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

## Debugging
If you build the library with `PROMEKI_DEBUG_ENABLE` defined, this will enable the `promekiDebug()` function
to write debug information to the log.  Each source file that wants to use `promekiDebug()` must also call
`PROMEKI_DEBUG(<DEBUG_NAME>)` at the top of the source file.  This creates a debug channel by this name and
assigns this source scope to this debug channel.  You can then use the `PROMEKI_DEBUG` environment variable
to enable debug output of certain debug channels.  You can provide a comma separated list for debugging
mutiple channels at once.  So, for example:

In your source file:
```
PROMEKI_DEBUG(MyChannel)

// elsewhere in your code
promekiDebug("This is a debug log entry")
```

And then when running your application:
```
export PROMEKI_DEBUG=MyChannel
./yourapp
```

And the promeki log output will now have all debug messages from the MyChannel debug channel.

