# BigCar backend developer tasks. CI runs: gofmt check, go vet, go test -race.

GO      ?= go
PKG     := ./...
BIN     := bin/bigcar-backend

.PHONY: all check fmt fmt-check vet test test-pg build run docker compose-up clean

all: check build

check: fmt-check vet test

fmt:
	gofmt -w .

fmt-check:
	@out="$$(gofmt -l .)"; if [ -n "$$out" ]; then echo "gofmt needed:"; echo "$$out"; exit 1; fi

vet:
	$(GO) vet $(PKG)

test:
	$(GO) test -race $(PKG)

# Postgres integration tests, e.g.
#   make test-pg BIGCAR_TEST_DATABASE_URL=postgres://bigcar:bigcar@127.0.0.1:5432/bigcar?sslmode=disable
test-pg:
	@test -n "$(BIGCAR_TEST_DATABASE_URL)" || (echo "set BIGCAR_TEST_DATABASE_URL"; exit 1)
	BIGCAR_TEST_DATABASE_URL="$(BIGCAR_TEST_DATABASE_URL)" $(GO) test -race -count=1 ./internal/storage/

build:
	CGO_ENABLED=0 $(GO) build -trimpath -o $(BIN) ./cmd/bigcar-backend

run:
	$(GO) run ./cmd/bigcar-backend --dev

docker:
	cd .. && docker build -f Backend/Dockerfile -t bigcar-backend .

compose-up:
	docker compose up --build

clean:
	rm -rf bin
