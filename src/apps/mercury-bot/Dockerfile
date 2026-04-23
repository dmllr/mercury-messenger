FROM ubuntu:24.04

# Install minimal runtime dependencies
# - ca-certificates: Required for SSL/TLS connections (Signaling, WebRTC)
# - libsqlite3-0: Required for SQLite storage
# - libstdc++6: Required for C++ runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libsqlite3-0 \
    libstdc++6 \
    libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the binary from the build context (placed by the build script)
COPY mercury-bot-bin /app/mercury-bot
RUN chmod +x /app/mercury-bot

# Create directories for persistent data and map ownership
RUN mkdir -p /data/identity /data/output && \
    chown -R ubuntu:ubuntu /data

USER ubuntu

# Default entrypoint
ENTRYPOINT ["/app/mercury-bot"]
CMD ["--identity-dir", "/data/identity", "--output-dir", "/data/output"]
