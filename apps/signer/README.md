*Copyright (C) 2021, Axis Communications AB, Lund, Sweden. All Rights Reserved.*

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

## Building the signer application
Below are meson commands to build the signer application. First you need to have the signed-video-framework library installed. Installing the share library and applications locally is in many cases preferably.

Then building the signer application with meson.
```
meson -Dsigner=true path/to/signed-video-framework-examples path/to/build/folder
meson install -C path/to/build/folder
```

### Example meson commands on Linux
The example commands also assume that both signed-video-framework and signed-video-framework-examples are located in the current directory.
```
meson --prefix $PWD/my_installs signed-video-framework build_lib
meson install -C build_lib
```
Then build and install the `signer.exe` as below. Since this application is implemented as a gStreamer set `GST_PLUGIN_PATH` for gStreamer to find it.
```
export GST_PLUGIN_PATH=$PWD/my_installs
meson --prefix $PWD/my_installs -Dsigner=true signed-video-framework-examples build_apps
meson install -C build_apps
```
The executable is now located at `./my_installs/bin/signer.exe`

## Running
Sign an mp4 file of an H264 video using the app
```
./path/to/your/installed/signer.exe -c h264 test_h264.mp4
```
With the example Linux commands above testing `test_h264.mp4` in [test-files/](../../test-files/).
```
./my_installs/bin/signer.exe -c h264 signed-video-framework-examples/test-files/test_h264.mp4
```

There are unsigned test files in [test-files/](../../test-files/) for both H264 and H265.

Note: There is currently a known flaw when signing H265. The timestamps of the first NALs are not
set correctly. This effects the validation of the first GOP, which then may not properly parse the
NALs.
