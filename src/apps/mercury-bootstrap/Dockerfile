FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libstdc++6 \
    libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY mercury-bootstrap-bin /app/mercury-bootstrap
RUN chmod +x /app/mercury-bootstrap

RUN mkdir -p /data && \
    chown -R ubuntu:ubuntu /data

USER ubuntu

EXPOSE 10807

ENTRYPOINT ["/app/mercury-bootstrap"]
CMD ["--port", "10807", "--identity-file", "/data/bootstrap_identity.key"]
