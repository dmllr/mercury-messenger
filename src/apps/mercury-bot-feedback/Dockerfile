FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libsqlite3-0 \
    libstdc++6 \
    libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY mercury-bot-bin /app/mercury-bot
RUN chmod +x /app/mercury-bot

RUN mkdir -p /data/identity /data/output && \
    chown -R ubuntu:ubuntu /data

USER ubuntu

# Default entrypoint
ENTRYPOINT ["/app/mercury-bot"]
CMD ["--identity-dir", "/data/identity", "--output-dir", "/data/output"]
