*Copyright (C) 2021, Axis Communications AB, Lund, Sweden. All Rights Reserved.*

# Signed Video Framework examples

## What is Signed Video Framework?
The *Signed Video Framework* is an open-source project that secures an H264, or H265, video from tampering by adding cryptographic signatures in SEI frames. See the [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework) for more details.

## Getting started with the repo
This repository contains a set of application examples which aims to enrich the developers implementation experience. All examples are using the [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework) and has a README file in its directory with instructions on how to build and run it.

The repository uses meson + ninja as default build method. Further, all application examples uses gStreamer APIs.
- [meson](https://mesonbuild.com/Getting-meson.html) Getting meson and ninja. Meson version 0.49.0 or newer is required.
- [gStreamer](https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c) All applications are built around the gStreamer framework to handle coded video.

## Example applications
Below is a list of example applications available in the repository.
- [signer](./apps/signer/)
  - The example code implements video signing.
- [validator](./apps/validator/)
  - The example code implements video authenticity validation.

### Building applications
The applications in this repository all have meson options for easy usage. These options are by default disabled and the user can enable an arbitrary number of them.

First you need to have the signed-video-framework library installed. Installing the shared library and applications locally is in many cases preferably.

Assuming the signed-video-framework library is installed and accessible build the application with meson as
```
meson -D<application>=true path/to/signed-video-framework-examples path/to/build/folder
meson install -C path/to/build/folder
```
Note that some applications require additional environment variables set, for example, `GST_PLUGIN_PATH`; See, individual application README.md.

#### Example meson commands on Linux
These example commands assume the current directory is the parent directory of both `signed-video-framework` and `signed-video-framework-examples`.
```
meson --prefix $PWD/my_installs signed-video-framework build_lib
meson install -C build_lib
```
Then build and install the `<application>.exe` as
```
meson --prefix $PWD/my_installs -D<application>=true signed-video-framework-examples build_apps
meson install -C build_apps
```
The executable is now located at `./my_installs/bin/<application>.exe`

## Example files
Shorter MP4 recordings for testing can be found in [test-files/](./test-files/).

# License
[MIT License](./LICENSE)
