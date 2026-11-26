CARGO_BIN_DIR := $(if $(CARGO_HOME),$(CARGO_HOME),$(HOME)/.cargo)/bin
export PATH := $(PATH):$(CARGO_BIN_DIR)

CARGO ?= $(or $(shell command -v cargo 2>/dev/null),$(wildcard $(CARGO_BIN_DIR)/cargo),cargo)
PYTHON ?= python3

.PHONY: all test clean

all: target/release/wg_encode bin/webgraph

target/release/wg_encode: Cargo.toml Cargo.lock $(wildcard src/*.rs)
	$(CARGO) build --release --locked

bin/webgraph:
	$(CARGO) install webgraph-cli --version 0.4.2 --locked --root "$(CURDIR)"

test: all
	$(CARGO) test --locked
	$(PYTHON) -m unittest discover -s tests

clean:
	rm -rf target bin __pycache__ tests/__pycache__
	rm -f .crates.toml .crates2.json
