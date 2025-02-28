# fastrpc_test

This repository contains a test application (`fastrpc_test.c`) that demonstrates the usage of FastRPC (Fast Remote Procedure Call) to offload computations to different DSP (Digital Signal Processor) domains on a Qualcomm device. The test application uses a simple calculator service to compute the sum and maximum of an array of integers.

## Pushing Files to the Device

After building the application, the following files need to be pushed to the device:

1. **fastrpc_test Binary**: The compiled test application.
2. **libcalculator.so**: The shared library implementing the calculator service.
3. **libcalculator_skel.so**: The skeleton library for the calculator service.

Use the following ADB commands to push the files to the device:

```bash
# Create the necessary directories on the device
adb shell mkdir -p /usr/bin/
adb shell mkdir -p /usr/lib/
adb shell mkdir -p /usr/lib/rfsa/sdk

# Push the fastrpc_test binary
adb push /path/to/your/fastrpc/test/.libs/fastrpc_test /usr/bin/

# Set executable permissions for the fastrpc_test binary
adb shell chmod 777 /usr/bin/fastrpc_test

# Push the libcalculator.so library
adb push /path/to/your/fastrpc/test/calculator/libcalculator.so /usr/lib/

# Push the libcalculator_skel.so library
adb push /path/to/your/fastrpc/test/calculator/libcalculator_skel.so /usr/lib/rfsa/sdk/
```

## Running the Test

The test application can be run on the device using the following command:

```bash
adb shell "export LD_LIBRARY_PATH=/usr/lib/ DSP_LIBRARY_PATH=/usr/lib/rfsa/sdk; /usr/bin/fastrpc_test -d 3 -U 1"
```
### Command Options

- `-d domain`: Specifies the DSP domain to run the test on. Valid values are:
  - `0`: ADSP (Application DSP)
  - `1`: MDSP (Modem DSP)
  - `2`: SDSP (Sensor DSP)
  - `3`: CDSP (Compute DSP)
  - Default: `3` (CDSP)

- `-U unsigned_PD`: Specifies whether to run the test on a signed or unsigned protection domain. Valid values are:
  - `0`: Signed PD
  - `1`: Unsigned PD
  - Default: `1`