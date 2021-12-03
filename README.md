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

## Example files
Shorter mp4 recordings for testing can be found in [test-files/](./test-files/).

# License
[MIT License](./LICENSE)
