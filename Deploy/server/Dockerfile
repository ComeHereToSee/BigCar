# BigCar dedicated server runtime image.
#
# Input: the packaged Linux server produced by .github/workflows/unreal.yml (artifact BigCar-LinuxServer.tar.gz),
# extracted next to this Dockerfile so that ./LinuxServer/BigCarServer.sh exists:
#
#   tar -xzf BigCar-LinuxServer.tar.gz -C Deploy/server
#   docker build -t bigcar-server Deploy/server
#
# One container runs one match at a time; the process exits after reporting the result and the orchestrator
# (docker compose `restart: always`, Kubernetes, Agones, ...) starts a fresh one.
FROM ubuntu:22.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates tzdata \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --create-home --uid 1000 bigcar

# Unreal refuses to run as root, so the server runs as an unprivileged user.
COPY --chown=bigcar:bigcar LinuxServer/ /srv/bigcar/
RUN chmod +x /srv/bigcar/BigCarServer.sh /srv/bigcar/BigCar/Binaries/Linux/BigCarServer

USER bigcar
WORKDIR /srv/bigcar

# BIGCAR_BACKEND_URL / BIGCAR_SERVER_KEY register the server with the backend (see Backend/README.md);
# without them it runs as an unmanaged server that anyone can join with the address.
ENV BIGCAR_REGION=local \
    BIGCAR_PUBLIC_ADDRESS=127.0.0.1

EXPOSE 7777/udp

ENTRYPOINT ["/srv/bigcar/BigCarServer.sh"]
CMD ["/Engine/Maps/Entry?game=match", "-port=7777", "-log", "-unattended", "-nosound"]
