/**
 * @file      timecodeoverlaynode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/timecodeoverlaynode.h>
#include <promeki/proav/medianodeconfig.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#include <promeki/proav/paintengine.h>
#include <promeki/core/metadata.h>
#include <promeki/core/timecode.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(TimecodeOverlayNode)

TimecodeOverlayNode::TimecodeOverlayNode(ObjectBase *parent) : MediaNode(parent) {
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

BuildResult TimecodeOverlayNode::build(const MediaNodeConfig &config) {
        BuildResult result;
        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }

        // Read config
        _fontPath = FilePath(config.get("fontPath", Variant(String())).get<String>());
        _fontSize = config.get("fontSize", Variant(36)).get<int>();
        _drawBackground = config.get("drawBackground", Variant(true)).get<bool>();
        _customText = config.get("customText", Variant(String())).get<String>();
        _colorR = config.get("textColorR", Variant(uint16_t(65535))).get<uint16_t>();
        _colorG = config.get("textColorG", Variant(uint16_t(65535))).get<uint16_t>();
        _colorB = config.get("textColorB", Variant(uint16_t(65535))).get<uint16_t>();

        // Parse position
        String posStr = config.get("position", Variant(String("bottomcenter"))).get<String>();
        if(!posStr.isEmpty()) {
                if(!parsePosition(posStr, _position)) {
                        result.addError("Unknown position: " + posStr);
                        return result;
                }
        }
        if(_position == Custom) {
                _customX = config.get("customX", Variant(0)).get<int>();
                _customY = config.get("customY", Variant(0)).get<int>();
        }

        // Validate font path
        if(_fontPath.isEmpty()) {
                result.addError("Font path is not set");
                return result;
        }
        if(!_fontPath.exists()) {
                result.addError("Font file does not exist: " + _fontPath.toString());
                return result;
        }

        // Initialize FontPainter
        _fontPainter.setFontFilename(_fontPath.toString());

        setState(Configured);
        return result;
}

void TimecodeOverlayNode::process() {
        Frame::Ptr frame = dequeueInput();
        if(!frame.isValid()) return;

        if(frame->imageList().isEmpty()) {
                deliverOutput(frame);
                return;
        }

        // Get the image and ensure we have exclusive ownership for mutation
        Image::Ptr img = frame->imageList()[0];
        Image *imgMut = img.modify();       // COW detach Image if shared
        imgMut->ensureExclusive();           // COW detach plane buffers if shared

        // Set up paint engine on the image
        PaintEngine pe = imgMut->createPaintEngine();
        _fontPainter.setPaintEngine(pe);

        // Read timecode from Image metadata
        String tcStr;
        if(img->metadata().contains(Metadata::Timecode)) {
                Timecode tc = img->metadata().get(Metadata::Timecode).get<Timecode>();
                tcStr = tc.toString().first;
        } else {
                tcStr = "--:--:--:--";
        }

        // Measure text to get accurate widths
        int tcWidth = _fontPainter.measureText(tcStr, _fontSize);
        int customWidth = _customText.isEmpty() ? 0 : _fontPainter.measureText(_customText, _fontSize);
        int maxTextWidth = tcWidth > customWidth ? tcWidth : customWidth;
        int lineSpacing = _fontSize / 4;
        int totalHeight = _fontSize + (_customText.isEmpty() ? 0 : lineSpacing + _fontSize);

        // Compute text position (x,y is the top-left of the text block)
        int x = 0, y = 0;
        computePosition((int)img->width(), (int)img->height(), maxTextWidth, totalHeight, x, y);

        // Draw background rectangle for legibility
        if(_drawBackground) {
                int pad = _fontSize / 4;
                PaintEngine::Pixel bgPixel = pe.createPixel(0, 0, 0);
                Rect<int32_t> bgRect(x - pad, y - _fontSize - pad,
                                     maxTextWidth + pad * 2, totalHeight + pad * 2);
                pe.fillRect(bgPixel, bgRect);
        }

        // Draw timecode text, centered within the block width
        int tcX = x + (maxTextWidth - tcWidth) / 2;
        _fontPainter.drawText(tcStr, tcX, y, _fontSize);

        // Draw custom text below timecode if set, also centered
        if(!_customText.isEmpty()) {
                int customX = x + (maxTextWidth - customWidth) / 2;
                int customY = y + _fontSize + lineSpacing;
                _fontPainter.drawText(_customText, customX, customY, _fontSize);
        }

        // Rebuild output frame with the modified image
        Frame::Ptr outFrame = Frame::Ptr::create();
        outFrame.modify()->imageList().pushToBack(img);
        outFrame.modify()->metadata() = frame->metadata();
        deliverOutput(outFrame);
        return;
}

void TimecodeOverlayNode::computePosition(int frameWidth, int frameHeight, int textWidth, int totalHeight, int &x, int &y) const {
        int margin = _fontSize / 2;
        switch(_position) {
                case TopLeft:
                        x = margin;
                        y = margin + _fontSize;
                        break;
                case TopCenter:
                        x = (frameWidth - textWidth) / 2;
                        y = margin + _fontSize;
                        break;
                case TopRight:
                        x = frameWidth - textWidth - margin;
                        y = margin + _fontSize;
                        break;
                case BottomLeft:
                        x = margin;
                        y = frameHeight - margin - (totalHeight - _fontSize);
                        break;
                case BottomCenter:
                        x = (frameWidth - textWidth) / 2;
                        y = frameHeight - margin - (totalHeight - _fontSize);
                        break;
                case BottomRight:
                        x = frameWidth - textWidth - margin;
                        y = frameHeight - margin - (totalHeight - _fontSize);
                        break;
                case Custom:
                        x = _customX;
                        y = _customY;
                        break;
        }
        return;
}

PROMEKI_NAMESPACE_END
