#!/bin/sh

set -e

here=$(cd `dirname $0`; pwd -P)
echo $here
cd $here/../../

# Set up Windows-specific build configuration
if [ ! -f "bindings/python/setup.cfg" ]; then
    echo "Setting up Windows build configuration..."
    (cd bindings/python && echo '[build_ext]' > setup.cfg && echo 'compiler = mingw32' >> setup.cfg)
fi

# Export default environment for mingw64 if not already set
export CONFIG_SITE=${CONFIG_SITE:-/etc/config.site}
export PREFIX=${PREFIX:-/mingw64}

autoreconf -i
./configure --disable-php \
            --disable-fortran \
            --disable-perl \
            --disable-matlab \
            --disable-idl \
            --disable-cplusplus \
            --enable-ansi-c \
            --enable-modules \
            --host=x86_64-w64-mingw32 \
            --with-python=python.exe \
            ac_cv_header_numpy_arrayobject_h=yes \
            --with-pcre=$PREFIX \
            --with-ltdl=$PREFIX \
            --with-liblzma=$PREFIX \
            --with-libFLAC=$PREFIX \
            "$@"
