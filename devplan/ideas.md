# Random Ideas That Need Exploration

1. ~~All MediaIO should take a "Name" config option.~~ *Mostly landed with
   [benchmarking.md](benchmarking.md) Part C — `MediaConfig::Name` (default `"media<localId>"`)
   and `MediaConfig::Uuid` (default fresh `UUID::generate()`) are shipped alongside a process-local
   atomic `localId` counter. Every `MediaIO` instance has a stable identity triple queryable via
   `localId()` / `name()` / `uuid()`. Still pending: the backend half of the idea — individual
   backends that spawn their own threads outside the shared `MediaIO::pool()` should name those
   threads from `name()`. No backend currently does this; it's a small drive-by change per
   backend when someone gets annoyed enough at `top` / `htop` showing unnamed worker threads.*

2. Clean up the mediaplay help:  Make the MediaIO config parameter help a single line and remove the
   empty line.

3. Make mediaplay check for unknown config options.  Maybe we need to wire this into the MediaIO
   framework itself, or maybe what's already there gives us enough to do this?  We don't want to
   have any MediaIOTask specific details in mediaio.

4. We should have mediaplay ignore displaying the Type config in the help, as that's implied by the
   -i, -o, and -c options.

5. We should make the mediaplay --stats go to logging, not print directly.

6. Make mediaplay set the default log to info if any reporting is going to happen (just in case it
   was overridden by an environment variable).

7. Go through the library and make sure we've moved all well known enums to use the Enum class.

8. Change MediaIO to use Source and Sink instead of Read and Write, to be less ambiguous.  We ought
   to make Source, Sink, SinkAndSource a Enum in enums.h.

9. Make the running test output from promeki-bench use a fixed size for the test name, so the
   running results are easier to read.  Right now they shift all over the place and make it harder
   to read.

10. We have a way to convert a byte count to human in String (e.g. 10000000 to 10 MB).  We also have
    other bits in the library that compute human strings for time (e.g. 1.67 ms).  We ought to just
    put all this metric (or 1024 based) logic all in one place.  The units are kind of arbitrary, we
    could keep those w/ the caller.

11. We should enhance the Enum object with a new object called EnumList that takes care of storing a
    list of enums (of the same type).  It should handle to/from string with a comma'ed list.  We
    should make it clear enum names should only contain A-Z a-z 0-9 and underscore (and never start
    with a number) basically, keep them C safe names.

12. We should use the EnumList object to configure the audio test pattern generator, where the list
    defines which test pattern should be put on each channel of audio.  If the list is shorter than
    the channels, we silence the remaining audio channels.

13. We should add some further audio test patterns to the generator: a pattern that can help us
    diagnose sample rate conversion, a pattern that can encode the channel number (machine readable)
    in the audio.  We'd also want to add tests for these in the Inspector MediaIO object

14. We need a well known enum to describe progressive and interlaced video.  It should contain the
    following: Unknown, Progressive, InterlacedEvenFirst, InterlacedOddFirst.  We should also add
    this enum to the ImageDesc object.  Currently we just have an interlaced flag.  That's not good
    enough.

15. We should add a VideoFormat object to the class.  This should class should take care of handling
    SMPTE/well know format names.  It should be very accepting of names (e.g. 1080p50, 1920x1080 50p,
    4Kp60, etc) but provide toString() names that try to be SMPTE names first and then more generic
    names if the VideoFormat isn't a SMPTE rate.  We should add the ability to get the VideoFormat
    from MediaDesc (should take an arg w/ the image index, default to 0).



