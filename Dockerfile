FROM ubuntu:24.04
RUN apt-get update && apt-get install -y build-essential openmpi-bin libopenmpi-dev \
    && rm -rf /var/lib/apt/lists/*
ENV OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1
WORKDIR /projeto
