/**
 * @file      timecodeoverlaynode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/timecodeoverlaynode.h>
#include <promeki/medianodeconfig.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/paintengine.h>
#include <promeki/color.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/resource.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(TimecodeOverlayNode)

TimecodeOverlayNode::TimecodeOverlayNode(ObjectBase *parent) : MediaNode(parent), _fastFont(PaintEngine()) {
        setName("TimecodeOverlayNode");
        addSink(MediaSink::Ptr::create("input", ContentVideo));
        addSource(MediaSource::Ptr::create("output", ContentVideo));
}

bool TimecodeOverlayNode::parsePosition(const String &str, Position &out) {
        if(str == "topleft")        { out = TopLeft;      return true; }
        if(str == "topcenter")      { out = TopCenter;    return true; }
        if(str == "topright")       { out = TopRight;     return true; }
        if(str == "bottomleft")     { out = BottomLeft;   return true; }
        if(str == "bottomcenter")   { out = BottomCenter; return true; }
        if(str == "bottomright")    { out = BottomRight;  return true; }
        if(str == "custom")         { out = Custom;       return true; }
        return false;
}

MediaNodeConfig TimecodeOverlayNode::defaultConfig() const {
        MediaNodeConfig cfg("TimecodeOverlayNode", "");
        cfg.set("FontPath", String());
        cfg.set("FontSize", 36);
        cfg.set("Position", "bottomcenter");
        cfg.set("TextColor", Color::White);
        cfg.set("BackgroundColor", Color::Black);
        cfg.set("DrawBackground", true);
        return cfg;
}

BuildResult TimecodeOverlayNode::build(const MediaNodeConfig &config) {
        BuildResult result;
        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }

        // Read config. FontPath is optional — an empty or unset value
        // is passed through to FastFont, which falls back to the
        // library's bundled default font internally.
        _fontPath = FilePath(config.get("FontPath", String()).get<String>());
        _fontSize = config.get("FontSize", 36).get<int>();
        _drawBackground = config.get("DrawBackground", true).get<bool>();
        _customText = config.get("CustomText", String()).get<String>();
        _textColor = config.get("TextColor", Color::White).get<Color>();
        _bgColor = config.get("BackgroundColor", Color::Black).get<Color>();

        // Parse position
        String posStr = config.get("Position", "bottomcenter").get<String>();
        if(!posStr.isEmpty()) {
                if(!parsePosition(posStr, _position)) {
                        result.addError("Unknown position: " + posStr);
                        return result;
                }
        }
        if(_position == Custom) {
                _customX = config.get("CustomX", 0).get<int>();
                _customY = config.get("CustomY", 0).get<int>();
        }

        // Validate font path only when the caller supplied one.
        // An empty path is valid: FastFont falls back to the
        // library's bundled default font internally. When a path
        // is given we still check that it exists, and since resource
        // paths (":/...") never appear on the host filesystem we
        // have to consult the resource registry separately.
        if(!_fontPath.isEmpty()) {
                const String fontPathStr = _fontPath.toString();
                const bool isResource = Resource::isResourcePath(fontPathStr);
                const bool found = isResource
                        ? Resource::exists(fontPathStr)
                        : _fontPath.exists();
                if(!found) {
                        result.addError("Font file does not exist: " + fontPathStr);
                        return result;
                }
        }

        // Initialize FastFont (empty path is fine — FastFont uses the
        // library default).
        _fastFont.setFontFilename(_fontPath.toString());
        _fastFont.setFontSize(_fontSize);
        _fastFont.setForegroundColor(_textColor);
        _fastFont.setBackgroundColor(_bgColor);

        setState(Configured);
        return result;
}

void TimecodeOverlayNode::processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) {
        (void)inputIndex;
        (void)deliveries;

        if(frame->imageList().isEmpty()) {
                return;
        }

        // Get the image and ensure we have exclusive ownership for mutation
        Image::Ptr img = frame->imageList()[0];
        Image *imgMut = img.modify();       // COW detach Image if shared
        imgMut->ensureExclusive();           // COW detach plane buffers if shared

        // Set up paint engine on the image
        PaintEngine pe = imgMut->createPaintEngine();
        _fastFont.setPaintEngine(pe);

        // Read timecode from Image metadata
        String tcStr;
        if(img->metadata().contains(Metadata::Timecode)) {
                Timecode tc = img->metadata().get(Metadata::Timecode).get<Timecode>();
                tcStr = tc.toString().first();
        } else {
                tcStr = "--:--:--:--";
        }

        // Use actual font metrics for layout instead of _fontSize
        int fontAscender = _fastFont.ascender();
        int fontLineHeight = _fastFont.lineHeight();

        // Measure text to get accurate widths
        int tcWidth = _fastFont.measureText(tcStr);
        int customWidth = _customText.isEmpty() ? 0 : _fastFont.measureText(_customText);
        int maxTextWidth = tcWidth > customWidth ? tcWidth : customWidth;
        int lineSpacing = fontLineHeight / 4;
        int totalHeight = fontLineHeight + (_customText.isEmpty() ? 0 : lineSpacing + fontLineHeight);

        // Compute text position (x,y is the baseline of the first text line)
        int x = 0, y = 0;
        computePosition((int)img->width(), (int)img->height(), maxTextWidth, totalHeight, fontAscender, x, y);

        // Draw background rectangle for legibility (padding area around text)
        if(_drawBackground) {
                int pad = fontLineHeight / 4;
                PaintEngine::Pixel bgPixel = pe.createPixel(_bgColor);
                Rect<int32_t> bgRect(x - pad, y - fontAscender - pad,
                                     maxTextWidth + pad * 2, totalHeight + pad * 2);
                pe.fillRect(bgPixel, bgRect);
        }

        // Draw timecode text, centered within the block width
        int tcX = x + (maxTextWidth - tcWidth) / 2;
        _fastFont.drawText(tcStr, tcX, y);

        // Draw custom text below timecode if set, also centered
        if(!_customText.isEmpty()) {
                int customX = x + (maxTextWidth - customWidth) / 2;
                int customY = y + fontLineHeight + lineSpacing;
                _fastFont.drawText(_customText, customX, customY);
        }

        // Rebuild output frame with the modified image, preserving metadata
        Metadata md = frame->metadata();
        frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(img);
        frame.modify()->metadata() = md;
        return;
}

void TimecodeOverlayNode::computePosition(int frameWidth, int frameHeight, int textWidth, int totalHeight, int fontAscender, int &x, int &y) const {
        int margin = fontAscender / 2;
        switch(_position) {
                case TopLeft:
                        x = margin;
                        y = margin + fontAscender;
                        break;
                case TopCenter:
                        x = (frameWidth - textWidth) / 2;
                        y = margin + fontAscender;
                        break;
                case TopRight:
                        x = frameWidth - textWidth - margin;
                        y = margin + fontAscender;
                        break;
                case BottomLeft:
                        x = margin;
                        y = frameHeight - margin - totalHeight + fontAscender;
                        break;
                case BottomCenter:
                        x = (frameWidth - textWidth) / 2;
                        y = frameHeight - margin - totalHeight + fontAscender;
                        break;
                case BottomRight:
                        x = frameWidth - textWidth - margin;
                        y = frameHeight - margin - totalHeight + fontAscender;
                        break;
                case Custom:
                        x = _customX;
                        y = _customY;
                        break;
        }
        return;
}

PROMEKI_NAMESPACE_END
