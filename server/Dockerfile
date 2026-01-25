FROM ubuntu:22.04

# Avoid interactive dialogs during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Update and install dependencies
# build-essential includes g++, make, etc.
# ffmpeg is needed for audio decoding
RUN apt-get update && apt-get install -y \
    build-essential \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy the source code
COPY main.cpp .

# Compile the server
# -pthread is required for std::thread
# -O3 for optimization
# We compile to /usr/local/bin so the binary is not hidden if we mount a volume to /app
RUN g++ -O3 -pthread main.cpp -o /usr/local/bin/server

# Expose ports
# 8080: Command port
# 8081: Audio stream port
EXPOSE 8080 8081

# Run the server
CMD ["server"]
