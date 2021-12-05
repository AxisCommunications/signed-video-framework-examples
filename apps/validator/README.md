*Copyright (C) 2021, Axis Communications AB, Lund, Sweden. All Rights Reserved.*

# Application to validate video authenticity
Note: This ewxample application code also serves as example code for how to implement the validation side of
the *Signed Video Framework*.

## Prerequisites
This application relies on GstAppSink (part of gStreamer) and signed-video-framework.
- [gStreamer](https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c)
- [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework)

## Description
The application process NAL by NAL and validates the authenticity continuously. The result is
written on screen and in addition, a summary is written to the file validation_results.txt.

It is implemented as a GstAppSink that process every NAL and validates the authenticity on-the-fly.

## Building the validator application
Below are meson commands to build the validator application. First you need to have the signed-video-framework library installed. Installing the share library and applications locally is in many cases preferably.
```
meson -Dvalidator=true path/to/signed-video-framework-examples path/to/build/folder
meson install -C path/to/build/folder
```

### Example meson commands on Linux
The example commands also assume that both signed-video-framework and signed-video-framework-examples are located in the current directory.
```
meson --prefix $PWD/my_installs signed-video-framework build_lib
meson install -C build_lib
```
Then build and install the `validator.exe` as
```
meson --prefix $PWD/my_installs -Dvalidator=true signed-video-framework-examples build_apps
meson install -C build_apps
```
The executable is now located at `./my_installs/bin/validator.exe`

## Running
Validate an mp4 file of an H264 video using the app
```
./path/to/your/installed/validator.exe -c h264 signed_test_h264.mp4
```
With the example Linux commands above testing `signed_test_h264.mp4` in [test-files/](../../test-files/)
```
./my_installs/bin/validator.exe -c h264 signed-video-framework-examples/test-files/signed_test_h264.mp4
```

There are both signed and unsigned test files in [test-files/](../../test-files/) for both H264 and
H265.

Note: There is currently a known flaw in
[signed_test_h265.mp4](../../test-files/signed_test_h265.mp4) where the timestamps of the first NALs
are not set correctly. This affects the validation of the first GOP, which is displayed as *signed*,
hence, a signature has so far been detected but there is no signature for the first GOP.
