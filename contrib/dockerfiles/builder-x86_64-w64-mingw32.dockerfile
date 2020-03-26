### Builder that acts as a base for building Defichain with basics dependencies 
### that are required throughout the process
FROM ubuntu:18.04

RUN apt update && apt dist-upgrade -y

# Setup Defichain build dependencies. Refer to depends/README.md and doc/build-unix.md
# from the source root for info on the builder setup  

RUN apt install -y software-properties-common build-essential libtool autotools-dev automake \
    pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev \
    libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
    libminiupnpc-dev libzmq3-dev libqrencode-dev \
    curl cmake \
    g++-mingw-w64-x86-64 mingw-w64-x86-64-dev nsis

# Set the default mingw32 g++ compiler option to posix.
RUN update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix

# For Berkeley DB - but we don't need as we do a depends build.
# RUN apt install -y libdb-dev
