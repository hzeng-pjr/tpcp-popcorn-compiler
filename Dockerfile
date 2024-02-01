FROM debian:bullseye-slim
RUN apt update && DEBIAN_FRONTEND=noninteractive apt install -y --no-install-recommends \
    apt-utils tzdata
ENV TZ="UTC"
RUN date

# create non-root user to use wherever possible
RUN useradd -m -s /bin/bash pjr
WORKDIR /rasec-popcorn-compiler
RUN chown pjr:pjr /rasec-popcorn-compiler
USER pjr

# copy custom popcorn compiler
COPY --chown=pjr:pjr . .
RUN chown pjr:pjr /rasec-popcorn-compiler

# install all
USER root
RUN apt update && DEBIAN_FRONTEND=noninteractive apt install -y --no-install-recommends \
    python3 g++ gcc-aarch64-linux-gnu flex bison cmake libtool make zip libtool-bin git \ 
    texinfo curl build-essential ca-certificates rsync gawk autoconf automake autopoint \
    pkg-config gettext libelf-dev xutils-dev libx11-dev libsm-dev libice-dev x11proto-dev \
    xcb-proto python3-xcbgen libxext-dev libxt-dev libxmu-dev libxpm-dev
RUN ./install_compiler.py --install-all

