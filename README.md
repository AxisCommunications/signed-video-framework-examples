# What is Signed Video Framework?
The *Signed Video Framework* is an open-source project that secures an H264, or H265, video from tampering by adding cryptographic signatures in SEI frames. See the [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework) for more details.

## Getting started with the repo
This repository contains a set of application examples which aims to enrich the developers implementation experience. All examples are using the [signed-video-framework](https://github.com/AxisCommunications/signed-video-framework) and has a README file in its directory with instructions on how to build and run it.

## Example applications
Below is a list of example applications available in the repository.
- signer
  - The example code implements a gStreamer element that process every NAL and adds SEI NALs to the stream repeatedly.
- validator
  - The example code implements a gstAppSink that process every NAL and validates the authenticity repeatedly.

# License
[MIT License](./LICENSE)
