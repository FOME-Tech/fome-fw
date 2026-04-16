#!/bin/bash
# Run the FOME CAN Bridge on Linux (SocketCAN)
# Usage: ./fome_bridge.sh [device_name] [rx_id] [tx_id]

DEVICE=${1:-can0}
RX_ID=${2:-0x102}
TX_ID=${3:-0x100}

java -Dcan.type=socketcan -Dcan.device=$DEVICE -Dcan.rx.id=$RX_ID -Dcan.tx.id=$TX_ID -jar fome-can-bridge.jar
