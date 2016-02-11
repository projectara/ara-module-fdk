#!/usr/bin/env python

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

import sys

def balance_vidpid(vidpid):
    # Taken from old python-version of create-tftf

    # Vendors have been told to use 15-bit MIDs, VIDs and PIDs, but the
    # bootrom requires Hamming-balanced values. The workaround is to copy
    # the 1s compliment of the lower 16 bits into the upper 16 bits.
    if vidpid != 0:
        if vidpid <= 0x00007fff:
            old_vidpid = vidpid
            vidpid = (vidpid | ~vidpid << 16) & 0xffffffff
        else:
            # 32-bit MID/VID/PID: Check for a valid Hamming weight
            if bin(vidpid).count('1') != 16:
                raise ValueError("{:#08x} has an invalid Hamming weight".
                               format(vidpid))
    return vidpid

def main():
    for arg in sys.argv[1:]:
        val = int(arg, 16)
        print("{:#08x} => {:#08x}".format(val, balance_vidpid(val)))

if __name__ == '__main__':
    main()
