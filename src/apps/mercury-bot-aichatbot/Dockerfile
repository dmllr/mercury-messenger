FROM ubuntu:24.04

# Install minimal runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4 \
    libsqlite3-0 \
    libstdc++6 \
    libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY mercury-llm-chatbot-bin /app/mercury-llm-chatbot
RUN chmod +x /app/mercury-llm-chatbot

RUN mkdir -p /data/identity && \
    chown -R ubuntu:ubuntu /data

USER ubuntu

ENTRYPOINT ["/app/mercury-llm-chatbot"]
