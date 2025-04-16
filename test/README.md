# fastrpc_test

This folder contains a test application (`fastrpc_test.c`) that demonstrates the usage of FastRPC (Fast Remote Procedure Call) to offload computations to different DSP (Digital Signal Processor) domains. The test application supports multiple examples, including a simple calculator service, a HAP example, and a multithreading example.

## Pushing Files to the Device

After building the application, the following files and directories need to be pushed to the device:

1. **fastrpc_test Binary**: The compiled test application.
2. **android Directory**: Contains shared libraries for the Android platform.
3. **linux Directory**: Contains shared libraries for the Linux platform.
4. **v68 Directory**: Contains skeletons for the v68 architecture version.
5. **v75 Directory**: Contains skeletons for the v75 architecture version.

Copy the following files and directories to `/usr/bin` on the target device:

- `/path/to/your/fastrpc/test/.libs/fastrpc_test`
- `/path/to/your/fastrpc/test/android` (if using Android)
- `/path/to/your/fastrpc/test/linux` (if using Linux)
- `/path/to/your/fastrpc/test/v68`
- `/path/to/your/fastrpc/test/v75`

**Note**: Push the `android` directory if you are using the Android platform, and the `linux` directory if you are using the Linux platform.

## Running the Test

To run the test application, follow these steps:

1. Navigate to the `/usr/bin` directory on the device.
2. Execute the `fastrpc_test` binary with the appropriate options.

Example command:

```bash
cd /usr/bin
./fastrpc_test -d 3 -U 1 -t linux -a v68
```

### Options

- `-d domain`: Run on a specific domain.
  - `0`: Run the example on ADSP
  - `1`: Run the example on MDSP
  - `2`: Run the example on SDSP
  - `3`: Run the example on CDSP
  - **Default Value**: `3` (CDSP) for targets having CDSP.

- `-U unsigned_PD`: Run on signed or unsigned PD.
  - `0`: Run on signed PD.
  - `1`: Run on unsigned PD.
  - **Default Value**: `1`

- `-t target`: Specify the target platform (android or linux).
  - **Default Value**: `linux`

- `-a arch_version`: Specify the architecture version (e.g., v68, v75).
  - **Default Value**: `v68`