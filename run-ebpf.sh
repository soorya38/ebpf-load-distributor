#!/usr/bin/env bash

set -e

CONTAINER="ebpf-lab"

echo "======================================"
echo " Starting eBPF Docker Demo Environment"
echo "======================================"

# Start docker compose (container should use tail -f /dev/null)
docker compose up -d

echo "Waiting for container to stabilize..."
sleep 5

# Ensure container is running
if ! docker ps | grep -q "$CONTAINER"; then
  echo "Container not running. Attempting restart..."
  docker start $CONTAINER
  sleep 3
fi

echo "======================================"
echo " Installing eBPF Toolchain"
echo "======================================"

docker exec -it $CONTAINER bash -c "
apt-get update &&
DEBIAN_FRONTEND=noninteractive apt-get install -y \
clang llvm gcc make \
iproute2 bpftool \
libbpf-dev linux-tools-common linux-tools-generic \
curl ca-certificates || true
"

echo "======================================"
echo " Mounting debugfs for eBPF logs"
echo "======================================"

docker exec -it $CONTAINER bash -c "
mkdir -p /sys/kernel/debug &&
mount -t debugfs none /sys/kernel/debug || true
"

echo "======================================"
echo " Compiling eBPF Program"
echo "======================================"

docker exec -it $CONTAINER bash -c "
cd /workspace &&
clang -O2 -target bpf -c xdp_lb.c -o xdp_lb.o
"

echo "======================================"
echo " Attaching XDP Program"
echo "======================================"

docker exec -it $CONTAINER bash -c "
ip link set dev eth0 xdp off 2>/dev/null || true
ip link set dev eth0 xdp obj /workspace/xdp_lb.o sec xdp
"

echo "======================================"
echo " Verifying eBPF Program"
echo "======================================"

docker exec -it $CONTAINER bpftool prog show || true

echo ""
echo "======================================"
echo " eBPF Demo Ready"
echo "======================================"
echo "Generate traffic from another terminal:"
echo "  docker exec -it ebpf-lab ping 8.8.8.8"
echo "  docker exec -it ebpf-lab curl google.com"
echo ""
echo "Streaming kernel logs below (Ctrl+C to stop)"
echo "======================================"
echo ""

docker exec -it $CONTAINER bash -c "
cat /sys/kernel/debug/tracing/trace_pipe
"

