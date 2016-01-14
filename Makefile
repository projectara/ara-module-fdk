# Copyright (c) 2015 Google, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Author: Fabien Parent <fparent@baylibre.com>
# Author: Joel Porquet <joel@porquet.org>

CWD := $(shell pwd)

# make the module directory easily overriden
MODULE ?= module-examples/skeleton
MODULE_PATH := $(CWD)/$(MODULE)
include $(MODULE_PATH)/module.mk

# make externals easily overriden
TOOLS_NAME = tools
TOOLS_ROOT = $(CWD)/$(TOOLS_NAME)
BOOTROM_TOOLS_ROOT ?= $(TOOLS_ROOT)/bootrom-tools
MANIFESTO_ROOT ?= $(TOOLS_ROOT)/manifesto

FW_NAME = firmware
FW_ROOT = $(CWD)/$(FW_NAME)
BOOTROM_ROOT ?= $(FW_ROOT)/bootrom
NUTTX_ROOT ?= $(FW_ROOT)/nuttx

# prepare NuttX
SCRIPTPATH := $(CWD)/scripts

TOPDIR := $(NUTTX_ROOT)/nuttx
BUILDDIR := $(CWD)/build
BUILDBASE := $(BUILDDIR)/$(MODULE)
NUTTX_BUILDBASE := $(BUILDBASE)/nuttx_build
BOOTROM_BUILDBASE := $(BUILDBASE)/bootrom
TFTFDIR := $(BUILDBASE)/tftf

prepend-dir-to = $(addprefix $2/,$1)
prepend-dir = $(foreach d,$($1),$(call prepend-dir-to,$(d),$2))

OOT_CONFIG := $(call prepend-dir,config,$(MODULE_PATH))
OOT_BOARD := $(call prepend-dir,board-files,$(BUILDBASE))
OOT_MANIFEST := $(call prepend-dir,manifest,$(BUILDBASE))

# variables needed for $(SCRIPTPATH)/build.sh
export NUTTX_BUILDBASE
export OOT_CONFIG
export NUTTX_ROOT
export SCRIPTPATH

# variables needed when compiling the firmware image
export PATH:=$(MANIFESTO_ROOT):$(PATH)
export OOT_BOARD
export OOT_MANIFEST

# building rules
all: tftf

# trusted firmware generation
tftf: build_bin
	echo "creating tftf image at: $(TFTFDIR)"

	# rename nuttx to nuttx.elf for create-tftf tool
	mv $(NUTTX_BUILDBASE)/image/nuttx \
		$(NUTTX_BUILDBASE)/image/nuttx.elf

	# run create-tftf
	$(BOOTROM_TOOLS_ROOT)/create-tftf \
		--elf $(NUTTX_BUILDBASE)/image/nuttx.elf \
		--outdir $(TFTFDIR) \
		--unipro-mfg 0x126 \
		--unipro-pid 0x1000 \
		--ara-stage 2 \
		--ara-vid $(vendor_id) \
		--ara-pid $(product_id) \
		--no-hamming-balance \
		--start 0x`grep '\bReset_Handler$$' $(NUTTX_BUILDBASE)/image/System.map | cut -d ' ' -f 1`

tftf_mkoutput:
	echo "creating tftf output directory: $(TFTFDIR)"
	mkdir -p $(TFTFDIR)

cp_source: tftf_mkoutput
	echo "copying module source to build directory: $(BUILDBASE)"
	cp -r $(MODULE_PATH)/* $(BUILDBASE)

build_bin: cp_source
	echo "starting firmware build"
	$(SCRIPTPATH)/build.sh

# configuration rules
menuconfig:
	# copy config file to nuttx folder, run menuconfig rule, copy back
	cp $(OOT_CONFIG) $(TOPDIR)/.config
	$(MAKE) -C $(TOPDIR) menuconfig
	cp $(TOPDIR)/.config $(OOT_CONFIG)

updateconfig:
	# copy config file to nuttx folder, run menuconfig rule, copy back
	cp $(OOT_CONFIG) $(TOPDIR)/.config
	$(MAKE) -C $(TOPDIR) olddefconfig > /dev/null 2>&1
	cp $(TOPDIR)/.config $(OOT_CONFIG)

### ===
# es2 bootloader image
# FIXME: this only needed for ES2 chip and should be removed when ES3 is out
es2_mkoutput:
	echo "creating bootrom output directory: $(BOOTROM_BUILDBASE)"
	mkdir -p $(BOOTROM_BUILDBASE)

es2boot: es2_mkoutput
	echo "building bootrom.bin in $(BOOTROM_BUILDBASE)"
	cd $(BOOTROM_ROOT) && ./configure es2tsb $(vendor_id) $(product_id)
	$(MAKE) -C $(BOOTROM_ROOT) OUTROOT=$(BOOTROM_BUILDBASE)
	truncate -s 2M $(BOOTROM_BUILDBASE)/bootrom.bin

es2boot_clean:
	echo "removing: $(BOOTROM_BUILDBASE)"
	rm -rf $(BOOTROM_BUILDBASE)
### ===


# init git submodules
submodule:
	echo "fetching git submodules"
	git submodule init
	git submodule update

# build all modules
ALL_MODULES_DIR=$(wildcard module-examples/*)
ALL_MODULES_BUILD=$(addsuffix -allbuild,$(ALL_MODULES_DIR))
build-all: $(ALL_MODULES_BUILD)

%-allbuild: %
	$(MAKE) MODULE=$<

# cleaning rules
clean:
	echo "removing build directory: $(BUILDBASE)"
	rm -rf $(BUILDBASE)

distclean: clean es2boot_clean
	echo "removing: $(BUILDDIR)"
	rm -rf $(BUILDDIR)

.PHONY: all clean distclean submodule
ifndef VERBOSE
.SILENT:
endif
