# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

# The DJV sync server in a container. It needs nothing but Node: SQLite is
# built in, and there are no packages to install. The sessions, the users
# and the settings are all in /data, which is meant to be a folder of the
# host (see deploy/docker-compose.yml), so that they outlive the container
# and can be backed up from the host. The passwords come in as Docker
# secrets, never in the image or the environment.

FROM node:24-alpine

LABEL org.opencontainers.image.title="DJV sync server" \
      org.opencontainers.image.description="Shares the markers of DJV reviews between people, live." \
      org.opencontainers.image.licenses="BSD-3-Clause"

ENV NODE_ENV=production \
    HOST=0.0.0.0 \
    PORT=8877 \
    DATA_DIR=/data \
    CONFIG_FILE=/data/config.json \
    ROOT_PASSWORD_FILE=/run/secrets/lldjv_password \
    SHARE_PASSWORD_FILE=/run/secrets/share_password

WORKDIR /app
COPY package.json server.js config.js users.js admin.html ./

# The server runs as Node's own unprivileged user, which owns the data
# folder when it is not mounted from the host.
RUN mkdir -p /data && chown node:node /data
USER node

EXPOSE 8877
VOLUME /data

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD wget -q -O /dev/null "http://127.0.0.1:${PORT}/api/health" || exit 1

CMD ["node", "--disable-warning=ExperimentalWarning", "server.js"]
