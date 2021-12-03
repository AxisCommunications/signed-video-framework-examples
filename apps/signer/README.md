# Application to sign a video file
Note: This example application code also serves as example code for how to implement the signing side of the
*Signed Video Framework*.

## Prerequisites
This application relies on gStreamer and signed-video-framework.
- [gStreamer](https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c)
- [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework)

## Description
The application process a file NAL by NAL and adds signatures in SEIs, provided by the
*Signed Video Framework*. A successfully signed GOP prints it on the screen.

It is implemented as a gStreamer element that process every NAL and adds SEI NALs to the stream repeatedly.

## Running
Sign an mp4 file of an H264 video using the app
```
./path/to/your/installed/signer.exe -c h264 test_h264.mp4
```

There are unsigned test files in [test-files/](../../test-files/) for both H264 and H265.

Note: There is currently a known flaw when signing H265. The timestamps of the first NALs are not
set correctly. This effects the validation of the first GOP, which then may not properly parse the
NALs.
