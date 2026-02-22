openocd -f interface/stlink.cfg \
  -c "set DUAL_BANK 1" \
  -f target/stm32h7x.cfg \
  -c "init" \
  -c "reset halt" \
  -c "stm32h7x mass_erase 0" \
  -c "stm32h7x mass_erase 1" \
  -c "program firmware/deliver/fome.bin verify reset exit 0x08000000"
