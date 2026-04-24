/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstdlib>
#include <promeki/pixelformat.h>
#include <promeki/imagedesc.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/imagefile.h>
#include <promeki/videotestpattern.h>
#include <promeki/color.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/metadata.h>

using namespace promeki;

static void usage(const char *argv0) {
        std::fprintf(stderr,
                "Usage: %s <output-folder> [width] [height]\n"
                "\n"
                "Generates a colorbars test image for every uncompressed pixel\n"
                "format that has a registered PaintEngine, burns the format name\n"
                "into the center, converts to RGBA8 for PNG output, and writes\n"
                "each result to <output-folder>/<PixelFormatName>.png.\n"
                "\n"
                "  width   Image width  (default: 1920)\n"
                "  height  Image height (default: 1080)\n",
                argv0);
}

int main(int argc, char **argv) {
        if(argc < 2) {
                usage(argv[0]);
                return 1;
        }

        String outDir(argv[1]);
        int width  = (argc > 2) ? std::atoi(argv[2]) : 1920;
        int height = (argc > 3) ? std::atoi(argv[3]) : 1080;

        if(width <= 0 || height <= 0) {
                std::fprintf(stderr, "Invalid dimensions: %dx%d\n", width, height);
                return 1;
        }

        PixelFormat::IDList ids = PixelFormat::registeredIDs();

        VideoTestPattern gen;
        gen.setPattern(VideoPattern::ColorBars);
        gen.setBurnEnabled(true);
        gen.setBurnPosition(BurnPosition::Center);
        gen.setBurnTextColor(Color::White);
        gen.setBurnBackgroundColor(Color::Black);
        gen.setBurnDrawBackground(true);

        int attempted = 0;
        int succeeded = 0;
        int skipped   = 0;
        int failed    = 0;

        for(PixelFormat::ID id : ids) {
                PixelFormat pd(id);
                if(!pd.isValid()) continue;
                if(pd.isCompressed()) continue;
                if(!pd.hasPaintEngine()) {
                        skipped++;
                        continue;
                }

                attempted++;
                const String &name = pd.name();

                ImageDesc desc(Size2Du32(width, height), pd);
                auto img = gen.createPayload(desc);
                if(!img.isValid()) {
                        std::fprintf(stderr, "FAIL  %-40s  create() failed\n", name.cstr());
                        failed++;
                        continue;
                }

                auto pngImg = img;
                if(pd.id() != PixelFormat::RGBA8_sRGB) {
                        pngImg = img->convert(PixelFormat(PixelFormat::RGBA8_sRGB), Metadata());
                        if(!pngImg.isValid()) {
                                std::fprintf(stderr, "FAIL  %-40s  convert to RGBA8 failed\n",
                                             name.cstr());
                                failed++;
                                continue;
                        }
                }

                String path = outDir + "/" + name + ".png";
                ImageFile f(ImageFile::PNG);
                f.setFilename(path);
                f.setVideoPayload(pngImg);
                Error saveErr = f.save();
                if(saveErr.isError()) {
                        std::fprintf(stderr, "FAIL  %-40s  save: %s\n",
                                     name.cstr(), saveErr.name().cstr());
                        failed++;
                        continue;
                }

                std::printf("OK    %-40s  %s\n", name.cstr(), path.cstr());
                succeeded++;
        }

        std::printf("\n");
        std::printf("Attempted: %d  Succeeded: %d  Failed: %d  Skipped (no engine): %d\n",
                    attempted, succeeded, failed, skipped);
        std::printf("RESULT: %s\n", (failed == 0 && succeeded > 0) ? "PASS" : "FAIL");
        return (failed == 0 && succeeded > 0) ? 0 : 1;
}
