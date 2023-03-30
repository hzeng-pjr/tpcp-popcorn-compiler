FROM ubuntu:focal

ENV TZ=America/Los_Angeles
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update

RUN DEBIAN_FRONTEND=noninteractive TZ=America/Los_Angeles apt-get -y install tzdata

RUN apt-get install -y locales locales-all protobuf-c-compiler \
  libprotobuf-c-dev libprotobuf-dev bsdmainutils git-core \
  python3-protobuf cgroup-lite libapparmor-dev libseccomp-dev \
  libprotobuf-dev libaio-dev apparmor libnet1-dev libnl-3-dev \
  libcap-dev gcc build-essential iputils-ping net-tools iproute2 \
  x11-apps gdb
ENV LC_ALL en_US.UTF-8
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US.UTF-8

RUN groupadd -r docker -g 1000 && \
    useradd -u 1000 -r -g docker docker

RUN setcap cap_sys_admin+ep /usr/bin/unshare
