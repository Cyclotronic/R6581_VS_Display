# Slow-motion (240 fps) analysis of display updates

**Question:** when the reading changes, does the TFT show a half-drawn frame (tearing)? If it does, drawing into a second buffer and flipping
to it (see the page-flip section of [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md)) should fix it.

**Answer:** no. Even at 240 fps the original firmware's in-place redraw updates all digits within about one frame of each other, which is
quicker than the panel's own pixel response, and the page-flip build showed no visible benefit. What looked like tearing in photos and 30 fps
video was the LCD's pixel fade plus camera exposure.

## What was filmed

- **Camera:** a phone in slow-motion mode, 240 fps (it records a mix of 120 and 240 fps segments, so frame times are read from the file, not assumed). Framing was fixed on the main line of the TFT with the meter in continuous DCV, so the reading changes every
  0.35 s or so.
- **Two clips, same framing:** the original firmware (about 19 s) and a build with page flip (about 20 s, about 4,200 frames).
- A 30 fps clip was tried first and was not enough: each reading change appears as one frame that is a mix of old and new digits, because the
  exposure of a single frame is long compared with the update.

## How it is analysed (`tools/events240.py`)

1. ffmpeg reads every frame with `-fps_mode passthrough` (so the 120 and 240 fps mix is kept exactly), crops the digit area of the main line,
   halves it and converts to grey.
2. Each frame is thresholded halfway between the background and the digit brightness, giving a bright/dark mask of the digits.
3. A **reading change** is found where the mask changes sharply from one frame to the next; events less than 12 frames apart are merged (the fade of one reading is not a new reading).
4. For each change, the crop is split into digit columns. For every column that actually changed, the **onset** is the first frame in which the column has moved 25 % of the way from its old to its final state.
5. The **spread** of a change is the difference in frames between the earliest and latest onset across its changed columns. At 240 fps one frame is 4.2 ms.

If the display updates atomically, all columns start in the same frame (spread 0). A progressive in-place redraw would start them one after another (a larger spread).
Changes with fewer than three changed columns are skipped, since a spread cannot be measured from them.

```
python -I tools/events240.py <video> <x> <y> <w> <h> [ncols]   # crop of the digit area in source pixels; needs ffmpeg, ffprobe, numpy
```

## Results

| | Original (in-place redraw) | Page flip |
| --- | --- | --- |
| Reading changes analysed | 43 | 59 |
| Median onset spread | 1 frame (4.2 ms) | 1 frame (4.2 ms) |
| Spread of 0 or 1 frame | 53 % | 71 % |
| Spread of 3 frames or more | 26 % | 20 % |

- **Few large outliers in both.** A change with a spread of 3 or more frames (12 ms or more) is rare and a few of them are measurement noise (a column whose new digit shares most of its pixels with the old one).
- **The fade:** after the digits switch, the old digit fades out over about 8 frames (about 33 ms) in the flip build and about 25 to 30 ms in the original. That is the LCD
  panel's pixel response, not a partial redraw. Frame by frame, the original also shows the new digits appearing together.
- **Interpretation:** the original redraw (about 25 ms at `-O0`, about 18 ms at `-O2`) is short compared with the panel's response, so no tearing is visible even at 240 fps.
  The flip build gives a slightly tighter spread (71 % against 53 % within a frame) but nothing you can see, and it costs a second buffer and extra latency. It was not merged.

## Caveats

- Two clips from one session each; the percentages are from 43 and 59 changes, so differences of a few points are not significant.
- The camera, lighting and crop affect the mask threshold; the method compares the two clips with the same settings, not absolute times.
- The measurement sees what the camera sees. It cannot show a very brief artefact shorter than one frame (4.2 ms), or anything that depends on the camera's rolling shutter.
- The original firmware clip is of the `-O0` build; the flip clip is of a flip build at `-O2`, so the two differ in more than the flip. The conclusion (no visible tearing in either) does not depend on that.

The clips themselves are not included in this repository.
