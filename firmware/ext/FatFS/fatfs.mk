# FATFS files.
# The disk IO binding is ours, not ChibiOS's. It needs a file name of its own: sources are found
# by name through VPATH, where ChibiOS's fatfs_diskio.c would win.
FATFSSRC = ${PROJECT_DIR}/hw_layer/fatfs_drives.c \
           ${CHIBIOS}/os/various/fatfs_bindings/fatfs_syscall.c \
           ${PROJECT_DIR}/ext/FatFS/ff.c \
           ${PROJECT_DIR}/ext/FatFS/ffunicode.c \
           ${PROJECT_DIR}/ext/FatFS/option/unicode.c

FATFSINC = ${PROJECT_DIR}/ext
