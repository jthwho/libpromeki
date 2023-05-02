/*****************************************************************************
 * png.cpp
 * April 30, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <png.h>
#include <csetjmp>
#include <cstdio>
#include <promeki/image.h>
#include <promeki/logger.h>
#include <promeki/string.h>
#include <promeki/error.h>

namespace promeki {

void pngError(png_structp png, png_const_charp msg) {
        promekiErr("libpng: %s", msg);
        longjmp(png_jmpbuf(png), 1);
        return;
}

void pngWarning(png_structp png, png_const_charp msg) {
        promekiWarn("libpng: %s", msg);
        return;
}

Error imageFileSavePNG(const String &filename, const Image &image) {
	FILE 			*fp;
	png_structp 		pngp;
	png_infop 		infop;
	int 			color, depth, rowb, i;

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
	switch(image.pixelFormat().id()) {
                case PixelFormat::RGBA8:
			color = PNG_COLOR_TYPE_RGB_ALPHA;
			depth = 8;
			break;

		default:
			promekiErr("Write '%s' failed: Pixel format '%s' not supported",
                                filename.cstr(), image.pixelFormat().name().cstr());
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
        size_t stride = image.stride();
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

} // namespace promeki

