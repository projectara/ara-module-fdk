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
MODULE ?= module-skeleton
MODULE_PATH := $(CWD)/$(MODULE)
include $(MODULE_PATH)/module.mk

OUTPUTDIR := $(CWD)/build
OUTPUTBASE := $(OUTPUTDIR)/$(MODULE)
OUTPUTIMAGE := $(OUTPUTBASE)/image

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
BUILDNAME := $(MODULE)

SCRIPTPATH := $(CWD)/scripts

TOPDIR := $(NUTTX_ROOT)/nuttx
BUILDBASE := $(NUTTX_ROOT)/oot
IMAGEDIR := $(BUILDBASE)/$(BUILDNAME)/image

prepend-dir-to = $(addprefix $2/,$1)
prepend-dir = $(foreach d,$($1),$(call prepend-dir-to,$(d),$2))

OOT_CONFIG := $(call prepend-dir,config,$(MODULE_PATH))
OOT_BOARD := $(call prepend-dir,board-files,$(OUTPUTBASE))
OOT_MANIFEST := $(call prepend-dir,manifest,$(OUTPUTBASE))

# variables needed for $(SCRIPTPATH)/build.sh
export BUILDNAME
export BUILDBASE
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
tftf: copy_bin
	$(BOOTROM_TOOLS_ROOT)/create-tftf \
		--elf $(OUTPUTIMAGE)/nuttx.elf --outdir $(OUTPUTIMAGE) \
		--unipro-mfg 0x126 --unipro-pid 0x1000 --ara-stage 2 \
		--ara-vid $(vendor_id) --ara-pid $(product_id) \
		--start 0x`grep '\bReset_Handler$$' $(OUTPUTIMAGE)/System.map | cut -d ' ' -f 1`

copy_bin: mkoutput build_bin
	cp $(IMAGEDIR)/nuttx $(OUTPUTIMAGE)/nuttx.elf
	cp $(IMAGEDIR)/nuttx.bin $(IMAGEDIR)/System.map $(OUTPUTIMAGE)

mkoutput:
	mkdir -p $(OUTPUTIMAGE)

build_bin: yuck_init $(OOT_BOARD) $(OOT_MANIFEST)
	$(SCRIPTPATH)/build.sh

$(OUTPUTBASE)/%: $(MODULE_PATH)/%
	cp $< $@

# build_ara_image.sh (called in $(SCRIPTPATH)/build.sh) runs a distclean on
# Nuttx root directory before copying it in an empty $(BUILDNAME) in order to
# compile it. If Nuttx is clean, performing a distclean on it raises tons of
# warning and the only way to avoid that is to initialize a context in it
# (yuck!)
yuck_init:
	cp $(SCRIPTPATH)/Make.defs $(TOPDIR)
	cp $(OOT_CONFIG) $(TOPDIR)/.config
	$(MAKE) -C $(TOPDIR) context

# configuration rule
menuconfig:
	cp $(OOT_CONFIG) $(TOPDIR)/.config
	$(MAKE) -C $(TOPDIR) menuconfig
	cp $(TOPDIR)/.config $(OOT_CONFIG)

### ===
# es2 bootloader image
# FIXME: this only needed for ES2 chip and should be removed when ES3 is out
es2boot: mkoutput
	cd $(BOOTROM_ROOT) && ./configure es2tsb $(vendor_id) $(product_id)
	$(MAKE) -C $(BOOTROM_ROOT) OUTROOT=$(MODULE)
	cp $(BOOTROM_ROOT)/$(MODULE)/bootrom.bin $(OUTPUTIMAGE)
	truncate -s 2M $(OUTPUTIMAGE)/bootrom.bin

es2boot_clean:
	make -C $(BOOTROM_ROOT) clean OUTROOT=$(MODULE)
### ===


# init git submodules
submodule:
	git submodule init
	git submodule update

# cleaning rules
clean:
	rm -rf $(OUTPUTBASE)

distclean: clean es2boot_clean
	rm -rf $(OUTPUTDIR)
	$(MAKE) -C $(TOPDIR) apps_distclean
	$(MAKE) -C $(TOPDIR) distclean

.PHONY: all clean distclean submodule
ifndef VERBOSE
.SILENT:
endif
