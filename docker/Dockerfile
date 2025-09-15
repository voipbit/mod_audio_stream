# =============================================================================
# mod_audio_stream - Working Build for Testing
# =============================================================================
#
# This Dockerfile builds FreeSWITCH from source and our mod_audio_stream module
#

FROM debian:bullseye-slim

LABEL maintainer="FreeSWITCH Community"
LABEL description="FreeSWITCH with mod_audio_stream module"

# Install all necessary dependencies
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        # Build essentials
        build-essential \
        cmake \
        pkg-config \
        git \
        wget \
        autoconf \
        automake \
        libtool \
        libtool-bin \
        yasm \
        nasm \
        # FreeSWITCH core dependencies
        libsqlite3-dev \
        libcurl4-openssl-dev \
        libpcre3-dev \
        libedit-dev \
        libopus-dev \
        uuid-dev \
        libsndfile1-dev \
        zlib1g-dev \
        libjpeg-dev \
        # mod_audio_stream specific dependencies
        libwebsockets-dev \
        libspeexdsp-dev \
        libspandsp-dev \
        # Runtime libraries
        libsqlite3-0 \
        libcurl4 \
        libpcre3 \
        libedit2 \
        libopus0 \
        uuid-runtime \
        libsndfile1 \
        libwebsockets16 \
        libspeexdsp1 \
        libspandsp2 \
        # Utilities
        ca-certificates \
        curl \
        procps \
        # Python and networking tools
        python3 \
        python3-pip \
        python3-venv \
        netcat-traditional \
        && rm -rf /var/lib/apt/lists/* \
        && apt-get clean

# Create freeswitch user and directories
RUN groupadd -r freeswitch && \
    useradd -r -g freeswitch -d /opt/freeswitch -s /bin/bash freeswitch && \
    mkdir -p /opt/freeswitch/{bin,lib,etc,var,share} \
             /opt/freeswitch/lib/freeswitch/mod \
             /opt/freeswitch/etc/freeswitch/autoload_configs \
             /var/log/freeswitch \
             /var/lib/freeswitch \
             /var/run/freeswitch

# Build sofia-sip dependency
WORKDIR /usr/src
RUN git clone https://github.com/freeswitch/sofia-sip.git && \
    cd sofia-sip && \
    ./bootstrap.sh && \
    ./configure --prefix=/usr && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# Build spandsp dependency
RUN git clone https://github.com/freeswitch/spandsp.git && \
    cd spandsp && \
    ./bootstrap.sh && \
    ./configure --prefix=/usr && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# Build FreeSWITCH from source
RUN git clone --depth=1 --branch=v1.10.11 https://github.com/signalwire/freeswitch.git && \
    cd freeswitch && \
    ./bootstrap.sh -j

# Debug: Check what packages are available
RUN echo "Checking spandsp installation..." && \
    find /usr -name "*spandsp*" -type f 2>/dev/null | head -10 && \
    pkg-config --list-all | grep spandsp || echo "No spandsp in pkg-config" && \
    export PKG_CONFIG_PATH=/usr/lib/pkgconfig:/usr/lib/*/pkgconfig:/usr/share/pkgconfig && \
    pkg-config --list-all | grep spandsp || echo "Still no spandsp in pkg-config"

# Configure FreeSWITCH with minimal modules
WORKDIR /usr/src/freeswitch
ENV PKG_CONFIG_PATH=/usr/lib/pkgconfig:/usr/lib/*/pkgconfig:/usr/share/pkgconfig

# Create minimal modules.conf with only essential modules
RUN cp modules.conf modules.conf.original && \
    cat > modules.conf << 'EOF'
# Minimal FreeSWITCH modules configuration
# Only include essential modules to avoid dependency issues

# Core modules - absolutely required
loggers/mod_console
loggers/mod_logfile
loggers/mod_syslog

# Application modules - core functionality
applications/mod_commands
applications/mod_dptools
applications/mod_expr
applications/mod_fifo
applications/mod_hash
applications/mod_playback
applications/mod_conference

# Dialplan modules
applications/mod_dialplan_xml

# Endpoint modules
endpoints/mod_sofia
endpoints/mod_loopback

# Codec modules - basic audio codecs
codecs/mod_g711
codecs/mod_g722
codecs/mod_g729
codecs/mod_gsm
codecs/mod_opus

# Format modules - file format support
formats/mod_sndfile
formats/mod_native_file
formats/mod_tone_stream
formats/mod_wav

# Event modules
event_handlers/mod_event_socket

# Timer modules
timers/mod_softtimer
EOF

# Debug: Check what modules are still enabled
RUN echo "Checking remaining modules:" && \
    grep -E "mod_verto|mod_enum|mod_signalwire|mod_lua" modules.conf || echo "Target modules not found or already commented"

RUN ./configure \
        --prefix=/opt/freeswitch \
        --disable-debug \
        --enable-optimize \
        --disable-static \
        --with-modinstdir=/opt/freeswitch/lib/freeswitch/mod \
        --without-python \
        --without-java \
        --without-perl \
        --disable-core-libedit-support \
        --disable-core-odbc-support \
        --disable-core-pgsql-support

# Build only the core and essential modules
RUN make -j$(nproc) && make install && \
    # Clean up build artifacts to save space
    rm -rf /usr/src/freeswitch/.git /usr/src/sofia-sip/.git /usr/src/spandsp/.git && \
    find /usr/src -name '*.o' -delete && \
    find /usr/src -name '*.a' -delete && \
    find /usr/src -type d -name '.libs' -exec rm -rf {} + 2>/dev/null || true

# Debug: Check FreeSWITCH installation (updated)
RUN echo "DEBUG: Checking FreeSWITCH installation at $(date)..." && \
    ls -la /opt/freeswitch/ && \
    echo "Include directory contents:" && \
    find /opt/freeswitch -name "*.h" | head -20 && \
    echo "Looking for switch.h:" && \
    find /opt/freeswitch -name "switch.h" && \
    echo "Looking for g711.h:" && \
    find /opt/freeswitch -name "g711.h" && \
    find /usr/src/freeswitch -name "g711.h" && \
    echo "Full src structure:" && \
    find /usr/src/freeswitch/src -name "*.h" | head -10

# Copy mod_audio_stream source
COPY . /usr/src/mod_audio_stream/
WORKDIR /usr/src/mod_audio_stream

# Build mod_audio_stream module
RUN mkdir -p build && \
    cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DFREESWITCH_INCLUDE_DIR=/opt/freeswitch/include/freeswitch \
        -DFREESWITCH_SOURCE_INCLUDE_DIR=/usr/src/freeswitch/src/include \
        -DFREESWITCH_LIBRARY=/opt/freeswitch/lib/libfreeswitch.so \
        -DCMAKE_INSTALL_PREFIX=/opt/freeswitch \
        -DFS_MOD_DIR=lib/freeswitch/mod && \
    make -j$(nproc) VERBOSE=1 && \
    cp mod_audio_stream.so /opt/freeswitch/lib/freeswitch/mod/

# Copy corrected FreeSWITCH configuration with ACLs
COPY docker/config/freeswitch.xml /opt/freeswitch/etc/freeswitch/

# Copy mod_audio_stream configuration (create basic one since sample doesn't exist)
RUN echo '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>' > /opt/freeswitch/etc/freeswitch/autoload_configs/audio_stream.conf.xml && \
    echo '<configuration name="audio_stream.conf" description="Audio Stream Configuration">' >> /opt/freeswitch/etc/freeswitch/autoload_configs/audio_stream.conf.xml && \
    echo '  <settings></settings>' >> /opt/freeswitch/etc/freeswitch/autoload_configs/audio_stream.conf.xml && \
    echo '</configuration>' >> /opt/freeswitch/etc/freeswitch/autoload_configs/audio_stream.conf.xml

# Install Python websockets package and copy test server
RUN pip3 install websockets
COPY docker/test_websocket_server.py /opt/freeswitch/bin/

# Set proper permissions
RUN chown -R freeswitch:freeswitch /opt/freeswitch \
                                   /var/log/freeswitch \
                                   /var/lib/freeswitch \
                                   /var/run/freeswitch

# Add FreeSWITCH binaries to PATH
ENV PATH="/opt/freeswitch/bin:${PATH}"

# Debug: Check what was actually built and installed
RUN echo "DEBUG: Checking what was built..." && \
    ls -la /usr/src/mod_audio_stream/build/ && \
    echo "Checking install directories:" && \
    find /opt/freeswitch -name "mod_audio_stream*" -type f && \
    echo "Checking all .so files:" && \
    find /opt/freeswitch -name "*.so" | head -10

# Test module loading (non-strict for debugging)
RUN echo "Testing module loading..." && \
    (ls -la /opt/freeswitch/lib/freeswitch/mod/mod_audio_stream.so || echo "Module not found in expected location") && \
    echo "Listing all files that were created:" && \
    find /opt/freeswitch -name "*.so" -type f | head -20

# Expose ports
EXPOSE 5060/udp 5060/tcp 5080/tcp 8021/tcp 8080/tcp
EXPOSE 16384-16394/udp

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
  CMD freeswitch -version || exit 1

# Volumes
VOLUME ["/var/log/freeswitch", "/var/lib/freeswitch"]

# Switch to freeswitch user
USER freeswitch

# Working directory
WORKDIR /opt/freeswitch

# Start FreeSWITCH in foreground
CMD ["freeswitch", "-nonat", "-c"]