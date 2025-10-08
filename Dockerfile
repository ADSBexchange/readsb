# syntax=docker/dockerfile:1.6

############################
# Builder (Alpine + toolchain)
############################
FROM alpine:3.20 AS build

# ---- Build-time---
ARG TRACKS_UUID=no          # yes|no
ARG PRINT_UUIDS=no           # yes|no
ARG APPLY_MAKEFILE_PATCH=yes # yes to replace "wiedehopf git" with "adsbexchange git"

RUN apk add --no-cache \
    build-base \
    git \
    cmake \
    libusb-dev \
    ncurses-dev \
    zlib-dev \
    zstd-dev

WORKDIR /src
# Build from the repo's working tree so readsb --version can see .git, if present.
COPY . .

# Optional Makefile patch
RUN if [ "$APPLY_MAKEFILE_PATCH" = "yes" ]; then \
    sed -i 's/wiedehopf git/adsbexchange git/g' Makefile || true; \
    fi

# Compile (net-only build; adjust if you add SDR later)
# Use a cache mount to speed up rebuilds
RUN --mount=type=cache,target=/root/.cache \
    make -j"$(getconf _NPROCESSORS_ONLN)" \
    TRACKS_UUID=${TRACKS_UUID} \
    PRINT_UUIDS=${PRINT_UUIDS}

# Prepare artifacts + metadata
RUN mkdir -p /out && \
    cp readsb viewadsb /out/ && chmod +x /out/readsb /out/viewadsb && \
    { \
    echo "READSB_TRACKS_UUID=${TRACKS_UUID}"; \
    echo "READSB_PRINT_UUIDS=${PRINT_UUIDS}"; \
    echo "READSB_COMMIT=$(git rev-parse HEAD 2>/dev/null || true)"; \
    echo "READSB_BRANCH_NAME=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"; \
    echo -n "READSB_VERSION="; /out/readsb --version 2>&1; \
    } > /out/version.txt && \
    V=$(/out/readsb --version 2>&1 | awk '{ print $3 }'); \
    touch "/out/${V}.readsb-version"

############################
# Runtime (minimal Alpine)
############################
FROM alpine:3.20

ARG UID=10100
ARG GID=10100

RUN apk add --no-cache \
    libstdc++ \
    libusb \
    ncurses-libs \
    zlib \
    zstd \
    tzdata \
    tini

# Dedicated, non-root user
RUN addgroup -g $GID readsb && \
    adduser  -D -H -s /sbin/nologin -u $UID -G readsb readsb && \
    mkdir -p /run/readsb /var/lib/readsb /var/log/readsb && \
    chown -R readsb:readsb /run/readsb /var/lib/readsb /var/log/readsb

COPY --from=build /out/readsb             /usr/local/bin/readsb
COPY --from=build /out/viewadsb           /usr/local/bin/viewadsb
COPY --from=build /out/version.txt        /etc/readsb-version.txt
COPY --from=build /out/*.readsb-version   /etc/

USER readsb

# Open only what you actually use; these are common net-only ports
EXPOSE 30004-30006/udp 32006-32009/tcp 34005-34009/tcp

HEALTHCHECK --interval=30s --timeout=3s --start-period=15s --retries=3 \
    CMD /usr/local/bin/readsb --help >/dev/null 2>&1 || exit 1

ENTRYPOINT ["/sbin/tini","--","/usr/local/bin/readsb"]
# override at `docker run`
CMD ["--help"]
