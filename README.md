# Getting Started

1. Clone ARA Module FDK

    >$ git clone https://github.com/Fabo/ara-module-fdk.git

2. Go into the repository

    >$ cd ./ara-module-fdk

3. Load the configuration you want (e.g. skeleton_defconfig)

    >$ make skeleton_defconfig

4. Initialize the module repository

    >$ make init

    This command will download nuttx and manifesto, then initialize nuttx. This
    command should be run once before using the repository for the first time,
    or after running the "make distclean" command.

5. Build the module FW image

    >$ make

    Once built, you will find the following files in the root of the repository:

        1. nuttx.elf -> Firmware ELF image \
        2. nuttx.bin -> Firmware BIN image (raw binary) \
        3. System.map -> Map linking each function of the firmware to itsaddress.

# Developing a new module

{MODULE_NAME} should be replaced by the name of your module. No space
    is allowed in the module name.

* Rename the skeleton defconfig and board file to fit the purpose of your module.

    Steps:
    1. rename configs/skeleton_defconfig to configs/{MODULE_NAME}_defconfig
    2. rename the board file board-skeleton.c to board-{MODULE_NAME}.c
    3. update the Makefile to use your renamed board file:
        >-obj += board-skeleton.o \
        >+obj += board-{MODULE_NAME}.o

* Adding new C files to your build

    >obj += filename.o

    Add this in "Makefile". Note that the extension should be ".o" and not
    ".c" in the Makefile.

* Update the NuttX and Manifesto repository to get the latest upstream changes

    >$ make update

* Make changes to the configuration

    >$ make menuconfig

    In order to use make menuconfig, you need to have installed on your system
    the package Kconfig-frontends.

    To install it, read the section "Installing kconfig-frontends"

* Save a newer configuration file

    >$ cp .config configs/{MODULE_NAME}_defconfig

* Clean the repository

    >$ make distclean

# Installing kconfig-frontends

Run the following commands to install kconfig-frontends:

    1. wget http://ymorin.is-a-geek.org/download/kconfig-frontends/kconfig-frontends-3.12.0.0.tar.bz2
    2. tar xjvf kconfig-frontends-3.12.0.0.tar.bz2
    3. cd kconfig-frontends-3.12.0.0
    4. ./configure
    5. make
    6. sudo make install
