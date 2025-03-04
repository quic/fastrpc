# fastrpc_test

This repository contains a test application (`fastrpc_test.c`) that demonstrates the usage of FastRPC (Fast Remote Procedure Call) to offload computations to different DSP (Digital Signal Processor) domains on a Qualcomm device. The test application supports multiple examples, including a simple calculator service, a HAP example, and a multithreading example.

## Pushing Files to the Device

After building the application, the following files need to be pushed to the device:

1. **fastrpc_test Binary**: The compiled test application.
2. **libcalculator.so**: The shared library implementing the calculator service.
3. **libcalculator_skel.so**: The skeleton library for the calculator service.
4. **libhap_example.so**: The shared library implementing the HAP example.
5. **libhap_example_skel.so**: The skeleton library for the HAP example.
6. **libmultithreading.so**: The shared library implementing the multithreading example.
7. **libmultithreading_skel.so**: The skeleton library for the multithreading example.

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

# Push the libhap_example.so library
adb push /path/to/your/fastrpc/test/hap/libhap_example.so /usr/lib/

# Push the libhap_example_skel.so library
adb push /path/to/your/fastrpc/test/hap/libhap_example_skel.so /usr/lib/rfsa/sdk/

# Push the libmultithreading.so library
adb push /path/to/your/fastrpc/test/multithreading/libmultithreading.so /usr/lib/

# Push the libmultithreading_skel.so library
adb push /path/to/your/fastrpc/test/multithreading/libmultithreading_skel.so /usr/lib/rfsa/sdk/
```

## Running the Test

The test application can be run on the device using the following command:

```bash
adb shell "export LD_LIBRARY_PATH=/usr/lib/ DSP_LIBRARY_PATH=/usr/lib/rfsa/sdk; /usr/bin/fastrpc_test -d 3 -U 1 -e 0"
```

### Command Options

- `-d domain`: Specifies the DSP domain to run the test on. Valid values are:
  - `0`: ADSP
  - `1`: MDSP
  - `2`: SDSP
  - `3`: CDSP
  - Default: `3` (CDSP)

- `-U unsigned_PD`: Specifies whether to run the test on a signed or unsigned protection domain. Valid values are:
  - `0`: Signed PD
  - `1`: Unsigned PD
  - Default: `1`

- `-e example`: Specifies the example to run. Valid values are:
  - `0`: Run the calculator example.
  - `1`: Run the HAP example.
  - `2`: Run the multithreading example.
  - Default: `0` (calculator)