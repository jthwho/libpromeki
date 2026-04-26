/**
 * @file      font.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/color.h>
#include <promeki/paintengine.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base class for font rendering.
 * @ingroup paint
 *
 * Font provides the common interface and state for all font renderers.
 * Subclasses implement the actual rendering strategy (e.g., cached
 * opaque blitting in FastFont, or per-pixel alpha compositing in
 * BasicFont).
 *
 * A PaintEngine is required at construction time and determines the
 * target pixel format.  The font file, size, and colors are configured
 * via setters.  Subclasses are notified of state changes through the
 * onStateChanged() hook, which they can override to invalidate caches
 * or reload resources.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * Subclass implementations may maintain internal glyph caches that are
 * not synchronized.
 *
 * @see FastFont, BasicFont
 * @see @ref fonts "Font Rendering"
 */
class Font {
        public:
                /** @brief Virtual destructor. */
                virtual ~Font();

                Font(const Font &) = delete;
                Font &operator=(const Font &) = delete;

                /**
                 * @brief Sets the path to the TrueType font file.
                 * @param val Path to the font file. May be either a
                 *            filesystem path or a @c ":/..." resource
                 *            path. Passing an empty string resets the
                 *            font to the library's bundled default —
                 *            the actual default path is an internal
                 *            detail and may change between releases.
                 */
                void setFontFilename(const String &val);

                /**
                 * @brief Returns the current font filename.
                 * @return The font filename.
                 */
                const String &fontFilename() const { return _fontFilename; }

                /**
                 * @brief Sets the font size in pixels.
                 * @param val Font size in pixels.
                 */
                void setFontSize(int val);

                /**
                 * @brief Returns the current font size in pixels.
                 * @return The font size.
                 */
                int fontSize() const { return _fontSize; }

                /**
                 * @brief Sets the foreground (text) color.
                 * @param color The foreground Color.
                 */
                void setForegroundColor(const Color &color);

                /**
                 * @brief Returns the current foreground color.
                 * @return The foreground Color.
                 */
                const Color &foregroundColor() const { return _fg; }

                /**
                 * @brief Sets the background color.
                 * @param color The background Color.
                 *
                 * The interpretation of the background color depends on the
                 * subclass.  FastFont fills glyph cells with it; BasicFont
                 * ignores it because it composites over existing content.
                 */
                void setBackgroundColor(const Color &color);

                /**
                 * @brief Returns the current background color.
                 * @return The background Color.
                 */
                const Color &backgroundColor() const { return _bg; }

                /**
                 * @brief Sets the paint engine used for rendering.
                 * @param pe The PaintEngine to use.
                 *
                 * The new engine is always stored.  However, onStateChanged()
                 * is only called when the pixel format pointer differs from
                 * the previous engine's, so switching between PaintEngines
                 * that share the same pixel format is cheap.
                 */
                void setPaintEngine(const PaintEngine &pe);

                /**
                 * @brief Returns the current paint engine.
                 * @return The PaintEngine.
                 */
                const PaintEngine &paintEngine() const { return _paintEngine; }

                /**
                 * @brief Enables or disables kerning.
                 * @param val True to enable kerning, false to disable.
                 *
                 * When enabled, the renderer applies FreeType kerning data
                 * to adjust spacing between glyph pairs.  Defaults to false.
                 */
                void setKerningEnabled(bool val);

                /**
                 * @brief Returns whether kerning is enabled.
                 * @return True if kerning is enabled.
                 */
                bool kerningEnabled() const { return _kerning; }

                /**
                 * @brief Returns whether the font is validly configured.
                 * @return True if a font filename is set, the font size is
                 *         positive, and the paint engine has a pixel format.
                 */
                bool isValid() const;

                /**
                 * @brief Draws text into the image bound to the current PaintEngine.
                 * @param text The text string to render.
                 * @param x    The x coordinate of the text origin in pixels.
                 * @param y    The y coordinate of the text baseline in pixels.
                 * @return True if the text was drawn successfully.
                 */
                virtual bool drawText(const String &text, int x, int y) = 0;

                /**
                 * @brief Measures the pixel width of a text string without drawing it.
                 * @param text The text string to measure.
                 * @return The width in pixels, or 0 on error.
                 */
                virtual int measureText(const String &text) = 0;

                /**
                 * @brief Returns the line height in pixels.
                 *
                 * The line height is the full height of the character cell
                 * (ascender + descender) as reported by the font metrics.
                 * May trigger lazy font loading in the subclass.
                 *
                 * @return Line height in pixels, or 0 if the font is not loaded.
                 */
                virtual int lineHeight() = 0;

                /**
                 * @brief Returns the ascender in pixels.
                 *
                 * The ascender is the distance from the baseline to the top
                 * of the character cell.  May trigger lazy font loading.
                 *
                 * @return Ascender in pixels, or 0 if the font is not loaded.
                 */
                virtual int ascender() = 0;

                /**
                 * @brief Returns the descender in pixels.
                 *
                 * The descender is the distance from the baseline to the
                 * bottom of the character cell.  May trigger lazy font loading.
                 *
                 * @return Descender in pixels, or 0 if the font is not loaded.
                 */
                virtual int descender() = 0;

        protected:
                /**
                 * @brief Constructs a Font with the given paint engine.
                 * @param pe The PaintEngine to use for rendering.
                 */
                Font(const PaintEngine &pe);

                /**
                 * @brief Called when any configuration property changes.
                 *
                 * Subclasses override this to invalidate caches or mark
                 * internal state as dirty.  The default implementation
                 * does nothing.
                 *
                 * @note This is not called when setPaintEngine() is invoked
                 *       with an engine that has the same pixel format pointer
                 *       as the current one.
                 */
                virtual void onStateChanged();

                /**
                 * @brief Returns the filename subclasses should actually load.
                 *
                 * When the caller set an explicit filename (filesystem
                 * path or @c ":/..." resource path), that is returned
                 * verbatim. When the filename is empty this returns
                 * the library's bundled default font path — an
                 * internal detail that subclasses consume through this
                 * method rather than by touching @c _fontFilename
                 * directly.
                 */
                String effectiveFilename() const;

                String          _fontFilename;
                int             _fontSize = 12;
                Color           _fg = Color::White;
                Color           _bg = Color::Black;
                PaintEngine     _paintEngine;
                bool            _kerning = false;
};

PROMEKI_NAMESPACE_END
