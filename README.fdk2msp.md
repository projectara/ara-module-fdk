# FDK to MSP

The fdk2msp scripts is meant to generate installable Module Support Packages
from built firmware sources.

# Prerequisites

The following utilities needs to be in your path:

1. `aapt` to generate an unsigned & unaligned .apk
2. `jarsigner` to sign the unaligned .apk
3. `zipalign` to optimize the signed .apk

All three needs to be in your path. The script also expects that the `aapt` utility will be inside an Android SDK tree and will use the path to the utility in order to find the `android.jar` file corresponding to version 23 of the Android build tools. 

In all case, the script checks for the expected prerequisites and will complain if they are not available.

# Usage

The fdk2msp script is a Python 2.7 script, which is present in most Linux
distribution. Before you run the script, you might need to install the
`python-cheetah` package which is the template library that was used to make
the templates.

To generate an .apk from a built firmware, run the script this way:

./fd2kmsp build/module-examples/skeleton

And the script will generate and sign an .apk conforming to the current proposed
format. The text files used to generate the manifests are generated in the
`manifests` folder for each package generate. This directory can be removed and 
should not be included in repositories.

# Extra configuration

The following extra fields needs to be added to `module.mk` for the generation
of the Ara manifest.

1. `package` The fully qualified name of the package for the generated .apk. That name must not include dash.
2. `name` The actual name of the project. This will be used to create the directory in the `manifests` temporary directory.  
3. cert_sar/cert_fcc/cert_ce: Corresponds to 3 different certification a module can have (CE, FCC and SAR). Those fields should be set to `true` or `false`. Their values are copied inside the Ara manifest file.
