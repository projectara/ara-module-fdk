#!/bin/bash
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

# define exit error codes
ARA_BUILD_CONFIG_ERR_BAD_PARAMS=1
ARA_BUILD_CONFIG_ERR_NO_NUTTX_TOPDIR=2
ARA_BUILD_CONFIG_ERR_CONFIG_NOT_FOUND=3
ARA_BUILD_CONFIG_ERR_CONFIG_COPY_FAILED=4

# Other build configuration.
ARA_MAKE_PARALLEL=1            # controls make's -j flag
ARA_MAKE_ALWAYS=""             # controls make's -B (--always-make) flag

build_image_from_defconfig() {
  # configpath, defconfigFile, buildbase
  # must be defined on entry

  echo "Build config file   : $defconfigFile"

  # define paths used during build process
  ARA_BUILD_CONFIG_PATH="$buildbase/config"
  ARA_BUILD_IMAGE_PATH="$buildbase/image"
  ARA_BUILD_TOPDIR="$buildbase"

  echo "Build output folder : $ARA_BUILD_TOPDIR"
  echo "Image output folder : $ARA_BUILD_IMAGE_PATH"

  # delete build tree if it already exists
  if [ -d $ARA_BUILD_TOPDIR ] ; then
    rm -rf $ARA_BUILD_TOPDIR
  fi

  # create folder structure in build output tree
  mkdir -p "$ARA_BUILD_CONFIG_PATH"
  mkdir -p "$ARA_BUILD_IMAGE_PATH"
  mkdir -p "$ARA_BUILD_TOPDIR"

  # Copy nuttx tree to build tree
  cp -r $NUTTX_ROOT/nuttx $ARA_BUILD_TOPDIR/nuttx
  cp -r $NUTTX_ROOT/apps $ARA_BUILD_TOPDIR/apps
  cp -r $NUTTX_ROOT/misc $ARA_BUILD_TOPDIR/misc

  pushd $ARA_BUILD_TOPDIR/nuttx > /dev/null

  make distclean

  # copy Make.defs to build output tree
  if ! install -m 644 -p ${configpath}/Make.defs ${ARA_BUILD_TOPDIR}/nuttx/Make.defs  >/dev/null 2>&1; then
      echo "Warning: Failed to copy Make.defs"
  fi

  # copy setenv.sh to build output tree
  if  install -p ${configpath}/setenv.sh ${ARA_BUILD_TOPDIR}/nuttx/setenv.sh >/dev/null 2>&1; then
  chmod 755 "${ARA_BUILD_TOPDIR}/nuttx/setenv.sh"
  fi

  # copy defconfig to build output tree
  if ! install -m 644 -p ${defconfigFile} ${ARA_BUILD_TOPDIR}/nuttx/.config ; then
      echo "ERROR: Failed to copy defconfig"
      exit $ARA_BUILD_CONFIG_ERR_CONFIG_COPY_FAILED
  fi

  # save config files
  cp ${ARA_BUILD_TOPDIR}/nuttx/.config   ${ARA_BUILD_CONFIG_PATH}/.config > /dev/null 2>&1
  cp ${ARA_BUILD_TOPDIR}/nuttx/Make.defs ${ARA_BUILD_CONFIG_PATH}/Make.defs > /dev/null 2>&1
  cp ${ARA_BUILD_TOPDIR}/nuttx/setenv.sh  ${ARA_BUILD_CONFIG_PATH}/setenv.sh > /dev/null 2>&1

  # make firmware
  make  -j ${ARA_MAKE_PARALLEL} ${ARA_MAKE_ALWAYS} -r -f Makefile.unix  2>&1 | tee $ARA_BUILD_TOPDIR/build.log

  MAKE_RESULT=${PIPESTATUS[0]}

  popd > /dev/null
}

copy_image_files() {
  echo "Copying image files"
  imgfiles="nuttx nuttx.bin System.map"
  for fn in $imgfiles; do
    cp $ARA_BUILD_TOPDIR/nuttx/$fn $ARA_BUILD_TOPDIR/image/$fn  >/dev/null 2>&1
    rm -f $ARA_BUILD_TOPDIR/nuttx/$fn >/dev/null 2>&1
  done

  # expand image to 2M using truncate utility
    truncate -s 2M $ARA_BUILD_TOPDIR/image/nuttx.bin
}

main() {
  defconfigFile=$OOT_CONFIG
  buildbase=$NUTTX_BUILDBASE
  configpath=$SCRIPTPATH

  cd $NUTTX_ROOT
  build_image_from_defconfig
  copy_image_files
  exit $MAKE_RESULT
}

main
