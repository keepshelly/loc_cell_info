FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libglfw3-dev \
    libglew-dev \
    libzmq3-dev \
    libpq-dev \
    libpqxx-dev \
    pkg-config \
    libsqlite3-dev \
    xorg-dev \
    libgl1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)
EXPOSE 5554 8080
CMD ["./build/monitor_server"]