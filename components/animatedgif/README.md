# animatedgif

[bitbank2/AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) (Apache-2.0), packaged as an
ESP-IDF component. The source is the submodule at `external/AnimatedGIF`, pinned to upstream
`c2478ec` — library version 2.2.3, which upstream has not tagged. The last tag, 2.2.0, predates
fixes this firmware depends on: disposal method 2 in cooked output, and a crash on corrupt
files with invalid block sizes.

Nothing upstream is modified. `shim/idf_shim.h` supplies the `millis()` and `delay()` that
`AnimatedGIF::playFrame()` calls when it is built for neither Arduino, Linux nor macOS.

The licence is upstream's: `external/AnimatedGIF/LICENSE`.
