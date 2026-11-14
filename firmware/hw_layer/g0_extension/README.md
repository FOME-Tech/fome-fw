G0 uses protocol 9: 36-byte SPI frames with CRC16/CCITT-FALSE in bytes 34..35.
A reply belongs to the previous exchange. The link checks CRC, command, status,
payload size, channel and protocol version, then dispatches it to its module.
Modules rotate fairly; reconnect discards the old pipeline.

Run standalone tests with sanitizers:

```sh
cmake -S firmware/hw_layer/g0_extension/tests -B /tmp/g0-tests -DG0_SANITIZE=ON
cmake --build /tmp/g0-tests -j12
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/g0-tests --output-on-failure
```

Physical SPI and I/O timing still require bench verification.
