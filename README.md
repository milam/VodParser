# VodScanner

Parses Twitch Overwatch tournament VODs for hero picks.

Example output: [google sheet](https://docs.google.com/spreadsheets/d/1Uvilc3Hj9vp2YDRrV5uaHfyOjms3Wy0PC8qVgy065SE/edit?usp=sharing) (calculations have been added manually)

Can be built with VS 2013+ or GCC 4.7+. Requires OpenCV 3.0 (both) and cURL (GCC).

Usage: `./vodscanner <vod-id>`

VODs are downloaded in small chunks and saved as `<vod-id>/cache/chunkXXXXXX.ts` - they can be deleted later on or even as the program is running (just make sure to keep the last 8 as they might be in use).

Hero picks are documented in `<vod-id>/picks.txt` in TSV (tab separated) format, with the first two columns being chunk start time and duration (in seconds), and the remaining listing hero names. The program adds a blank row between matches. It tries to ignore match preparation time but it doesn't do so perfectly, so you might need to go through the resulting list and delete all small groups of rows. The program also saves a screenshot for every match in `<vod-id>/<start-time>.png`.

The program saves its current execution status in a `json` file, so you can close it at any time and resume download later.

### Source code notes

Most of the generic code was copied from my other projects. The relevant code is in `vod.h/cpp` (downloading VOD chunks), `match.h/cpp` (pattern matching, identical to OpenCV `matchTemplate` with `CV_TM_CCORR_NORMED` but supports alpha mask) and `main.cpp` containing the application logic.
