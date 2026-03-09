/**
 * @file      imagefileio.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

#define PROMEKI_REGISTER_IMAGEFILEIO(name) [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_imagefileio_, PROMEKI_UNIQUE_ID) = \
        ImageFileIO::registerImageFileIO(new name);


PROMEKI_NAMESPACE_BEGIN

class Error;
class ImageFile;

class ImageFileIO {
        public:
                static int registerImageFileIO(ImageFileIO *object);
                static const ImageFileIO *lookup(int id);

                ImageFileIO() = default;
                virtual ~ImageFileIO() {};

                int id() const {
                        return _id;
                }

                bool isValid() const {
                        return _id != 0;
                }

                bool canLoad() const {
                        return _canLoad;
                }

                bool canSave() const {
                        return _canSave;
                }

                String name() const {
                        return _name;
                }

                virtual Error load(ImageFile &imageFile) const;
                virtual Error save(ImageFile &imageFile) const;

        protected:
                int             _id = 0;
                bool            _canLoad = false;
                bool            _canSave = false;
                String          _name;

};

PROMEKI_NAMESPACE_END

