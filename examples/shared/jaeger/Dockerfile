FROM jaegertracing/all-in-one@sha256:91c9e4f9abbff25e417f00341a65f7cb07e431f9f77c13feaa2bd30f63d59ab3
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
