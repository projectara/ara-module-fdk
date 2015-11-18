# Getting Started

1. Clone the ARA Module FDK

    >$ git clone https://github.com/projectara/ara-module-fdk.git

2. Go into the directory

    >$ cd ./ara-module-fdk

3. Initialize the submodules

    >$ make submodule

    This command will download the submodules Bootrom-tools, Manifesto and
    NuttX. *Note that this command should be run whenever the submodules need to
    be updated.*

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

4. Update the Bootrom-tools, Manifesto and NuttX repositories to get the latest
   upstream changes

    >$ make submodule

5. Compile

    >$ make MODULE={MODULE_NAME}

6. The generated files are in `output-{MODULE_NAME}` and can be flashed to the
   GPBridge

In order to clean the repository, there are two possible commands:

* `make clean`: deletes the generated files of the FDK
* `make distclean`: also cleans the NuttX repository

Tips: to avoid having to define the variable `MODULE` each time you run the
Makefile, you can edit the main Makefile and directly update `MODULE` with the
name of your module.

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

