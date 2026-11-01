FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libstdc++6 \
    libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY mercury-bootstrap-bin /app/mercury-bootstrap
COPY entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/mercury-bootstrap /app/entrypoint.sh

RUN mkdir -p /data && \
    chown -R ubuntu:ubuntu /data

USER ubuntu

EXPOSE 10807/tcp 10807/udp 3478/udp 50000-50500/udp

ENTRYPOINT ["/app/entrypoint.sh"]
CMD []
