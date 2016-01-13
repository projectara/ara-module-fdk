# Getting Started

1. Clone the ARA Module FDK

    >$ git clone https://github.com/projectara/ara-module-fdk.git

2. Go into the directory

    >$ cd ./ara-module-fdk

3. Switch to stable release

    By default, the FDK will be initialized with the *development branch*.  
    The latest tagged release is `mdk-v0.4.0`. To switch to a stable release:

    >$ git checkout mdk-0.4.0

4. Initialize the submodules

    >$ make submodule

    This command will download compatible versions of the submodules
    [bootrom](https://github.com/projectara/bootrom),
    [bootrom-tools](https://github.com/projectara/bootrom-tools),
    [manifest](https://github.com/projectara/manifesto) and
    [nuttx](https://github.com/projectara/nuttx).

5. Build the default skeleton module

    >$ make

    Once built, you will find the following file in the newly generated
    `build/module-examples/skeleton/tftf` directory:

    * `ara:00000126:00001000:00000000:00000000:02.tftf`: a signed TFTF image

# Developing a new module

`{MODULE_NAME}` should be replaced by the name of your module. *No space is
allowed in the module name.*

1. Copy the skeleton module directory
    >$ cp -r module-examples/skeleton {MODULE_NAME}

2. Optionally add new C files for your module support and update the
   `modules.mk` accordingly

    >board-files += new_c_file.c

3. Optionally make changes to the configuration file

    >$ make MODULE={MODULE_NAME} menuconfig

    In order to use make menuconfig, you need to have installed on your system
    the package Kconfig-frontends (*see next section*).

4. Compile

    >$ make MODULE={MODULE_NAME}

5. The following generated files are in `build/{MODULE_NAME}/nuttx_build/image`
 and can be flashed to the GPBridge

 * `nuttx.elf`: Firmware ELF image
 * `nuttx.bin`: Firmware BIN image (raw binary) extended to 2M
 * `System.map`: Map linking each function of the firmware to its

In order to clean the repository, there are two possible commands:

* `make clean`: deletes `build/{MODULE_NAME}`
* `make distclean`: deletes the whole `build` directory

Tips: to avoid having to define the variable `MODULE` each time you run the
Makefile, you can edit the main Makefile and directly update `MODULE` with the
name of your module.

# Boot-over-Unipro

1. In your module directory, edit the `module.mk` file and update the
   `vendor_id` and `product_id` with appropriate values (you can keep the
   default value of `0x00000000` if unsure).

2. Build the S1 bootloader image:

    >$ make MODULE={MODULE_NAME} es2boot

3. Flash the resulting image located at
 `build/{MODULE_NAME}/bootrom/bootrom.bin`
   on the module using the Dediprog SF-100.

    >$ flashrom --programmer dediprog -w build/{MODULE_NAME}/bootrom/bootrom.bin

4. Build the TFTF firmware image:

    >$ make MODULE={MODULE_NAME} tftf

5. Copy the resulting image located at
   `build/{MODULE_NAME}/tftf/ara:....:02.tftf` to the Android filesystem:

    >$ adb push build/{MODULE_NAME}/tftf/*.tftf /data/firmware

    You might have to create `/data/firmware` before on the Android filesystem:
```
$ adb shell
Android$ su
Android$ mkdir -p /data/firmware
Android$ chmod 777 /data/firmware
```

6. If everything goes fine, the firmware image should be automatically
   transfered to the module after the module is hot-plugged.

# Installing kconfig-frontends

Run the following commands in order to install `kconfig-frontends`:

```
wget http://ymorin.is-a-geek.org/download/kconfig-frontends/kconfig-frontends-3.12.0.0.tar.bz2
tar xjvf kconfig-frontends-3.12.0.0.tar.bz2
cd kconfig-frontends-3.12.0.0
./configure
make
sudo make install
```

# Switching to development branch

To use the latest unstable version of the FDK and submodules:

```
$ git checkout master
$ git submodule update --remote
```
