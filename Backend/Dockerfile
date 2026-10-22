# syntax=docker/dockerfile:1
#
# BigCar backend image.
#
# The build context is the REPOSITORY ROOT (the image bundles the game data
# from Content/BigCar/Data). From the repository root:
#
#   docker build -f Backend/Dockerfile -t bigcar-backend .
#
# Backend/Dockerfile.dockerignore limits the context to Backend/ and
# Content/BigCar/Data/ (requires BuildKit, the default builder).

FROM golang:1.24-alpine AS build
WORKDIR /src
ENV CGO_ENABLED=0 GOTOOLCHAIN=local
COPY Backend/go.mod Backend/go.sum ./
RUN go mod download
COPY Backend/ ./
RUN go build -trimpath -ldflags="-s -w" -o /out/bigcar-backend ./cmd/bigcar-backend

FROM gcr.io/distroless/static-debian12:nonroot
WORKDIR /app
COPY --from=build /out/bigcar-backend /app/bigcar-backend
COPY Content/BigCar/Data /app/data
ENV BIGCAR_ADDR=:8080 \
    BIGCAR_DATA_DIR=/app/data
EXPOSE 8080
USER nonroot:nonroot
HEALTHCHECK --interval=10s --timeout=5s --start-period=5s --retries=3 \
    CMD ["/app/bigcar-backend", "--healthcheck"]
ENTRYPOINT ["/app/bigcar-backend"]
