# Getting Started

1. Clone the ARA Module FDK

    >$ git clone https://github.com/projectara/ara-module-fdk.git

2. Go into the directory

    >$ cd ./ara-module-fdk

3. Initialize the submodules

    >$ make submodule

    This command will download the submodules
    [bootrom](https://github.com/projectara/bootrom),
    [bootrom-tools](https://github.com/projectara/bootrom-tools),
    [manifest](https://github.com/projectara/manifesto) and
    [nuttx](https://github.com/projectara/nuttx).

4. Build the default skeleton module

    >$ make

    Once built, you will find the following files in the newly generated
    `output-skeleton` directory:

    * `nuttx.elf`: Firmware ELF image
    * `nuttx.bin`: Firmware BIN image (raw binary) extended to 2M
    * `System.map`: Map linking each function of the firmware to its address

# Developing a new module

`{MODULE_NAME}` should be replaced by the name of your module. *No space is
allowed in the module name.*

1. Copy the skeleton module directory
    >$ cp -r module-skeleton module-{MODULE_NAME}

2. Optionally add new C files for your module support and update the
   `modules.mk` accordingly

    >board-files += new_c_file.c

3. Optionally make changes to the configuration file

    >$ make MODULE={MODULE_NAME} menuconfig

    In order to use make menuconfig, you need to have installed on your system
    the package Kconfig-frontends (*see next section*).

4. Compile

    >$ make MODULE={MODULE_NAME}

5. The generated files are in `output-{MODULE_NAME}` and can be flashed to the
   GPBridge

In order to clean the repository, there are two possible commands:

* `make clean`: deletes the generated files of the FDK
* `make distclean`: also cleans the NuttX repository

Tips: to avoid having to define the variable `MODULE` each time you run the
Makefile, you can edit the main Makefile and directly update `MODULE` with the
name of your module.

# Boot-over-Unipro

1. In your module directory, edit the `module.mk` file and update the
   `vendor_id` and `product_id` with appropriate values (you can keep the
   default value of `0x00000000` if unsure).

2. Build the S1 bootloader image:

    >$ make MODULE={MODULE_NAME} es2boot

3. Flash the resulting image located at `output-{MODULE_NAME}/bootrom.bin` on
   the module using the Dediprog SF-100.

    >$ flashrom --programmer dediprog -w output-{MODULE_NAME}/bootrom.bin

4. Build the TFTF firmware image:

    >$ make MODULE={MODULE_NAME} tftf

5. Copy the resulting image located at `output-{MODULE_NAME}/ara:....:02.tftf`
   to the Android filesystem:

    >$ adb push output-{MODULE_NAME}/*.tftf /data/firmware

You might have to create `/data/firmware` before on the Android filesystem:

    >$ adb shell
    >Android$ su
    >Android$ mkdir -p /data/firmware
    >Android$ chmod 777 /data/firmware

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

