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
 * @par Config options
 * - `FontPath` (String): Path to a TrueType font file (required).
 * - `FontSize` (int): Font size in points (default: 36).
 * - `Position` (String): Position preset (default: "bottomcenter").
 *   Values: topleft, topcenter, topright, bottomleft, bottomcenter, bottomright.
 * - `CustomX` (int): Custom X position (only used with position "custom").
 * - `CustomY` (int): Custom Y position (only used with position "custom").
 * - `TextColorR` (uint16_t): Red component 0-65535 (default: 65535).
 * - `TextColorG` (uint16_t): Green component 0-65535 (default: 65535).
 * - `TextColorB` (uint16_t): Blue component 0-65535 (default: 65535).
 * - `DrawBackground` (bool): Draw dark background behind text (default: true).
 * - `CustomText` (String): Additional text to render below timecode.
 *
 * @par Example
 * @code
 * MediaNodeConfig cfg("TimecodeOverlayNode", "overlay");
 * cfg.set("FontPath", "/path/to/font.ttf");
 * cfg.set("FontSize", 48);
 * cfg.set("Position", "bottomcenter");
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

                MediaNodeConfig defaultConfig() const override;
                BuildResult build(const MediaNodeConfig &config) override;

        protected:
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override;

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

                static bool parsePosition(const String &str, Position &out);

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
