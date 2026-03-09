/**
 * @file      imagefile.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/image.h>

PROMEKI_NAMESPACE_BEGIN

class ImageFileIO;

class ImageFile {
        public:
                enum ID {
                        Invalid = 0,
                        PNG
                };

                ImageFile(int id = 0);

                const String &filename() const {
                        return _filename;
                }

                void setFilename(const String &val) {
                        _filename = val;
                        return;
                }

                const Image &image() const {
                        return _image;
                }

                void setImage(const Image &val) {
                        _image = val;
                        return;
                }

                Error load();
                Error save();

        private:
                String                  _filename;
                Image                   _image;
                const ImageFileIO       *_io;
};

PROMEKI_NAMESPACE_END


