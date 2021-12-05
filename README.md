*Copyright (C) 2021, Axis Communications AB, Lund, Sweden. All Rights Reserved.*

# What is Signed Video Framework?
The *Signed Video Framework* is an open-source project that secures an H264, or H265, video from tampering by adding cryptographic signatures in SEI frames. See the [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework) for more details.

## Getting started with the repo
This repository contains a set of application examples which aims to enrich the developers implementation experience. All examples are using the [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework) and has a README file in its directory with instructions on how to build and run it.

## Example applications
Below is a list of example applications available in the repository.
- [signer](./apps/signer/)
  - The example code implements video signing.
- [validator](./apps/validator/)
  - The example code implements video authenticity validation.

### Building applications
The applications in this repository all have meson options for easy usage. First you need to have the signed-video-framework library installed. Installing the share library and applications locally is in many cases preferably.

Then build the application with meson as
```
meson -D<application>=true path/to/signed-video-framework-examples path/to/build/folder
meson install -C path/to/build/folder
```
Note that some applications may require additional environment variables set, for example, `GST_PLUGIN_PATH`; See, individual application README.md.

#### Example meson commands on Linux
The example commands also assume that both signed-video-framework and signed-video-framework-examples are located in the current directory.
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
Shorter mp4 recordings for testing can be found in [test-files/](./test-files/).

# License
[MIT License](./LICENSE)
