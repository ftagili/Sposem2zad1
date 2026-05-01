FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    graphviz \
    flex \
    bison \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /s2a