#!/usr/bin/env bash
# ── Hedge Fund App – Build & Run Setup ────────────────────────────────────────
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# ── 1. Install system dependencies (Fedora/RHEL) ──────────────────────────────
install_deps() {
    echo "[setup] Installing system dependencies..."
    sudo dnf install -y \
        cmake gcc-c++ ninja-build \
        libX11-devel libXext-devel libXrandr-devel libXi-devel \
        libXcursor-devel libXinerama-devel libXfixes-devel \
        wayland-devel libxkbcommon-devel wayland-protocols-devel \
        mesa-libGL-devel mesa-libEGL-devel \
        glew-devel \
        openssl-devel \
        git
    echo "[setup] Dependencies installed."
}

# ── 2. Configure ──────────────────────────────────────────────────────────────
configure() {
    echo "[setup] Configuring with CMake..."
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        2>&1 | tee "$BUILD_DIR/cmake.log" || {
        echo "[setup] CMake configure failed. See $BUILD_DIR/cmake.log"
        exit 1
    }
    echo "[setup] Configure OK."
}

# ── 3. Build ──────────────────────────────────────────────────────────────────
build() {
    local JOBS
    JOBS=$(nproc 2>/dev/null || echo 4)
    echo "[setup] Building with $JOBS jobs..."
    cmake --build "$BUILD_DIR" --config Release -j "$JOBS" \
        2>&1 | tee "$BUILD_DIR/build.log" || {
        echo "[setup] Build failed. See $BUILD_DIR/build.log"
        exit 1
    }
    echo "[setup] Build OK."
    echo "[setup] Binaries:"
    echo "        Backend:  $BUILD_DIR/backend/hf_server"
    echo "        Frontend: $BUILD_DIR/frontend/hf_client"
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
        --port  "${HF_PORT:-8080}" \
        --db    "${HF_DB_PATH:-trading_app.db}" \
        --broker "${HF_BROKER_MODE:-mock}" &
    SERVER_PID=$!
    echo "[setup] Backend PID=$SERVER_PID"
    echo "$SERVER_PID" > "$ROOT_DIR/.server.pid"
}

run_client() {
    [[ -f "$ROOT_DIR/.env" ]] && source "$ROOT_DIR/.env"
    echo "[setup] Starting frontend client..."
    "$BUILD_DIR/frontend/hf_client" \
        --host localhost \
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
    deps)      install_deps ;;
    configure) mkdir -p "$BUILD_DIR"; configure ;;
    build)     mkdir -p "$BUILD_DIR"; configure; build ;;
    run)
        init_env
        run_server
        sleep 1
        run_client
        stop_server
        ;;
    server)    init_env; run_server; wait ;;
    client)    init_env; run_client ;;
    stop)      stop_server ;;
    all)
        install_deps
        init_env
        mkdir -p "$BUILD_DIR"
        configure
        build
        echo ""
        echo "======================================================="
        echo "  Build complete!"
        echo "  To run the app:"
        echo "    Terminal 1: ./setup.sh server"
        echo "    Terminal 2: ./setup.sh client"
        echo "  Or both at once: ./setup.sh run"
        echo "  Default login: admin / admin123"
        echo "======================================================="
        ;;
    *)
        echo "Usage: $0 {all|deps|configure|build|run|server|client|stop}"
        exit 1
        ;;
esac
