FROM ubuntu:24.04
LABEL maintainer="WineASIO Contributors"
ENV DEBIAN_FRONTEND noninteractive

# enable i386
RUN dpkg --add-architecture i386

# update system
RUN apt-get update -qq && apt-get upgrade -qqy && apt-get clean

# install packages needed for build
RUN apt-get install -qqy --no-install-recommends \
    ca-certificates \
    gcc \
    gcc-multilib \
    gcc-mingw-w64-x86-64 \
    gcc-mingw-w64-i686 \
    git \
    libjack-jackd2-dev \
    libjack-jackd2-dev:i386 \
    make \
    openssl \
    wget \
    && apt-get clean

# install Wine 11 from WineHQ
RUN mkdir -pm755 /etc/apt/keyrings && \
    wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key && \
    wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/ubuntu/dists/noble/winehq-noble.sources

RUN apt-get update -qq && \
    apt-get install -qqy --no-install-recommends winehq-stable wine-stable-dev && \
    apt-get clean

# fetch wineasio (use giang17 fork with Wine 11 support)
RUN git clone https://github.com/giang17/wineasio.git --depth=1

# build wineasio for Wine 11
WORKDIR /wineasio
RUN make -f Makefile.wine11 32 64

# CMD for inspection
CMD ["bash"]
