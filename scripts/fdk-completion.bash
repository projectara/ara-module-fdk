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

# bash completion for fdk
# author: joel@porquet.org

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

function _fdk()
{
    local cur prev opts cmds

    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD - 1]}"

    opts="-h -v -n -b -k"
    cmds=("clean" "build" "menuconfig" "updateconfig" "s1boot" "nuttx_configure")
    specials=("all" "all-frame" "all-module")

    # options
    case "${prev}" in
        -h)
            return
            ;;
        -b)
            COMPREPLY=($(compgen -d -- ${cur}))
            return
            ;;
    esac

    if [[ "${cur}" == -* ]]; then
        COMPREPLY=($(compgen -W "${opts}" -- "${cur}"))
        return
    fi

    # filenames
    if list_contains "${prev}" "${cmds[@]}" || \
        list_contains "${prev}" "${specials}" || \
        [[ "${prev}" == *.mk ]]; then
        compopt -o filenames 2>/dev/null # adds / at the end of the directories
        COMPREPLY=($(compgen -d -- ${cur}) \
            $(compgen -f -X '!*.mk' -- ${cur}) \
            $(compgen -W "${specials[*]}" -- ${cur}))
        return
    fi

    # commands
    COMPREPLY=($(compgen -W "${cmds[*]}" -- "${cur}"))

}

complete -F _fdk ./build-fdk.sh
