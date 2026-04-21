# Color Science {#color_science}

How color works, and how promeki represents it.

This page introduces the color science concepts behind the library's
`Color`, `ColorModel`, `XYZColor`, and `CIEPoint` classes. It is written
for programmers who may not have a color science background and want to
understand what these classes do and why they are designed the way they
are.

## What Is a Color? {#cs_what}

A color, at the physical level, is a distribution of electromagnetic
energy across the visible spectrum (roughly 360–700 nm wavelength).
The human eye has three types of cone cells, each sensitive to a
different range of wavelengths. The brain combines the three cone
responses into the sensation we perceive as "color."

Because we only have three types of cones, any color perception can
be described by three numbers — a *tristimulus* value. This is the
foundation of all color science: three numbers are sufficient to
describe any color a human can see.

The fourth number the library stores — alpha — is not part of color
science. It represents opacity (1.0 = fully opaque, 0.0 = fully
transparent) and is used by compositing and rendering systems.

## CIE XYZ: The Universal Color Space {#cs_cie}

In 1931, the International Commission on Illumination (CIE) defined a
set of *color matching functions* based on experiments with human
observers. These functions map any spectral distribution to three
numbers called **X**, **Y**, and **Z** — the CIE XYZ tristimulus
values.

- **Y** is designed to equal luminance (perceived brightness).
- **X** and **Z** encode chromaticity (the "colorful" part) and are
  chosen so that all visible colors have non-negative XYZ values.

CIE XYZ is *device-independent*: it describes what a color looks like
to a human observer, not how a particular monitor or camera represents
it. For this reason, promeki uses XYZ as the "connection space" for
all color conversions: to convert from any model A to any model B, the
path is always A → XYZ → B.

The library represents XYZ values with the `XYZColor` class.

> **Reference:** CIE 015:2004, "Colorimetry" (the defining standard).

## Chromaticity: Separating Color from Brightness {#cs_chromaticity}

XYZ has three dimensions, but often we want to talk about color
independent of brightness. The CIE xy chromaticity diagram achieves
this by projecting XYZ onto two dimensions:

```
x = X / (X + Y + Z)
y = Y / (X + Y + Z)
```

The resulting (x, y) point describes the *hue and saturation* of a
color with no brightness information. The boundary of the
chromaticity diagram — called the *spectral locus* — is a horseshoe
shape corresponding to the pure spectral colors (single wavelengths).
All real colors lie inside or on this boundary.

Chromaticity is how color space *primaries* and *white points* are
specified. For instance, the sRGB red primary is at (0.64, 0.33) and
the D65 white point is at (0.3127, 0.3290).

The library represents chromaticity coordinates with the `CIEPoint`
class, which also provides utilities for converting between
wavelengths, chromaticity, and correlated color temperature.

## RGB Color Spaces {#cs_rgb}

An RGB color space maps three numbers (Red, Green, Blue) to real-world
colors. The mapping is defined by four things:

1. **Three primaries** — the chromaticity (x, y) of the red, green,
   and blue lights used by the display. These define a triangle on the
   chromaticity diagram; any color inside the triangle can be
   reproduced. This triangle is the *gamut* of the color space.
2. **A white point** — the chromaticity of "white" (equal R=G=B).
   Most modern standards use D65 (~6504 K daylight).
3. **A transfer function** ("gamma curve") — a nonlinear mapping
   between encoded values (what gets stored in a file or sent to a
   display) and linear light intensity. This encoding improves
   perceptual uniformity for 8-bit and 10-bit storage.
4. **A luminance range** — typically Y = 0 to Y = 1 for SDR content.

Different standards define different primaries and transfer functions:

| Color Space   | Typical Use      | Gamut    | Transfer Function |
|---------------|------------------|----------|-------------------|
| sRGB          | Web, consumer displays | Standard | ~1/2.4 power + linear toe |
| Rec.709       | HD video (1080p) | Same as sRGB | 0.45 power + linear toe |
| Rec.601 PAL   | SD video (576i)  | Slightly different | Same as Rec.709 |
| Rec.601 NTSC  | SD video (480i)  | Slightly different | Same as Rec.709 |
| Rec.2020      | UHD/4K/8K video  | Wide gamut | Similar to Rec.709 |

Note that sRGB and Rec.709 share the same primaries and white point
but have slightly different transfer functions. In practice the
difference is small, but it matters for precise color work.

Each of these is represented by a `ColorModel` constant (e.g.
`ColorModel::sRGB`, `ColorModel::Rec709`). Each also has a
"Linear" variant (e.g. `ColorModel::LinearSRGB`) that uses the
same primaries but an identity transfer function, for use in
compositing or physically-based rendering where arithmetic must
operate on linear light values.

### Transfer Functions (Gamma) {#cs_transfer}

If you store light intensity linearly in 8 bits, you get 256 levels
from black to white. But the human eye is much more sensitive to
differences in dark tones than in bright tones, so a linear encoding
wastes most of its precision on bright values that all look the same.
The transfer function (loosely called "gamma") warps the encoding so
that more of the 256 levels are allocated to the dark end of the scale
where the eye can tell the difference.

The transfer function has two directions:

- The **OETF** (Opto-Electronic Transfer Function) goes from linear
  light to encoded values. A camera applies this.
- The **EOTF** (Electro-Optical Transfer Function) goes from encoded
  values back to linear light. A display applies this.

When converting a color to CIE XYZ, the library applies the EOTF
(linearizes) first, then multiplies by the primary matrix. When
converting from XYZ, it applies the inverse matrix, then the OETF.

`ColorModel::applyTransfer()` and `ColorModel::removeTransfer()`
expose these functions directly. `ColorModel::isLinear()` tells
you whether a model has an identity transfer (i.e. values are already
linear).

### Prime Notation: R'G'B' vs RGB {#cs_prime}

In color science literature, a prime symbol (') is used to distinguish
between linear-light values and gamma-encoded values:

- **R, G, B** (no prime) are *linear-light* intensities — physically
  proportional to the number of photons. Doubling R doubles the
  light output. These are what `ColorModel::LinearSRGB` stores.
- **R', G', B'** (with prime) are *gamma-encoded* values — the
  result of applying the transfer function (OETF) to linear light.
  These are what you find in an sRGB JPEG or an 8-bit frame buffer.
  `ColorModel::sRGB` stores R'G'B' values.

The distinction matters because many operations (alpha compositing,
lighting, blur) must be done in linear light to be physically correct.
If you blend two R'G'B' values directly, you get visible darkening at
the blend boundary because the gamma curve makes the math nonlinear.
The correct approach is: remove gamma → blend in linear → reapply
gamma.

The same notation applies to luma:

- **Y** (no prime) is *linear-light luminance*, as defined by CIE:
  `Y = 0.2126*R + 0.7152*G + 0.0722*B` (for BT.709 coefficients).
- **Y'** (with prime) is *luma*, computed from gamma-encoded
  components: `Y' = 0.2126*R' + 0.7152*G' + 0.0722*B'`. This is
  what video systems actually transmit.

In casual usage, "Y" is often used loosely to mean either. In the
library, the YCbCr models operate on gamma-encoded (primed)
components following standard broadcast practice: the luma Y' is
computed from the parent model's R'G'B' values. The library's
accessor is simply named `y()` since the context is unambiguous.

> **Reference:** Charles Poynton, *Digital Video and HD*, Chapter 6
> ("Luma and Color Differences") gives a thorough treatment of the
> distinction between luminance (Y) and luma (Y') and why it matters.

## Derived Color Models {#cs_derived}

Some color representations are not independent color spaces but
mathematical rearrangements of an RGB space.

### HSV and HSL {#cs_hsv}

**HSV** (Hue, Saturation, Value) and **HSL** (Hue, Saturation,
Lightness) rearrange RGB into a cylindrical coordinate system:

- **Hue** is the "color" on a 0–360 degree wheel (red at 0, green
  at 120, blue at 240).
- **Saturation** is how vivid the color is (0 = gray, 1 = pure color).
- **Value** (HSV) or **Lightness** (HSL) is brightness.

HSV and HSL are widely used in color pickers and artistic tools because
adjusting hue, saturation, and brightness independently is intuitive.
However, they are not perceptually uniform — "50% lightness" in HSL
does not look equally bright across all hues.

In the library, HSV and HSL are *derived from a parent RGB model* —
currently sRGB. Converting between HSV and sRGB is a local
mathematical transform that does not require going through XYZ, but
the library routes through XYZ anyway for uniformity.

See `ColorModel::HSV_sRGB` and `ColorModel::HSL_sRGB`.

### YCbCr (Luma/Chroma) {#cs_ycbcr}

**YCbCr** separates an image into luma (brightness) and two
chroma-difference (color) signals:

- **Y'** (luma) is a weighted sum of R', G', B' (gamma-encoded).
- **Cb** (blue-difference chroma) is proportional to B' - Y'.
- **Cr** (red-difference chroma) is proportional to R' - Y'.

This separation is fundamental to video compression. The human visual
system is much less sensitive to spatial detail in color than in
brightness, so Cb and Cr can be stored at lower resolution (chroma
subsampling, e.g. 4:2:2 or 4:2:0) without visible quality loss.

The exact Y'/Cb/Cr coefficients depend on which RGB space the signal
came from:

- `ColorModel::YCbCr_Rec709` uses BT.709 luma coefficients
  (`Y' = 0.2126R' + 0.7152G' + 0.0722B'`), standard for HD video.
- `ColorModel::YCbCr_Rec601` uses BT.601 luma coefficients
  (`Y' = 0.299R' + 0.587G' + 0.114B'`), standard for SD video.
- `ColorModel::YCbCr_Rec2020` uses BT.2020 luma coefficients
  (`Y' = 0.2627R' + 0.6780G' + 0.0593B'`), standard for UHD video.

#### Why YCbCr, not YUV? {#cs_not_yuv}

The term "YUV" is widely but incorrectly used as a synonym for YCbCr
in software and API documentation. They are actually different things:

- **YUV** is an analog encoding defined for PAL composite video. U
  and V are continuous, unscaled chroma-difference signals with ranges
  that depend on the modulation standard. YUV is not used in any
  modern digital format.
- **YCbCr** is the digital encoding defined by ITU-R BT.601 and
  subsequent standards. Cb and Cr are scaled and offset to fit into
  a defined integer range (e.g. 16–240 for 8-bit studio levels).
  This is what JPEG, MPEG, H.264, H.265, and every other modern
  codec actually uses.
- **YPbPr** is the analog component video equivalent of YCbCr,
  carried on three separate wires (the red/green/blue component
  cables on older equipment).

Many APIs (including V4L2, FFmpeg's naming, and DirectShow) label
their pixel formats "YUV" when they are actually YCbCr. This library
uses the correct term. If you encounter "YUV" in external code, it
almost certainly means YCbCr.

> **Reference:** Charles Poynton, *Digital Video and HD*, Section 7.4
> ("YUV and luminance considered harmful") thoroughly explains this
> naming confusion and why the distinction matters.

## CIE Perceptual Models {#cs_cie_models}

### CIE L*a*b* {#cs_lab}

**CIE L\*a\*b\*** (often written "Lab") is a rearrangement of XYZ
designed to be *perceptually uniform*: a numerical difference of, say,
5 units in Lab corresponds to roughly the same perceived color
difference regardless of where in color space you are. This makes Lab
ideal for:

- Measuring color accuracy (Delta E calculations).
- Image processing operations like sharpening or noise reduction that
  should affect perceptual detail equally across colors.

The three components are:

- **L\*** (Lightness): 0 = black, 100 = white.
- **a\***: negative = green, positive = red.
- **b\***: negative = blue, positive = yellow.

In the library, Lab components are stored normalized to 0.0–1.0. Use
`ColorModel::toNative()` to get the conventional ranges, or
`Color::fromNative()` to construct a `Color` from conventional values.

See `ColorModel::CIELab`.

## Component Normalization {#cs_normalization}

All color components in promeki are stored as normalized floats in the
range 0.0–1.0 (with the exception that linear-light HDR values may
exceed 1.0). This provides a uniform interface regardless of the
color model.

For models where the conventional range differs, the `ColorModel`
provides native range information via `ColorModel::compInfo()`,
and the conversion helpers `ColorModel::toNative()` and
`ColorModel::fromNative()` (or their `Color` counterparts).

| Model    | Component | Normalized Range | Native Range |
|----------|-----------|------------------|--------------|
| RGB      | Red       | 0.0 – 1.0        | 0.0 – 1.0    |
| HSV      | Hue       | 0.0 – 1.0        | 0 – 360 degrees |
| HSV      | Saturation| 0.0 – 1.0        | 0.0 – 1.0    |
| Lab      | L*        | 0.0 – 1.0        | 0 – 100      |
| Lab      | a*        | 0.0 – 1.0        | -128 – 127   |
| YCbCr    | Cb        | 0.0 – 1.0        | -0.5 – 0.5   |

## How Conversion Works {#cs_conversion}

To convert a `Color` from model A to model B, the library performs:

```cpp
// Pseudocode for Color::convert()
float xyz[3];
modelA.toXYZ(sourceComponents, xyz);   // A -> XYZ
modelB.fromXYZ(xyz, destComponents);   // XYZ -> B
```

For an RGB model, `toXYZ()` means:

1. Remove the transfer function (EOTF) to get linear RGB.
2. Multiply by the 3x3 RGB-to-XYZ matrix (the Normalized Primary
   Matrix, computed from the primaries and white point).

And `fromXYZ()` means:

1. Multiply by the inverse matrix (XYZ-to-RGB).
2. Apply the transfer function (OETF).

For derived models (HSV, YCbCr), the conversion first goes to the
parent RGB model, then follows the RGB pipeline to XYZ.

This pipeline is designed for precision, not for bulk pixel processing.
For high-throughput image conversion, use the library's image pipeline
facilities which can generate optimized combined matrices and LUTs.

## Library Classes {#cs_classes}

| Class | Purpose |
|-------|---------|
| `Color` | A color value: four float components + a `ColorModel`. The main type you work with. |
| `ColorModel` | Defines a color model/space: type, primaries, white point, transfer function. Lightweight value (just a pointer). |
| `XYZColor` | A color in the CIE 1931 XYZ color space. Three double-precision components. |
| `CIEPoint` | A point in the CIE xy chromaticity diagram. Used to define primaries and white points. |

### Quick Start {#cs_usage}

```cpp
#include <promeki/color.h>

using namespace promeki;

// Create colors in different models
Color red   = Color::Red;                           // Named sRGB constant
Color green = Color::hsv(120.0f / 360.0f, 1.0f, 1.0f); // HSV green
Color gray  = Color::lab(0.5f, 128.0f/255.0f, 128.0f/255.0f); // 50% Lab gray

// Convert between models
Color hsvRed = red.toHSV();
Color labRed = red.toLab();
Color back   = labRed.toRGB();   // roundtrip back to sRGB

// Access components
float hue = hsvRed.h();          // normalized 0-1
float hueDegrees = hsvRed.toNative(0); // native 0-360

// Get 8-bit sRGB for display (auto-converts if needed)
uint8_t r = green.r8();  // converts HSV -> sRGB -> 0-255

// Serialize losslessly
String s = red.toString();                // "sRGB(1,0,0,1)"
Color parsed = Color::fromString(s);      // roundtrips perfectly

// Color operations
Color mid = red.lerp(green, 0.5);         // interpolate
Color comp = red.complementary();          // cyan
double lum = red.luminance();              // perceptual brightness
```

## Further Reading {#cs_further}

- Charles Poynton, *Digital Video and HD: Algorithms and Interfaces*,
  2nd ed. (Morgan Kaufmann, 2012). Practical coverage of RGB, YCbCr,
  transfer functions, and broadcast color spaces.
- Mark D. Fairchild, *Color Appearance Models*, 3rd ed. (Wiley, 2013).
  Thorough treatment of CIE colorimetry and perceptual color models.
- Wyszecki & Stiles, *Color Science: Concepts and Methods, Quantitative
  Data and Formulae*, 2nd ed. (Wiley-Interscience, 1982; reprinted
  2000). The definitive reference for CIE colorimetry.
- Bruce Lindbloom's color science website
  (<http://www.brucelindbloom.com>). Comprehensive equations and
  calculators for color space conversions.
- IEC 61966-2-1:1999, the sRGB standard.
- ITU-R BT.709-6, the Rec.709 HD video standard.
- ITU-R BT.601-7, the Rec.601 SD video standard.
- ITU-R BT.2020-2, the Rec.2020 UHD video standard.
