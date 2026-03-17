/**
 * @file      proav/timecodeoverlaynode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/filepath.h>
#include <promeki/proav/medianode.h>
#include <promeki/proav/fontpainter.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Burns timecode text into video frames using FontPainter.
 * @ingroup proav_pipeline
 *
 * Processing node with one Image input and one Image output. Reads
 * timecode from the Image's own Metadata and renders it as text onto
 * the image buffer. Optionally renders additional custom text.
 *
 * Requires a TrueType font file to be set via setFontPath() before
 * configure() is called.
 *
 * @par Example
 * @code
 * TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
 * overlay->setFontPath("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
 * overlay->setFontSize(48);
 * overlay->setPosition(TimecodeOverlayNode::BottomCenter);
 * @endcode
 */
class TimecodeOverlayNode : public MediaNode {
        PROMEKI_OBJECT(TimecodeOverlayNode, MediaNode)
        public:
                /** @brief Named position presets for text placement. */
                enum Position {
                        TopLeft,        ///< @brief Top-left corner.
                        TopCenter,      ///< @brief Top center.
                        TopRight,       ///< @brief Top-right corner.
                        BottomLeft,     ///< @brief Bottom-left corner.
                        BottomCenter,   ///< @brief Bottom center.
                        BottomRight,    ///< @brief Bottom-right corner.
                        Custom          ///< @brief Custom x/y coordinates.
                };

                /**
                 * @brief Constructs a TimecodeOverlayNode.
                 * @param parent Optional parent object.
                 */
                TimecodeOverlayNode(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                virtual ~TimecodeOverlayNode() = default;

                /**
                 * @brief Sets the path to the TrueType font file.
                 * @param path Path to a .ttf font file (required).
                 */
                void setFontPath(const FilePath &path) { _fontPath = path; return; }

                /** @brief Returns the font path. */
                const FilePath &fontPath() const { return _fontPath; }

                /**
                 * @brief Sets the font size in points.
                 * @param points Font size (default: 36).
                 */
                void setFontSize(int points) { _fontSize = points; return; }

                /** @brief Returns the font size in points. */
                int fontSize() const { return _fontSize; }

                /**
                 * @brief Sets the text position using a named preset.
                 *
                 * The actual x/y coordinates are computed during process()
                 * based on the frame dimensions and font size.
                 *
                 * @param pos The position preset.
                 */
                void setPosition(Position pos) { _position = pos; return; }

                /**
                 * @brief Sets a custom text position.
                 *
                 * Switches the position mode to Custom.
                 *
                 * @param x X coordinate of the text origin.
                 * @param y Y coordinate of the text origin.
                 */
                void setPosition(int x, int y) {
                        _position = Custom;
                        _customX = x;
                        _customY = y;
                        return;
                }

                /** @brief Returns the current position preset. */
                Position position() const { return _position; }

                /**
                 * @brief Sets the text color.
                 * @param r Red component (0-65535).
                 * @param g Green component (0-65535).
                 * @param b Blue component (0-65535).
                 */
                void setTextColor(uint16_t r, uint16_t g, uint16_t b) {
                        _colorR = r; _colorG = g; _colorB = b;
                        return;
                }

                /**
                 * @brief Enables or disables drawing a dark background behind the text.
                 * @param enable true to draw a background rectangle for legibility.
                 */
                void setDrawBackground(bool enable) { _drawBackground = enable; return; }

                /** @brief Returns true if background drawing is enabled. */
                bool drawBackground() const { return _drawBackground; }

                /**
                 * @brief Sets additional custom text to render below the timecode.
                 * @param text The custom text string (e.g., "TEST SIGNAL").
                 */
                void setCustomText(const String &text) { _customText = text; return; }

                /** @brief Returns the custom text. */
                const String &customText() const { return _customText; }

                // ---- Lifecycle overrides ----

                /**
                 * @brief Validates the font path and initializes the FontPainter.
                 *
                 * Transitions to Configured on success. Fails if the font path
                 * is not set or does not exist on disk.
                 *
                 * @return Error::Ok on success, or Error::Invalid.
                 */
                Error configure() override;

                /**
                 * @brief Reads timecode from the input image's metadata and renders it as text.
                 *
                 * Dequeues a Frame from the input port, ensures exclusive buffer
                 * ownership via copy-on-write, renders the timecode (and optional
                 * custom text), then delivers the modified Frame to the output port.
                 */
                void process() override;

        private:
                FilePath        _fontPath;
                int             _fontSize = 36;
                Position        _position = BottomCenter;
                int             _customX = 0;
                int             _customY = 0;
                uint16_t        _colorR = 65535;
                uint16_t        _colorG = 65535;
                uint16_t        _colorB = 65535;
                bool            _drawBackground = true;
                String          _customText;

                FontPainter     _fontPainter;

                /**
                 * @brief Computes text placement coordinates from the current position preset.
                 * @param frameWidth  Width of the frame in pixels.
                 * @param frameHeight Height of the frame in pixels.
                 * @param textWidth   Width of the widest text line in pixels.
                 * @param totalHeight Total height of the text block in pixels.
                 * @param[out] x      Computed x origin.
                 * @param[out] y      Computed y origin (baseline of first line).
                 */
                void computePosition(int frameWidth, int frameHeight, int textWidth, int totalHeight, int &x, int &y) const;
};

PROMEKI_NAMESPACE_END
