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

