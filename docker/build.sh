#!/bin/bash
#
# WineASIO Wine 11 Docker Build Script
#

cd $(dirname $0)

# Clean old files
rm -f *.dll *.so

set -e

echo "Building WineASIO for Wine 11..."

# Build Docker image
docker build -t wineasio-wine11 .

# Extract built files
docker run -v $PWD:/mnt --rm --entrypoint \
    cp wineasio-wine11:latest \
        /wineasio/build_wine11/wineasio64.dll \
        /wineasio/build_wine11/wineasio64.so \
        /wineasio/build_wine11/wineasio.dll \
        /wineasio/build_wine11/wineasio.so \
        /mnt/

echo ""
echo "Build complete! Files created:"
ls -la *.dll *.so
echo ""
echo "Installation (Wine 11):"
echo "  sudo cp wineasio64.dll /opt/wine-stable/lib/wine/x86_64-windows/"
echo "  sudo cp wineasio64.so /opt/wine-stable/lib/wine/x86_64-unix/"
echo "  sudo cp wineasio.dll /opt/wine-stable/lib/wine/i386-windows/"
echo "  sudo cp wineasio.so /opt/wine-stable/lib/wine/i386-unix/"
