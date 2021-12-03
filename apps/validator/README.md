## Application to validate authenticity
Note: This application code also serves as example code for how to implement the validation side of
the *Signed Video Framework*.

### Prerequisites
This application relies on GstAppSink (part of gStreamer) and signed-video-framework.
- [gStreamer](https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c)
- [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework)

### Description
The application process NAL by NAL and validates the authenticity continuously. The result is
written on screen and in addition, a summary is written to the file validation_results.txt.

## Running
Validate an MP4 file of an H264 video using the app
```
./path/to/your/installed/validator.exe -c h264 signed_test_h264.mp4
```

There are both signed and unsigned test files in [test-files/](../../test-files/) for both H264 and
H265.

Note: There is currently a known flaw in
[signed_test_h265.mp4](../../test-files/signed_test_h265.mp4) where the timestamps of the first NALs
are not set correctly. This effects the validation of the first GOP, which is displayed as *signed*,
which means we know it is signed, but we cannot validate it.
