#!/bin/bash
# Copyright (c) 2016 Google, Inc.
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

# author: joel@porquet.org

# return the error code of the first failing commands in a pipeline of commands
set -o pipefail

### Global variables definition

# Get the path of this script (works even if this script is sourced and not
# executed, but won't work with symlink)
SCRIPT_PATH=$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd )

# default paths
FDK_DIR="${FDK_FDK_DIR:-fdk}"
NUTTX_DIR="${FDK_NUTTX_DIR:-nuttx}"
BOOTROM_DIR="${FDK_BOOTROM_DIR:-bootrom}"
BOOTROM_TOOLS_DIR="${FDK_BOOTROM_TOOLS_DIR:-bootrom-tools}"
MANIFESTO_DIR="${FDK_MANIFESTO_DIR:-manifesto}"

CROSS_COMPILE_DIR=${FDK_CROSS_COMPILE_DIR:-./arm-none-eabi-4.9/bin}

VERBOSITY=0
LOG_FILE=

BUILD_DIR_NAME="out"

CMD=
TARGETS=

DRY_RUN=false
KEEP_INTER=false

### Usage

# for developers: please keep the completion scripts up to date when you modify
# the usage of this script
function usage()
{
    local script_name=$(basename "${0}")
    cat <<-USAGE
Usage: ${script_name} [options] <command> [<target>...]

Description:
    This program is the front-end buildsystem of the Firmware Development Kit
    for Project ARA.

Options:

    -h              Print this help message
    -v              Increase verbosity
    -n              Dry-run (print the commands that would be executed)
    -b <dir>        Specify a build directory ('${BUILD_DIR_NAME}' by default)
    -k              Keep intermediary firmware binary files

Commands:

    clean           Remove the build directory corresponding to the specified
                    targets (without an argument, remove the entire build
                    directory)

    build           Build the firmware images corresponding to the specified
                    targets

    menuconfig      Configure the firmware correspoding to the specified
                    targets

    updateconfig    Update the firmware configuration corresponding to the
                    specified targets

Commands (internal development):

    s1boot          Generate the stage-1 bootloader image for the specified
                    targets

    s2boot          Generate the stage-2 bootloader image for the specified
                    targets (unimplemented)

    nuttx_configure Install the configuration files directly to NuttX directory
                    for in-tree incremental builds

Targets:

    A target is generally the path to a specific 'mk' file. For example:
        $ ./${script_name} menuconfig path/to/module.mk
        $ ./${script_name} build path/to/module.mk

    Multiple targets can be provided to the same command.

    For the commands 'build' and 'updateconfig', it is possible to specify one
    of the following special target:

        - all           Build all module and frame firmware images
        - all-frame     Build all frame firmware images
        - all-module    Build all module firmware images

Paths:

    The following paths are currently defined:
    FDK_NUTTX_DIR         = ${NUTTX_DIR}
    FDK_BOOTROM_DIR       = ${BOOTROM_DIR}
    FDK_BOOTROM_TOOLS_DIR = ${BOOTROM_TOOLS_DIR}
    FDK_MANIFESTO_DIR     = ${MANIFESTO_DIR}
USAGE
}

### Helpers

function die()
{
    echo "Error: $@" >&2
    if [[ -n ${LOG_FILE} ]]; then
        echo "Log of the failing build can be found in ${LOG_FILE}" >&2
    fi
    exit 1
}

function _run_log()
# 1: level, 2: echo command, [2:]: command
# returns the command's error code
{
    local level=${1}
    local echo_cmd=${2}
    shift 2

    if ${DRY_RUN}; then
        # print everything in the console
        ${echo_cmd} && echo "${@}"
    elif [[ ${level} -lt 0 ]]; then
        # special case where we don't wanting logging because we need to
        # interact with the command
        ${echo_cmd} && echo "${@}"
        ${@}
    elif [[ ${VERBOSITY} -ge ${level} ]]; then
        if [[ -z ${LOG_FILE} ]]; then
            # no log file, print in the console
            ${echo_cmd} && echo "${@}"
            ${@}
        else
            # print both in console and log file, if verbosity is high enough
            ${echo_cmd} && echo "${@}" | tee -a ${LOG_FILE}
            ${@} 2>&1 | tee -a ${LOG_FILE}
        fi
    else
        if [[ -z ${LOG_FILE} ]]; then
            # no log file, don't print but execute
            ${@} > /dev/null
        else
            # print only in log file
            ${echo_cmd} && echo "${@}" | tee -a ${LOG_FILE} > /dev/null
            ${@} 2>&1 | tee -a ${LOG_FILE} > /dev/null
        fi
    fi

    return ${?}
}

function run_log()
# 1: level, [2:]: command
# returns the command's error code
{
    local level=${1}
    shift
    _run_log ${level} true ${@}
}

function echo_log()
# 1: level, [2:]: args to echo
{
    local level=${1}
    shift
    _run_log ${level} false echo ${@}
}

function list_contains()
# 1: item to look up, 2: list
# returns 0 if item is in list, 1 otherwise
{
    local e
    for e in "${@:2}"; do
        [[ "${e}" == "${1}" ]] && return 0
    done
    return 1
}

### Commands

function _check_command()
# 1: name of command
# returns 0 if command exists, 1 otherwise
{
    local command=("clean" "build" "menuconfig" "updateconfig" "s1boot" "s2boot"
    "nuttx_configure")
    list_contains "${1}" "${command[@]}" && return 0 || return 1
}

function check_command()
# 1: name of command
{
    _check_command ${1} || die "Command does not exist: ${1}"
}

### Target files

function check_target_special()
# 1: name of target
# returns 0 if special target, 1 otherwise (eg path to mk target file)
{
    local target_special=("all" "all-frame" "all-module")
    list_contains "${1}" "${target_special[@]}" && return 0 || return 1
}

function _check_target_mk()
# 1: path of mk target file
# returns 0 if target file is readable, 1 otherwise
{
    [[ -r ${1} ]] && return 0 || return 1
}

function check_target_mk()
# 1: path of mk target file
{
    _check_target_mk ${1} || \
        die "File target does not exist or is not accessible: ${1}"
}

function get_var_mk()
# 1: name of variable to get from mk target file
# returns the exit status of the make command
{
    TARGET=${TARGET} make --quiet -f - ${1}.var <<'EOF'
include $(TARGET)
%.var:
	@echo $($*)
EOF
    return ${?}
}

function read_target_mk()
{
    echo_log 1 "# Reading ${TARGET}"

    [[ ! -r ${TARGET} ]] && die "Cannot read target file: ${TARGET}"

    get_var_mk "dummy" > /dev/null || die "File target is misformed: ${TARGET}"

    CONFIG_FILE=$(get_var_mk "config")
    MANIFEST_FILE=$(get_var_mk "manifest")
    BOARD_FILES=($(get_var_mk "board-files"))
    VENDOR_ID=$(get_var_mk "vendor_id")
    PRODUCT_ID=$(get_var_mk "product_id")
    PRODUCT_ID_ES2=$(get_var_mk "product_id_es2")
    TYPE=($(get_var_mk "type"))
    VERSION=($(get_var_mk "version"))
    FRAME=$(get_var_mk "frame")

    echo_log 1 "CONFIG_FILE=${CONFIG_FILE}"
    echo_log 1 "MANIFEST_FILE=${MANIFEST_FILE}"
    echo_log 1 "BOARD_FILES=${BOARD_FILES[@]}"
    echo_log 1 "VENDOR_ID=${VENDOR_ID}"
    echo_log 1 "PRODUCT_ID=${PRODUCT_ID}"
    echo_log 1 "PRODUCT_ID_ES2=${PRODUCT_ID_ES2}"
    echo_log 1 "TYPE=${TYPE[@]}"
    echo_log 1 "VERSION=${VERSION[@]}"
    echo_log 1 "FRAME=${FRAME}"
}

### Build dir management

function _mk_dir()
# 1: path of directory to create
{
    run_log 2 mkdir -p "${1}" || die "Cannot create directory: ${1}"
}

function _rm_dir()
# 1: path of directory to remove
{
    run_log 2 rm -rf "${1}" || die "Cannot remove directory: ${1}"
}

function echo_remove_dir()
# 1: path of directory to remove
{
    [[ ! -e "${1}" ]] && return
    echo_log 1 "# Removing ${1}"
    _rm_dir "${1}"
}

function build_topdir()
# 1: true for creating, false for removing
{
    if ${1}; then
        _mk_dir "${BUILD_DIR_NAME}"
    else
        echo_remove_dir "${BUILD_DIR_NAME}"
    fi
}

function build_topdir_target()
# 1: true for creating, false for removing
{
    BUILD_DIR_PATH="${BUILD_DIR_NAME}/${TARGET_BASE}"
    if ${1}; then
        _mk_dir "${BUILD_DIR_PATH}"
    else
        echo_remove_dir "${BUILD_DIR_PATH}"
    fi
}

function echo_build_dir()
# 1: path of directory to remove and create
{
    echo_remove_dir "${1}"
    echo_log 1 "# Creating ${1}"
    _mk_dir "${1}"
}

### Bootrom tools

function _bootrom_tools_compile()
# 1: bootrom component to compile
{
    local bootrom_srcdir=$(cd ${BOOTROM_DIR} >/dev/null && pwd)
    local bootrom_tools_srcdir=$(cd ${BOOTROM_TOOLS_DIR} >/dev/null && pwd)
    run_log 2 make BOOTROM_SRCDIR=${bootrom_srcdir} \
        TOPDIR=${bootrom_tools_srcdir} \
        COMMONDIR=${bootrom_tools_srcdir}/src/common \
        -C ${bootrom_tools_srcdir}/src/${1} || \
        die "Could not compile ${bootrom_tools_srcdir}/src/${1}"
}

function bootrom_tools_compile_create_tftf()
{
    echo_log 1 "# Compiling create-tftf"

    _bootrom_tools_compile "common"
    _bootrom_tools_compile "create-tftf"
}

function bootrom_tools_compile_create_ffff()
{
    echo_log 1 "# Compiling create-ffff"

    _bootrom_tools_compile "common"
    _bootrom_tools_compile "create-ffff"
}

### NuttX

function nuttx_configure()
# 1: path of the nuttx directory to be configured
{
    echo_log 1 "# Installing Make.defs"
    run_log 2 install -m 644 "${FDK_DIR}/scripts/Make.defs" "${1}/nuttx/Make.defs" || \
        die "Could not install Make.defs"
    echo_log 1 "# Installing ${CONFIG_FILE}"
    run_log 2 install -m 644 "${CONFIG_FILE[@]/#/${TARGET_BASE}/}" "${1}/nuttx/.config" || \
        die "Could not install ${CONFIG_FILE}"
}

function nuttx_prepare()
{
    # Create directory hierarchy
    BUILD_DIR_NUTTX="${BUILD_DIR_PATH}/nuttx"
    echo_build_dir "${BUILD_DIR_NUTTX}"

    BUILD_DIR_OUT="${BUILD_DIR_PATH}/out"
    echo_build_dir "${BUILD_DIR_OUT}"

    if [[ ! -z ${MANIFEST_FILE} ]]; then
        BUILD_DIR_MANIFEST="${BUILD_DIR_PATH}/manifest"
        echo_build_dir "${BUILD_DIR_MANIFEST}"
    fi
    if [[ ! -z ${BOARD_FILES} ]]; then
        BUILD_DIR_MODULE="${BUILD_DIR_PATH}/module"
        echo_build_dir "${BUILD_DIR_MODULE}"
    fi

    # Copy NuttX source code and user-specified code
    echo_log 1 "# Copying NuttX files"
    run_log 2 cp -r ${NUTTX_DIR}/nuttx ${BUILD_DIR_NUTTX}/nuttx || \
        die "Cannot copy ${NUTTX_DIR}/nuttx directory"
    run_log 2 cp -r ${NUTTX_DIR}/apps ${BUILD_DIR_NUTTX}/apps || \
        die "Cannot copy ${NUTTX_DIR}/apps directory"
    run_log 2 cp -r ${NUTTX_DIR}/misc ${BUILD_DIR_NUTTX}/misc || \
        die "Cannot copy ${NUTTX_DIR}/misc directory"

    if [[ ! -z ${MANIFEST_FILE} ]]; then
        echo_log 1 "# Copying manifest file"
        run_log 2 cp "${MANIFEST_FILE/#/${TARGET_BASE}/}" ${BUILD_DIR_MANIFEST} || \
            die "Cannot copy manifest file"
    fi
    if [[ ! -z ${BOARD_FILES} ]]; then
        echo_log 1 "# Copying board-specific files"
        run_log 2 cp "${BOARD_FILES[@]/#/${TARGET_BASE}/}" ${BUILD_DIR_MODULE} || \
            die "Cannot boards-specific files"
    fi

    # Clean NuttX
    echo_log 1 "# Cleaning NuttX directory"
    local make_args=""
    if [[ ${VERBOSITY} -gt 2 ]]; then
        make_args="V=1"
    fi
    run_log 2 make -C ${BUILD_DIR_NUTTX}/nuttx ${make_args} distclean || \
        die "NuttX cleaning failed"

    # Install the various files necessary for building
    nuttx_configure "${BUILD_DIR_NUTTX}"
}

function nuttx_clean()
{
    # Remove directory hierarchy
    echo_remove_dir "${BUILD_DIR_NUTTX}"
    echo_remove_dir "${BUILD_DIR_OUT}"
    if [[ ! -z ${MANIFEST_FILE} ]]; then
        echo_remove_dir "${BUILD_DIR_MANIFEST}"
    fi
    if [[ ! -z ${BOARD_FILES} ]]; then
        echo_remove_dir "${BUILD_DIR_MODULE}"
    fi
}

function _export_cross_compile()
{
    local cross_compile=$(cd ${CROSS_COMPILE_DIR} >/dev/null && pwd)
    export PATH=${cross_compile}:${PATH}
}

function _export_manifesto()
{
    local manifesto_path=$(cd ${MANIFESTO_DIR} >/dev/null && pwd)
    export PATH=${manifesto_path}:${PATH}
}

function _export_versions()
{
    git -C ${NUTTX_DIR} rev-parse --is-inside-work-tree >/dev/null 2>&1 || \
        die "${NUTTX_DIR} is not a git working directory"
    git -C ${FDK_DIR} rev-parse --is-inside-work-tree >/dev/null 2>&1 || \
        die "${FDK_DIR} is not a git working directory"

    local nuttx_version=$(git -C ${NUTTX_DIR} describe --long --dirty --tags --abbrev=10)
    local fdk_version=$(git -C ${FDK_DIR} describe --long --dirty --tags --abbrev=10)

    export OOT_NUTTX_VERSION_STR=${nuttx_version}
    export OOT_FDK_VERSION_STR=${fdk_version}
}

function _export_build_name()
{
    export OOT_BUILD_NAME="${TARGET}"
}

function _nuttx_export_vars()
{
    _export_cross_compile
    _export_manifesto
    _export_versions
    _export_build_name

    if [[ ! -z ${MANIFEST_FILE} ]]; then
        local pwd_manifest=$(cd ${BUILD_DIR_MANIFEST} >/dev/null && pwd)
        export OOT_MANIFEST="${MANIFEST_FILE/#/${pwd_manifest}/}"
    fi
    if [[ ! -z ${BOARD_FILES} ]]; then
        local pwd_module=$(cd ${BUILD_DIR_MODULE} >/dev/null && pwd)
        export OOT_BOARD="${BOARD_FILES[@]/#/${pwd_module}/}"
    fi
}

function nuttx_make()
{
    echo_log 1 "# Making NuttX"
    _nuttx_export_vars
    local make_args=""
    if [[ ${VERBOSITY} -gt 2 ]]; then
        make_args="V=1"
    fi
    run_log 2 make -C ${BUILD_DIR_NUTTX}/nuttx -r -f Makefile.unix ${make_args} || \
        die "NuttX compilation failed"
}

function _nuttx_get_config()
{
    echo_log 1 "# Retrieving back ${CONFIG_FILE}"
    run_log 2 cp ${BUILD_DIR_NUTTX}/nuttx/.config "${CONFIG_FILE[@]/#/${TARGET_BASE}/}"  || \
        die "Could not retrieve back ${CONFIG_FILE}"
}

function nuttx_menuconfig()
{
    echo_log 1 "# Configuring NuttX"
    _nuttx_export_vars
    run_log -1 make -C ${BUILD_DIR_NUTTX}/nuttx -r -f Makefile.unix menuconfig || \
        die "NuttX configuration failed"
    _nuttx_get_config
}

function nuttx_updateconfig()
{
    echo_log 1 "# Updating NuttX configuration"
    _nuttx_export_vars
    run_log 2 make -C ${BUILD_DIR_NUTTX}/nuttx -r -f Makefile.unix olddefconfig || \
        die "NuttX configuration update failed"
    _nuttx_get_config
}

function nuttx_buildconfig()
{
    echo_log 0 "## Building buildconfig"
    run_log 2 make -C nuttx/nuttx -r -f Makefile.unix buildconfig || \
        die "NuttX buildconfig failed"
}

### Image creation

function check_fw_type()
{
    if [[ -z ${TYPE} ]]; then
        # if no type was defined, let's assume gpb
        TYPE=("gpb")
        echo_log 1 "Warning: 'type' was not defined, assuming '${TYPE}'"
    else
        local fw_type=("svc" "apb" "gpb")
        local fwt
        for fwt in "${TYPE[@]}"; do
            [[ "${fwt}" == "svc" && ${#TYPE[@]} -gt 1 ]] && \
                die "A firmware cannot target SVC and other firmware types"
            list_contains "${fwt}" "${fw_type[@]}" || return 1
        done
    fi
    return 0
}

function check_fw_version()
{
    if [[ -z ${VERSION} ]]; then
        # if no version was defined, generate only es3
        VERSION=("es3")
        echo_log 1 "Warning: 'version' was not defined, assuming '${VERSION[@]}'"
    else
        list_contains "svc" "${TYPE[@]}" && \
            echo_log 1 "Warning: firmware versions do not apply for SVC firmware"

        local fw_version=("es2" "es3")
        local fwv
        for fwv in "${VERSION[@]}"; do
            list_contains "${fwv}" "${fw_version[@]}" || return 1
        done
    fi
    return 0
}

function nuttx_copy_binary()
# 1: path of source nuttx binary
# 2: path of destination nuttx binary
{
    echo_log 1 "# Copying NuttX binary ${1} output to ${2}"
    run_log 2 cp "${BUILD_DIR_NUTTX}/nuttx/${1}" "${2}" || \
        die "Cannot copy NuttX binary ${1} output to ${2}"
}

function bootrom_copy_binary()
# 1: path of source bootrom binary
# 2: path of destination bootrom binary
{
    echo_log 1 "# Copying Bootrom binary ${1} output to ${2}"
    run_log 2 cp "${BUILD_DIR_BOOTROM}/build/${1}" "${2}" || \
        die "Cannot copy Bootrom binary ${1} output to ${2}"
}

function rm_binary_file()
# 1: the binary file to remove
{
    if ! ${KEEP_INTER}; then
        run_log 2 rm ${1} || die "Could not remove temporary file ${1}"
    fi
}

function truncate_binary_file()
# 1: path of binary file to truncate
{
    echo_log 1 "# Truncating ${1} to 2M"
    run_log 2 truncate -s 2M "${1}" || \
        die "Cannot truncate ${1}"
}

TOSHIBA_MIPI_MID="0x0126"

function _create_nuttx_tftf()
# 1: path of input nuttx binary
# 2: path of output tftf binary
# 3: unipro PID value
# 4: extra arguments to the create-tftf command
{
    echo_log 1 "# Create TFTF image from ${1} to ${2}"
    run_log 2 ${BOOTROM_TOOLS_DIR}/bin/create-tftf \
        --verbose \
        --type s2fw \
        --name "${TARGET_NAME}" \
        --elf "${1}" \
        --out "${2}" \
        --unipro-mfg "${TOSHIBA_MIPI_MID}" \
        --unipro-pid "${3}" \
        --start-sym Reset_Handler \
        ${4} \
        || die "TFTF image creation failed"
}

function _create_nuttx_ffff()
# 1: path of input tftf binary
# 2: path of output ffff binary
{
    echo_log 1 "# Create FFFF image from ${1} to ${2}"
    run_log 2 ${BOOTROM_TOOLS_DIR}/bin/create-ffff \
        --verbose \
        --name "${TARGET_NAME}" \
        --header-size 0x1000 \
        --generation 1  \
        --flash-capacity 0x40000 \
        --image-length 0x40000 \
        --erase-size 0x800 \
        --s2f ${1} --eloc 0x2000 --egen 1 \
        --out ${2} \
        || die "FFFF image creation failed"
}

function create_nuttx_tftf_module()
# 1: path of input nuttx binary
# 2: reference to path of output tftf binary (unset, gets defined in the function)
{
    local unipro_pid=
    local extra_arg=
    local vendor_id product_id

    [[ -z ${VENDOR_ID} ]] && die "A vendor_id should be defined for this target"
    vendor_id=${VENDOR_ID}

    if [[ "${VERSION_CUR}" == "es2" ]]; then
        # *PBridge ES2
        [[ -z ${PRODUCT_ID_ES2} ]] && die "A product_id_es2 should be defined for this target"
        product_id=${PRODUCT_ID_ES2}
        unipro_pid="0x1000"
        extra_arg="--no-hamming-balance"
        vendor_id=$((${vendor_id} & 0xffff))
        product_id=$((${product_id} & 0xffff))
    else
        # ES3
        [[ -z ${PRODUCT_ID} ]] && die "A product_id should be defined for this target"
        product_id=${PRODUCT_ID}
        if [[ "${TYPE_CUR}" == "apb" ]]; then
            # APBridge ES3
            unipro_pid="0x1001"
        else
            # GPBridge ES3
            unipro_pid="0x1002"
        fi
    fi

    # craft the filename for the tftf image and put it back in $2
    local tftf_filename=$(printf "ara_%.8x_%.8x_%.8x_%.8x_02.tftf" \
        ${TOSHIBA_MIPI_MID} ${unipro_pid} ${vendor_id} ${product_id})
    printf -v "${2}" '%s' "${BUILD_DIR_OUT}/${tftf_filename}"

    _create_nuttx_tftf "${1}" "${!2}" "${unipro_pid}" \
        "${extra_arg} --ara-vid ${vendor_id} --ara-pid ${product_id}"
}

function create_nuttx_ffff_module()
# 1: path of input nuttx binary
# 2: reference to path of output ffff binary (unset, gets defined in the function)
# 3: path to existing tftf binary
{
    # craft the filename for the ffff image from the tftf filename
    local ffff_filename="$(basename ${3} .tftf).ffff"
    printf -v "${2}" '%s' "${BUILD_DIR_OUT}/${ffff_filename}"

    _create_nuttx_ffff "${nuttx_tftf}" "${!2}"
}

function create_nuttx_ffff_frame()
# 1: path of input nuttx binary
# 2: path of output ffff binary
{
    # Frame FFFF files are necessarily APBs
    local unipro_pid="0x1001"
    local nuttx_tftf="${BUILD_DIR_OUT}/nuttx.tftf"

    _create_nuttx_tftf "${1}" "${nuttx_tftf}" "${unipro_pid}" ""
    _create_nuttx_ffff "${nuttx_tftf}" "${2}"

    rm_binary_file "${nuttx_tftf}"
}

function image_congrats()
# 1: Type of image
# 2: path of output image
{
    echo_log 0 "# Congratulations! ${1} firmware is available in ${2}"
}

function nuttx_image_create()
{
    # Depending on the type of firmware (SVC, APBridgeA, APBridgeE and
    # GPBridge) and the version of the chip (ES2 vs ES3), the images are
    # different
    # Frame:
    #   SVC: always a bin file (not truncated)
    #   APBridgeA: bin file on ES2, FFFF file on ES3 (type apb)
    #   APB2: bin file on ES2, FFFF file on ES3 (type apb)
    # Module:
    #   APBridgeE: TFTF file on ES2, TFTF and FFFF file on ES3 (type apb)
    #   GPBridge: TFTF file on ES2, TFTF and FFFF file on ES3 (type gpb)

    # Creating the proper binary images
    echo_log 1 "# Creating binary images"

    check_fw_type || \
        die "The specified firmware types are incorrect: ${TYPE[@]}"
    check_fw_version || \
        die "The specified firmware versions are incorrect: ${VERSION[@]}"

    if [[ ! -z "${FRAME}" ]]; then
        # Frame firmware (SVC, APB1, APB2)
        TYPE_CUR="${TYPE[0]}"
        case "${TYPE_CUR}" in
            svc)
                local nuttx_bin="${BUILD_DIR_OUT}/nuttx-${TARGET_NAME}.bin"
                nuttx_copy_binary "nuttx.bin" "${nuttx_bin}"
                image_congrats "SVC" "${nuttx_bin}"
                ;;
            apb)
                # Firmware APBridge firmware (ES2, ES3)
                for VERSION_CUR in ${VERSION[@]}; do
                    case "${VERSION_CUR}" in
                        es2)
                            local
                            nuttx_bin="${BUILD_DIR_OUT}/nuttx-${TARGET_NAME}-${VERSION_CUR}.bin"
                            nuttx_copy_binary "nuttx.bin" "${nuttx_bin}"
                            truncate_binary_file "${nuttx_bin}"
                            image_congrats "Frame APB" "${nuttx_bin}"
                            ;;
                        es3)
                            local nuttx_elf="${BUILD_DIR_OUT}/nuttx.elf"
                            local nuttx_ffff="${BUILD_DIR_OUT}/nuttx-${TARGET_NAME}-${VERSION_CUR}.ffff"
                            nuttx_copy_binary "nuttx" "${nuttx_elf}"
                            bootrom_tools_compile_create_tftf
                            bootrom_tools_compile_create_ffff
                            create_nuttx_ffff_frame "${nuttx_elf}" "${nuttx_ffff}"
                            truncate_binary_file "${nuttx_ffff}"
                            rm_binary_file "${nuttx_elf}"
                            image_congrats "Frame APB" "${nuttx_ffff}"
                            ;;
                    esac
                done
                ;;
        esac
    else
        # For all the module target, we need the elf version of compiled NuttX
        local nuttx_elf="${BUILD_DIR_OUT}/nuttx.elf"
        local nuttx_tftf nuttx_ffff
        nuttx_copy_binary "nuttx" "${nuttx_elf}"

        # Module firmware (ES2, ES3)
        for VERSION_CUR in ${VERSION[@]}; do
            case "${VERSION_CUR}" in
                es2)
                    for TYPE_CUR in ${TYPE[@]}; do
                        bootrom_tools_compile_create_tftf
                        create_nuttx_tftf_module "${nuttx_elf}" nuttx_tftf
                        image_congrats "Module ES2" "${nuttx_tftf}"
                        nuttx_tftf=
                    done
                    ;;
                es3)
                    for TYPE_CUR in ${TYPE[@]}; do
                        bootrom_tools_compile_create_tftf
                        bootrom_tools_compile_create_ffff
                        create_nuttx_tftf_module "${nuttx_elf}" nuttx_tftf
                        image_congrats "Module ES3" "${nuttx_tftf}"
                        create_nuttx_ffff_module "${nuttx_elf}" nuttx_ffff "${nuttx_tftf}"
                        image_congrats "Module ES3" "${nuttx_ffff}"
                        nuttx_ffff=
                        nuttx_tftf=
                    done
                    ;;
            esac
        done

        rm_binary_file "${nuttx_elf}"
    fi
}

### Bootrom

function bootrom_prepare()
{
    # Create directory hierarchy
    BUILD_DIR_BOOTROM="${BUILD_DIR_PATH}/bootrom"
    echo_remove_dir "${BUILD_DIR_BOOTROM}"

    BUILD_DIR_OUT="${BUILD_DIR_PATH}/out"
    echo_build_dir "${BUILD_DIR_OUT}"

    # Copy Bootrom source code
    echo_log 1 "# Copying Bootrom files"
    run_log 2 cp -r ${BOOTROM_DIR} ${BUILD_DIR_BOOTROM} || \
        die "Cannot copy ${BOOTROM_DIR} directory to ${BUILD_DIR_BOOTROM}"

    # Clean bootrom
    echo_log 1 "# Cleaning Bootrom directory"
    run_log 2 make -C ${BUILD_DIR_BOOTROM} distclean || \
        die "Bootrom cleaning failed"
}

function bootrom_make()
{
    echo_log 1 "# Making Bootrom"
    _export_cross_compile
    _export_manifesto
    run_log 2 ${BUILD_DIR_BOOTROM}/configure es2tsb "${VENDOR_ID}" "${PRODUCT_ID}" || \
        die "Bootrom configuration failed"
    run_log 2 make -C ${BUILD_DIR_BOOTROM} || \
        die "Bootrom compilation failed"
    return
}

function bootrom_image_create()
{
    # Creating the proper binary images
    echo_log 1 "# Creating binary images"

    local bootrom_bin="${BUILD_DIR_OUT}/bootrom.bin"
    bootrom_copy_binary "bootrom.bin" "${bootrom_bin}"
    truncate_binary_file "${bootrom_bin}"
    image_congrats "Bootrom" "${bootrom_bin}"
}

### Commands

function _cmd_nuttx()
# 1: list of functions to pre-call
# 2: action to print
# 3: list of functions to post-call
{
    local pre_fn=(${!1})
    local post_fn=(${!3})
    local fn

    # pre-call functions
    for fn in "${pre_fn[@]}"; do
        ${fn}
    done

    # print message
    echo_log 0 "## ${2} firmware ${TARGET}"

    # common actions
    build_topdir_target true
    # Cannot use a log file before the build directory is created
    LOG_FILE="${BUILD_DIR_PATH}/build.log"
    read_target_mk
    nuttx_prepare

    # post-call functions
    for fn in ${post_fn[@]}; do
        ${fn}
    done

    # Reset the log file for this build
    LOG_FILE=
}

function cmd_clean()
{
    echo_log 0 "## Cleaning ${TARGET}"
    build_topdir_target false
}

function cmd_build()
{
    local pre_fn=()
    local post_fn=(nuttx_make nuttx_image_create)
    _cmd_nuttx pre_fn[@] "Building" post_fn[@]
}

function cmd_menuconfig()
{
    local pre_fn=(nuttx_buildconfig)
    local post_fn=(nuttx_menuconfig nuttx_clean)
    _cmd_nuttx pre_fn[@] "Menuconfig" post_fn[@]
}

function cmd_updateconfig()
{
    local pre_fn=(nuttx_buildconfig)
    local post_fn=(nuttx_updateconfig nuttx_clean)
    _cmd_nuttx pre_fn[@] "Updateconfig" post_fn[@]
}

function cmd_s1boot()
{
    echo_log 0 "## Building s1boot for ${TARGET}"
    build_topdir_target true
    # Cannot use a log file before the build directory is created
    LOG_FILE="${BUILD_DIR_PATH}/build.log"
    read_target_mk
    bootrom_prepare
    bootrom_make
    bootrom_image_create
    # Reset the log file for this build
    LOG_FILE=
}

function cmd_nuttx_configure()
{
    echo_log 0 "## Configuring in-source Nuttx ${TARGET}"
    read_target_mk
    nuttx_configure "${NUTTX_DIR}"
}

### Command line management

function parse_cmdline()
{
    # parse the options first
    while getopts ":hvb:nj:k" arg; do
        case ${arg} in
            h)
                usage | more -df >&2
                exit 0
                ;;
            v)
                VERBOSITY=$((VERBOSITY+1))
                ;;
            b)
                BUILD_DIR_NAME=${OPTARG}
                ;;
            n)
                DRY_RUN=true
                ;;
            k)
                KEEP_INTER=true
                ;;
            :)
                die "Option -${OPTARG} requires an argument"
                ;;
            \?)
                die "Invalid option: -${OPTARG}"
                ;;
        esac
    done
    shift $((OPTIND - 1))

    # parse command
    [[ ${#} -lt 1 ]] && die "Expecting at least a command..."
    local cmd="${1}"
    shift
    check_command ${cmd}
    CMD=${cmd}

    # special command without a target
    if [[ "${CMD}" == "clean" && ${#} -lt 1 ]]; then
        build_topdir false
        return
    fi

    # parse targets
    TARGETS=()
    while [[ ${#} -gt 0 ]]; do
        local target="${1}"
        shift
        if check_target_special ${target}; then
            if [[ "${target}" == "all" || "${target}" == "all-module" ]]; then
                local module_targets=($(find ${FDK_DIR}/module -name "module.mk"))
                TARGETS=(${TARGETS[@]} ${module_targets[@]})
            fi
            if [[ "${target}" == "all" || "${target}" == "all-frame" ]]; then
                local frame_targets=($(find ${FDK_DIR}/frame -name "frame.mk"))
                TARGETS=(${TARGETS[@]} ${frame_targets[@]})
            fi
            echo_log 0 "### Building: [${frame_targets[@]}]"
        else
            check_target_mk ${target}
            TARGETS=(${TARGETS[@]} ${target})
        fi
    done
    [[ -z ${TARGETS[@]} ]] && die "Expecting at least a target..."
}

function run_command()
{
    for TARGET in "${TARGETS[@]}"; do
        TARGET_BASE=$(dirname "${TARGET}")
        TARGET_NAME=$(basename "${TARGET_BASE}")
        case ${CMD} in
            clean)
                cmd_clean
                ;;
            build)
                cmd_build
                ;;
            menuconfig)
                cmd_menuconfig
                ;;
            updateconfig)
                cmd_updateconfig
                ;;
            s1boot)
                cmd_s1boot
                ;;
            s2boot)
                die "Unimplemented"
                ;;
            nuttx_configure)
                cmd_nuttx_configure
                ;;
        esac
    done
}

# If no arguments were given, let's show the usage and exit
[[ ${#} -le 0 ]] && { usage; exit 0; }

# Parse the command line
parse_cmdline "$@"

# Run the command with the arguments that were given
run_command
