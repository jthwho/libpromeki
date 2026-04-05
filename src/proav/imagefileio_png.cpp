/**
 * @file      imagefileio_png.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <png.h>
#include <csetjmp>
#include <cstdio>
#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>

PROMEKI_NAMESPACE_BEGIN

static void pngError(png_structp png, png_const_charp msg) {
        promekiErr("libpng: %s", msg);
        longjmp(png_jmpbuf(png), 1);
        return;
}

static void pngWarning(png_structp png, png_const_charp msg) {
        promekiWarn("libpng: %s", msg);
        return;
}

class ImageFileIO_PNG : public ImageFileIO {
        public:
                ImageFileIO_PNG() {
                        _id = ImageFile::PNG;
                        _canLoad = false;
                        _canSave = true;
                        _name = "PNG";
                }
                
                //Error load(ImageFile &imageFile) const override;
                Error save(ImageFile &imageFile) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_PNG);

Error ImageFileIO_PNG::save(ImageFile &imageFile) const {
	FILE 			*fp;
	png_structp 		pngp;
	png_infop 		infop;
	int 			color, depth, rowb, i;
        const Image             &image = imageFile.image();
        const String            &filename = imageFile.filename();

	fp = std::fopen(filename.cstr(), "w");
	if(!fp){
                Error ret = Error::syserr();
		promekiErr("Unable to open file %s: %s", filename.cstr(), ret.name().cstr());
		return ret;
	}
	
	pngp = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if(!pngp) {
		promekiErr("Unable to create png struct");
                std::fclose(fp);
		return Error::LibraryFailure;
	}
        png_set_error_fn(pngp, NULL, pngError, pngWarning);

	infop = png_create_info_struct(pngp);
	if(!infop) {
                promekiErr("Unable to create png info struct");
                std::fclose(fp);
		return Error::LibraryFailure;
	}
	
	if(setjmp(png_jmpbuf(pngp))) {
                promekiErr("stdjmp failed");
		png_destroy_write_struct(&pngp, &infop);
                std::fclose(fp);
		return Error::LibraryFailure;
	}
	
	png_init_io(pngp, fp);
	switch(image.pixelDesc().id()) {
                case PixelDesc::RGBA8_sRGB:
			color = PNG_COLOR_TYPE_RGB_ALPHA;
			depth = 8;
			break;

		default:
			promekiErr("Write '%s' failed: Pixel description '%s' not supported",
                                filename.cstr(), image.pixelDesc().name().cstr());
			png_destroy_write_struct(&pngp, &infop);
                        std::fclose(fp);
			return Error::PixelFormatNotSupported;
	}

        // If the image is interlaced, use ADAM7 encoding
        int interlaced = image.desc().interlaced() ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE; 
	png_set_IHDR(pngp, infop, image.width(), image.height(), depth, color,
                interlaced, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	
	//if(frame->getProps().exists(Meki_Gamma))
	//	png_set_gAMA(pngp, infop, frame->getProp(Meki_Gamma).toDouble());
	
	png_write_info(pngp, infop);
	
	/* Organize the data in our image into pointers to rows. */
        std::vector<png_bytep> lines(image.height());
        size_t stride = image.lineStride();
        png_bytep buf = static_cast<png_bytep>(image.data());
	for(i = 0; i < image.height(); i++) {
		lines[i] = buf;
                buf += stride;
	}
	png_write_image(pngp, lines.data());
	png_write_end(pngp, NULL);
	png_destroy_write_struct(&pngp, &infop);
        std::fclose(fp);
	return Error::Ok;
}

PROMEKI_NAMESPACE_END

