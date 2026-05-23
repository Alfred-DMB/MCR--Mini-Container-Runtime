#!/bin/bash
set -euo pipefail

MCR=./mcr
ROOTFS=./alpine
PASS=0
FAIL=0

run_test() {
    local name="$1"
    local expected="$2"
    shift 2
    local actual
    actual=$("$MCR" run --rootfs "$ROOTFS" -- "$@" 2>/dev/null)
    if echo "$actual" | grep -q "$expected"; then
        echo "  [PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] $name"
        echo "         expected: $expected"
        echo "         got:      $(echo "$actual" | tail -3)"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== MCR integration tests ==="
echo ""

# El binario existe
echo "[1] Binary"
if [[ -x "$MCR" ]]; then
    echo "  [PASS] mcr binary exists and is executable"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] mcr binary not found — run 'make' first"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "[2] Isolation"

# El contenedor ve UID 0 (user namespace activo)
run_test "UID is 0 inside container" "uid=0" id

# El hostname dentro del contenedor es "mcr"
run_test "hostname is 'mcr'" "mcr" hostname

# El rootfs es Alpine (busybox está en /bin)
run_test "rootfs is Alpine" "busybox" ls /bin

echo ""
echo "[3] Basic execution"

# echo funciona
run_test "echo works" "hello" /bin/echo hello

# sh puede ejecutar un comando inline
run_test "sh -c works" "ok" /bin/sh -c "echo ok"

# pwd dentro del contenedor es /
run_test "working dir is /" "/" pwd

echo ""
echo "[4] Filesystem isolation"

# /proc está montado
run_test "/proc is mounted" "proc" sh -c "mount | grep proc"

# /tmp existe
run_test "/tmp is mounted" "tmp" sh -c "ls / | grep tmp"

echo ""
echo "=============================="
echo "  Results: $PASS passed, $FAIL failed"
echo "=============================="

[[ $FAIL -eq 0 ]]
