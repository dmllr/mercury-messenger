FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libsqlite3-0 \
    libstdc++6 \
    libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY mercury-bot-aichatbot-bin /app/mercury-bot-aichatbot
RUN chmod +x /app/mercury-bot-aichatbot

RUN mkdir -p /data/identity && \
    chown -R ubuntu:ubuntu /data

USER ubuntu

ENTRYPOINT ["/app/mercury-bot-aichatbot"]
