G0 uses protocol 9: 36-byte SPI frames with CRC16/CCITT-FALSE in bytes 34..35.
A reply belongs to the previous exchange. The link checks CRC, command, status,
payload size, channel and protocol version, then dispatches it to its module.
Modules rotate fairly; reconnect discards the old pipeline.

Enable Atlas with `G0_EXTENSION=yes`. SPI5 uses PF7/PF8/PF9 and PF6 for CS.
Only the 10 ms background worker transfers data. DMA buffers are noncached on H7.
State locks cover module callbacks and readers/writers, never SPI transfers.

ADC inputs EFI_ADC_20..EFI_ADC_31 correspond to G0 channels 0..9, 17 and 18.
Values are volts at the G0 pin; board divider calibration still applies.
Unavailable or 250 ms old samples invalidate sensors. Filters reload on recovery.

G0_DIGITAL_1..G0_DIGITAL_4 use the GPIO interface. Configuration is acknowledged
before polling. Reads return false when unavailable. SENT is unsupported.

Run standalone tests with sanitizers:

```sh
cmake -S firmware/hw_layer/g0_extension/tests -B /tmp/g0-tests -DG0_SANITIZE=ON
cmake --build /tmp/g0-tests -j12
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/g0-tests --output-on-failure
```

Physical SPI and I/O timing still require bench verification.
