#!/usr/bin/env bash
# ── Hedge Fund App – Build & Run Setup ────────────────────────────────────────
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# ── 1a. Full deps (backend + desktop GUI frontend) ────────────────────────────
install_deps() {
    echo "[setup] Installing full dependencies (backend + GUI frontend)..."
    sudo dnf install -y \
        cmake gcc-c++ \
        libX11-devel libXext-devel libXrandr-devel libXi-devel \
        libXcursor-devel libXinerama-devel libXfixes-devel \
        wayland-devel libxkbcommon-devel wayland-protocols-devel \
        mesa-libGL-devel mesa-libEGL-devel \
        glew-devel openssl-devel git
    echo "[setup] Full dependencies installed."
}

# ── 1b. Server-only deps — no GUI libraries needed ───────────────────────────
install_server_deps() {
    echo "[setup] Installing server-only dependencies (no GUI)..."
    sudo dnf install -y cmake gcc-c++ openssl-devel git
    echo "[setup] Server dependencies installed."
}

# ── 2. Configure ──────────────────────────────────────────────────────────────
configure() {
    local extra="${1:-}"
    echo "[setup] Configuring with CMake... $extra"
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        $extra \
        2>&1 | tee "$BUILD_DIR/cmake.log" || {
        echo "[setup] CMake configure failed. See $BUILD_DIR/cmake.log"
        exit 1
    }
    echo "[setup] Configure OK."
}

# ── 3. Build ──────────────────────────────────────────────────────────────────
build() {
    local target="${1:-}"
    local JOBS; JOBS=$(nproc 2>/dev/null || echo 4)
    echo "[setup] Building with $JOBS jobs..."
    cmake --build "$BUILD_DIR" --config Release -j "$JOBS" ${target:+--target $target} \
        2>&1 | tee "$BUILD_DIR/build.log" || {
        echo "[setup] Build failed. See $BUILD_DIR/build.log"
        exit 1
    }
    echo "[setup] Build OK."
}

# ── 4. Create .env from template ──────────────────────────────────────────────
init_env() {
    if [[ ! -f "$ROOT_DIR/.env" ]]; then
        cp "$ROOT_DIR/.env.example" "$ROOT_DIR/.env"
        echo "[setup] Created .env from template. Edit it to set your secrets."
    fi
}

# ── 5. Run ────────────────────────────────────────────────────────────────────
run_server() {
    [[ -f "$ROOT_DIR/.env" ]] && source "$ROOT_DIR/.env"
    echo "[setup] Starting backend server on port ${HF_PORT:-8080}..."
    "$BUILD_DIR/backend/hf_server" \
        --port   "${HF_PORT:-8080}" \
        --db     "${HF_DB_PATH:-trading_app.db}" \
        --broker "${HF_BROKER_MODE:-mock}" &
    SERVER_PID=$!
    echo "[setup] Backend PID=$SERVER_PID"
    echo "$SERVER_PID" > "$ROOT_DIR/.server.pid"
}

run_client() {
    [[ -f "$ROOT_DIR/.env" ]] && source "$ROOT_DIR/.env"
    local host="${HF_SERVER_HOST:-localhost}"
    echo "[setup] Starting frontend client (connecting to $host:${HF_PORT:-8080})..."
    "$BUILD_DIR/frontend/hf_client" \
        --host "$host" \
        --port "${HF_PORT:-8080}"
}

stop_server() {
    if [[ -f "$ROOT_DIR/.server.pid" ]]; then
        PID=$(cat "$ROOT_DIR/.server.pid")
        kill "$PID" 2>/dev/null && echo "[setup] Server $PID stopped." || true
        rm -f "$ROOT_DIR/.server.pid"
    fi
}

# ── Main ──────────────────────────────────────────────────────────────────────
case "${1:-all}" in

    # ── Full workstation build (backend + GUI) ─────────────────────────────
    deps)
        install_deps ;;
    configure)
        mkdir -p "$BUILD_DIR"; configure ;;
    build)
        mkdir -p "$BUILD_DIR"; configure; build ;;

    # ── Headless server: only the backend binary ───────────────────────────
    #   On the server machine:
    #     ./setup.sh server-deps    (once, installs cmake/gcc/openssl)
    #     ./setup.sh server-build   (builds only hf_server, no GUI libs)
    #     ./setup.sh server         (runs hf_server)
    server-deps)
        install_server_deps ;;
    server-build)
        mkdir -p "$BUILD_DIR"
        configure "-DBUILD_FRONTEND=OFF"
        build hf_server
        echo ""
        echo "  Binary ready: $BUILD_DIR/backend/hf_server"
        echo "  Run with:     ./setup.sh server"
        ;;

    # ── Run commands ──────────────────────────────────────────────────────
    run)
        init_env
        run_server
        sleep 1
        run_client
        wait
        ;;
    server)   init_env; run_server; wait ;;
    client)   init_env; run_client ;;
    stop)     stop_server ;;

    # ── All-in-one (first-time workstation setup) ─────────────────────────
    all)
        install_deps
        init_env
        mkdir -p "$BUILD_DIR"
        configure
        build
        echo ""
        echo "======================================================="
        echo "  Build complete!"
        echo ""
        echo "  Workstation:"
        echo "    Terminal 1: ./setup.sh server"
        echo "    Terminal 2: ./setup.sh client"
        echo ""
        echo "  Remote server:"
        echo "    ./setup.sh server-deps && ./setup.sh server-build"
        echo "    ./setup.sh server"
        echo ""
        echo "  Default login: admin / admin123"
        echo "======================================================="
        ;;
    *)
        echo "Usage: $0 {all|deps|build|server-deps|server-build|server|client|run|stop}"
        exit 1
        ;;
esac
