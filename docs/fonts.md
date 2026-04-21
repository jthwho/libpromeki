# Font Rendering {#fonts}

How to render text onto images with the Font framework.

This page describes the font rendering system, the available
renderers, and how to choose between them.

## Class Hierarchy {#font_hierarchy}

All font renderers derive from the `Font` abstract base class,
which manages common state (font filename, size, colors, paint
engine, kerning) and defines the rendering interface.

- `Font` — Abstract base. Requires a PaintEngine at
  construction. Provides setters, getters, and the pure virtual
  drawText() / measureText() / metrics interface.
- `FastFont` — Cached opaque renderer. Pre-renders each glyph
  into the target pixel format for maximum throughput.
- `BasicFont` — Alpha-compositing renderer. Composites each
  glyph pixel-by-pixel for correct transparency.

## Choosing a Renderer {#font_choosing}

| Aspect                | FastFont                        | BasicFont                              |
|-----------------------|---------------------------------|----------------------------------------|
| Rendering strategy    | Cached opaque blit (memcpy)     | Per-pixel alpha compositing             |
| Speed                 | Very fast for repeated draws    | Slower; re-composites every draw        |
| Memory                | Higher (per-glyph image cache)  | Minimal (no cache)                      |
| Transparency          | No — glyph cells are opaque     | Yes — composites over existing content  |
| Background color      | Fills glyph cell                | Ignored                                 |
| Best for              | HUD overlays, timecodes, titles | Transparent overlays, one-off renders   |

Use `FastFont` when the same text (or the same character set) is
drawn many times at the same size and color, such as burning
timecodes into a video stream. Use `BasicFont` when text must
blend smoothly over a varying background, or when rendering is
infrequent and memory matters more than speed.

## Color Semantics {#font_colors}

Both renderers accept foreground and background colors via the
`Font` base class. Their interpretation differs:

- **FastFont**: The foreground color is composited onto the
  background color to produce a fully opaque glyph cell image.
  The background fills the entire cell area.
- **BasicFont**: Only the foreground color is used. Glyph pixels
  are composited with per-pixel alpha over whatever is already in
  the target image. The background color is ignored.

## Kerning {#font_kerning}

Both renderers support optional kerning via
`Font::setKerningEnabled()`. When enabled, the renderer queries
FreeType's kerning tables to adjust horizontal spacing between
glyph pairs. Kerning is disabled by default.

## Font Metrics {#font_metrics}

After configuring a font (filename, size, and paint engine), the
following metrics are available via lazy loading:

- `Font::ascender()` — distance from baseline to top of cell.
- `Font::descender()` — distance from baseline to bottom of cell.
- `Font::lineHeight()` — full cell height (ascender + descender).

These methods trigger font loading on first call. They return 0
if the font cannot be loaded.

## PaintEngine and Invalidation {#font_paintengine}

A PaintEngine is required at construction and determines the target
pixel format. It can be changed later via
`Font::setPaintEngine()`. Switching to a PaintEngine with the
same pixel format pointer is cheap — no cache invalidation occurs.
Switching pixel formats triggers a full invalidation in subclasses.

## Examples {#font_examples}

### FastFont: Timecode Overlay {#font_example_fast}

```cpp
Image img(1920, 1080, PixelDesc::RGB8_sRGB);
FastFont ff(img.createPaintEngine());
ff.setFontFilename("/path/to/font.ttf");
ff.setFontSize(48);
ff.setForegroundColor(Color::White);
ff.setBackgroundColor(Color::Black);

// First draw caches glyphs; subsequent draws are fast blits
ff.drawText("01:00:00:00", 100, 200);
ff.drawText("01:00:00:01", 100, 200); // reuses cached glyphs
```

### BasicFont: Transparent Overlay {#font_example_basic}

```cpp
Image img(1920, 1080, PixelDesc::RGBA8_sRGB);
BasicFont bf(img.createPaintEngine());
bf.setFontFilename("/path/to/font.ttf");
bf.setFontSize(24);
bf.setForegroundColor(Color(255, 255, 255, 200)); // semi-transparent

// Composites over whatever is already in the image
bf.drawText("Watermark", 50, 100);
```
