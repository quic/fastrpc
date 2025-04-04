# Document for Running QNN Models

## For Linux Host (Setup)

### Step 1: Install Qualcomm AI Engine Direct (QNN SDK)

1. Download the QNN SDK:
	* Go to the QNN SDK product [page](https://www.qualcomm.com/developer/software/qualcomm-ai-engine-direct-sdk).
	* Click **Get Software** to download the QNN SDK.
	* Unzip the downloaded SDK.
2. Set Up Your Environment:
	* Open a terminal.
	* Navigate to `qairt/<QNN_SDK_VERSION>` inside the unzipped QNN SDK.
	* Run the following commands to set up the environment:
		+ `cd bin`
		+ `source ./envsetup.sh`
	* This will automatically set the environment variables `QNN_SDK_ROOT` and `SNPE_ROOT`.
	* To save this for future bash terminals, run:
		+ `echo 'export QNN_SDK_ROOT="${QNN_SDK_ROOT}"' >> ~/.bashrc`
3. Check Dependencies:
    * Run:
    	+ `sudo ${QNN_SDK_ROOT}/bin/check-linux-dependency.sh`
    * When all necessary dependencies are installed, the script will display “Done!!”.
4. Verify Toolchain Installation:
    * Run:
        + `${QNN_SDK_ROOT}/bin/envcheck -c`
    * This will verify that the required toolchain is installed successfully.

### Step 2: Install QNN SDK Dependencies

1. Install Python 3.10:
	* Run the following commands:
		+ `sudo apt-get update && sudo apt-get install python3.10 python3-distutils libpython3.10`
	* Verify the installation by running:
		+ `python3 --version`
2. Navigate to QNN SDK Root:
	* Run:
		+ `cd ${QNN_SDK_ROOT}`
3. Create and Activate a Virtual Environment:
	* Run the following commands (you may rename `MY_ENV_NAME` to any name you prefer):
		+ `python3 -m venv MY_ENV_NAME`
		+ `source MY_ENV_NAME/bin/activate`
4. Update Dependencies:
	* Run:
		+ `python "${QNN_SDK_ROOT}/bin/check-python-dependency"`

### Step 3: Install Model Frameworks

1. Identify Relevant Frameworks:
	* QNN supports multiple model frameworks. Install only the ones relevant for the AI model files you want to use.
	* **Warning:** You do not need to install all packages listed here.
2. Note:
	* You can install a package by running:
		+ `pip3 install package==version`
	* Example Installations:
		+ `pip3 install torch==1.13.1`
		+ `pip3 install tensorflow==2.10.1`
		+ `pip3 install tflite==2.3.0`
		+ `pip3 install torchvision==0.14.1`
		+ `pip3 install onnx==1.12.0`
		+ `pip3 install onnxruntime==1.17.1`
		+ `pip3 install onnxsim==0.4.36`

### Step 4: Install Target Device OS-Specific Toolchain Code

#### Working with an Android Target Device:

1. Install Android NDK:
	* Download the [Android NDK r26c](https://dl.google.com/android/repository/android-ndk-r26c-linux.zip).
	* Unzip the file.
2. Set Up Environment Variables:
	* Open a terminal.
	* Replace `<path-to-your-android-ndk-root-folder>` with the path to the unzipped `android-ndk-r26c` folder, then run:
		+ `echo 'export ANDROID_NDK_ROOT="<path-to-your-android-ndk-root-folder>"' >> ~/.bashrc`
	* Add the location of the unzipped file to your PATH by running:
		+ `echo 'export PATH="${ANDROID_NDK_ROOT}:${PATH}"' >> ~/.bashrc`
		+ `source ~/.bashrc`
3. Verify Environment Variables:
	* Run:
		+ `${QNN_SDK_ROOT}/bin/envcheck -n`

#### Working with a Linux Target Device:

1. Verify Clang++14 Installation:
	* For Linux target devices, you will likely need to use clang++14 to build models for them using the QNN SDK.
	* You can verify if you have clang++14 by running:
		+ `${QNN_SDK_ROOT}/bin/envcheck -c`
2. Set Up Cross Compiler (for **Linux Embedded**):
	* Open a new terminal
	* Follow these steps to set up the cross compiler:
		+ `wget https://artifacts.codelinaro.org/artifactory/qli-ci/flashable-binaries/qimpsdk/qcm6490/x86/qcom-6.6.28-QLI.1.1-Ver.1.1_qim-product-sdk-1.1.3.zip`
		+ `unzip qcom-6.6.28-QLI.1.1-Ver.1.1_qim-product-sdk-1.1.3.zip`
		+ `umask a+rx` 
        + `cd /path/to/unzip/target/qcm6490/sdk`
		+ `sh qcom-wayland-x86_64-qcom-multimedia-image-armv8-2a-qcm6490-toolchain-ext-1.0.sh`
		+ `export ESDK_ROOT=<path of installation directory>`
		+ `cd $ESDK_ROOT`
		+ `source environment-setup-armv8-2a-qcom-linux`

## CNN to QNN for Linux Host

### Step 1: Set Up The Example TensorFlow Model

1. Verify TensorFlow Installation:
	* Run:
		+ `python3 -m pip show tensorflow`
2. Set the TENSORFLOW_HOME Environment Variable:
	* Run the following commands:
		```
        tensorflowLocation=$(python -m pip show tensorflow | grep '^Location: ' | awk '{print $2}')
		export TENSORFLOW_HOME="$tensorflowLocation/tensorflow"
		echo "export TENSORFLOW_HOME=$tensorflowLocation/tensorflow" >> ~/.bashrc 
        ```
3. Verify Environment Variable:
	* Run:
		+ `${TENSORFLOW_HOME}`
4. Create Test Data:
	* Run:
		+ `python3 ${QNN_SDK_ROOT}/examples/Models/InceptionV3/scripts/setup_inceptionv3.py -a ~/tmpdir -d`
	* This will create the test data for this tutorial:
		+ Model file at: `${QNN_SDK_ROOT}/examples/Models/InceptionV3/tensorflow/inception_v3_2016_08_28_frozen.pb`
		+ Raw images at: `${QNN_SDK_ROOT}/examples/Models/InceptionV3/data/cropped`

### Step 2: Convert the CNN Model into a QNN Model

1. Quantized Model Conversion:
	* To use a quantized model, pass in the `--input_list` flag to specify the input.
	* Run the following command:
		```
        python ${QNN_SDK_ROOT}/bin/x86_64-linux-clang/qnn-tensorflow-converter \
		--input_network "${QNN_SDK_ROOT}/examples/Models/InceptionV3/tensorflow/inception_v3_2016_08_28_frozen.pb" \
		--input_dim input 1,299,299,3 \
		--input_list "${QNN_SDK_ROOT}/examples/Models/InceptionV3/data/cropped/raw_list.txt" \
		--out_node "InceptionV3/Predictions/Reshape_1" \
		--output_path "${QNN_SDK_ROOT}/examples/Models/InceptionV3/model/Inception_v3.cpp"
        ```
2. Flags Explanation:
	* `--input_network`: Path to the source framework model.
	* `--input_dim`: Input name followed by dimensions.
	* `--input_list`: Absolute path to input data (required for quantization).
	* `--out_node`: Name of the graph’s output Tensor Names.
	* `--output_path`: Indicates where to put the output files.
3. Artifacts Produced:
	* `Inception_v3.cpp`: Contains the sequence of API calls.
	* `Inception_v3.bin`: Static data associated with the model.
	* `Inception_v3_net.json`: Describes the model data in a standardized manner (not needed to build the QNN model later on).
4. Location of Artifacts:
	* You can find the artifacts in the `${QNN_SDK_ROOT}/examples/Models/InceptionV3/model` directory.

### Other Model Types:
If you are using another type of model, you can refer to the [Tools page](https://docs.qualcomm.com/bundle/publicresource/topics/80-63442-50/tools.html) for a table of potential scripts to help convert them into QNN format. These scripts will have a similar qnn-model-type-converter naming convention.

### Step 3: Model Build on Linux Host

1. Choose Target Architecture:
	* Select the most relevant supported target architecture from the following list:
		+ 64-bit Linux targets: `x86_64-linux-clang`
		+ 64-bit Android devices: `aarch64-android`
		+ Qualcomm’s QNX OS: `aarch64-qnx` (Note: This architecture is not supported by default in the QNN SDK.)
		+ OpenEmbedded Linux (GCC 11.2): `aarch64-oe-linux-gcc11.2`
		+ OpenEmbedded Linux (GCC 9.3): `aarch64-oe-linux-gcc9.3`
		+ OpenEmbedded Linux (GCC 8.2): `aarch64-oe-linux-gcc8.2`
		+ Ubuntu Linux (GCC 9.4): `aarch64-ubuntu-gcc9.4`
		+ Ubuntu Linux (GCC 7.5): `aarch64-ubuntu-gcc7.5`
2. Set Target Architecture:
	* On your host machine, set the target architecture of your target device by running:
		+ `export QNN_TARGET_ARCH="your-target-architecture-from-above"`
	* For **Android**:
		+ `export QNN_TARGET_ARCH="aarch64-android"`
	* For **Linux Embedded**:
		+ `export QNN_TARGET_ARCH="aarch64-oe-linux-gcc11.2"`
3. Generate Model Library:
	* Run the following command on your host machine:
		```
        python3 "${QNN_SDK_ROOT}/bin/x86_64-linux-clang/qnn-model-lib-generator" \
		-c "${QNN_SDK_ROOT}/examples/Models/InceptionV3/model/Inception_v3.cpp" \
		-b "${QNN_SDK_ROOT}/examples/Models/InceptionV3/model/Inception_v3.bin" \
		-o "${QNN_SDK_ROOT}/examples/Models/InceptionV3/model_libs" \
		-t ${QNN_TARGET_ARCH}
        ```
	* `-c`: Path to the `.cpp` QNN model file.
	* `-b`: Path to the `.bin` QNN model file (optional but recommended).
	* `-o`: Path to the output folder.
	* `-t`: Target architecture to build for.
4. Verify Output File:
	* Run:
		+ `ls ${QNN_SDK_ROOT}/examples/Models/InceptionV3/model_libs/${QNN_TARGET_ARCH}`
	* Verify that the output file `libInception_v3.so` is inside. This file will be used on the target device to execute inferences. The output `.so` file will be located in the `model_libs` directory, named according to the target architecture.

### Step 4: Model Build on Linux Host

Users can set custom options and different performance modes to the HTP Backend through the backend config. Follow these steps to create the necessary JSON files:

1. Create Backend Config File (`be.json`):
	* For **Android** with the following content:
        ```json
        {
            "graphs": [
                {
                    "graph_names": [
                        "Inception_v3"
                    ],
                    "vtcm_mb": 8
                }
            ],
            "devices": [
                {
                    "htp_arch": "v79"
                }
            ]
        }
        ```
	* For **Linux Embedded** with the following content:
        ```json
        {
            "graphs": [
                {
                    "graph_names": [
                        "Inception_v3"
                    ],
                    "vtcm_mb": 8
                }
            ],
            "devices": [
                {
                    "htp_arch": "v73"
                }
            ]
        }
        ```
2. Create Backend Extensions Config File (`config.json`):
	* The config file with minimum parameters specified through JSON is given below:
		```json
        {
            "backend_extensions": {
                "shared_library_path": "./libQnnHtpNetRunExtensions.so",
                "config_file_path": "./be.json"
            }
        }
        ```
3. Place JSON Files in the Model Libraries Path:
	* Ensure that the `be.json` and `config.json` files are located in the following path:
		+ `${QNN_SDK_ROOT}/examples/Models/InceptionV3/model_libs/${QNN_TARGET_ARCH}`

### Step 5: Prepare the Target Device

1. Open a Terminal on the Target Device:
	* Run the following commands to create a destination folder and set the necessary permissions:
		```
        adb shell "mkdir -p /data/local/tmp"
		adb shell "ln -s /etc/ /data/local/tmp"
		adb shell "chmod -R 777 /data/local/tmp"
		adb shell "mkdir -p /data/local/tmp/inception_v3"
        ```
2. Push Necessary Files:
	* Push `libQnnHtp.so` and other necessary executables, input data, and input list from your host machine to `/data/local/tmp/inception_v3` on the target device:
		```
        adb push \path\to\qairt\2.32.0.250228\lib\${QNN_TARGET_ARCH} /data/local/tmp/inception_v3
		adb shell "cd /data/local/tmp/inception_v3; mv ./${QNN_TARGET_ARCH}/* ./; rmdir ${QNN_TARGET_ARCH}"
        ```
	* For **Android**:
		+ `adb push \path\to\qairt\2.32.0.250228\lib\hexagon-v79\unsigned /data/local/tmp/inception_v3`
	* For **Linux Embedded**:
        + `adb push \path\to\qairt\2.32.0.250228\lib\hexagon-v73\unsigned /data/local/tmp/inception_v3`
		
    * Continue remaining steps on the target device:
        ```
        adb shell "cd /data/local/tmp/inception_v3; mv ./unsigned/* ./; rmdir unsigned"
        adb push \path\to\qairt\2.32.0.250228\examples\Models\InceptionV3\model_libs\${QNN_TARGET_ARCH}\ /data/local/tmp/inception_v3
        adb shell "cd /data/local/tmp/inception_v3; mv ./${QNN_TARGET_ARCH}/* ./; rmdir ${QNN_TARGET_ARCH}"
        ```
        ```
        adb push \path\to\qairt\2.32.0.250228\examples\Models\InceptionV3\data\cropped /data/local/tmp/inception_v3
        adb push \path\to\qairt\2.32.0.250228\examples\Models\InceptionV3\data\target_raw_list.txt /data/local/tmp/inception_v3
        adb push \path\to\qairt\2.32.0.250228\bin\${QNN_TARGET_ARCH}\qnn-net-run /data/local/tmp/inception_v3
        adb shell "chmod 777 /data/local/tmp/inception_v3/qnn-net-run"
        ```
3. Run the Model & Pull Output Directory to Host Machine:
	* On the target device, run the following command to execute the model:
		```
        adb shell "LD_LIBRARY_PATH=/data/local/tmp/inception_v3 DSP_LIBRARY_PATH=/data/local/tmp/inception_v3; cd /data/local/tmp/inception_v3; ./qnn-net-run --model ./libInception_v3.so --input_list ./target_raw_list.txt --backend ./libQnnHtp.so --output_dir ./output --config_file ./config.json"
        ```
	* Run the following command to pull the output directory from the target device to your host machine:
	    + `adb pull /data/local/tmp/inception_v3/output \path\to\qairt\2.32.0.250228\examples\Models\InceptionV3`

### Step 6: View Classification Result

1. Navigate to the Example Directory on Host Machine:
	* Run:
		+ `cd ${QNN_SDK_ROOT}/examples/Models/InceptionV3`
2. Run the Script to View Classification Results:
	* Execute the following command to view the classification results:
	    ```
        python3 "./scripts/show_inceptionv3_classifications.py" \
		-i "./data/cropped/raw_list.txt" \
		-o "./output" \
		-l "./data/imagenet_slim_labels.txt"
        ```
	* Ensure that the classification results in the output match the following:
		```
        ${QNN_SDK_ROOT}/examples/Models/InceptionV3/data/cropped/notice_sign.raw 0.152344 459 brass
		${QNN_SDK_ROOT}/examples/Models/InceptionV3/data/cropped/chairs.raw      0.281250 832 studio couch
		${QNN_SDK_ROOT}/examples/Models/InceptionV3/data/cropped/trash_bin.raw   0.800781 413 ashcan
		${QNN_SDK_ROOT}/examples/Models/InceptionV3/data/cropped/plastic_cup.raw 0.988281 648 measuring cup
        ```

### References

* [Linux Setup](https://docs.qualcomm.com/bundle/publicresource/topics/80-63442-50/linux_setup.html)
* [CNN to QNN for Linux Host](https://docs.qualcomm.com/bundle/publicresource/topics/80-63442-50/qnn_tutorial_linux_host.html)
* [CNN to QNN for Linux Host on Linux Target](https://docs.qualcomm.com/bundle/publicresource/topics/80-63442-50/qnn_tutorial_linux_host_linux_target.html)