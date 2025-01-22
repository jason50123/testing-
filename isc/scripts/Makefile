

# PAL configs		   ch pkg die plane blk page
PAL_SIZE_PARAMS		:= 4  4   2   2     64  128
PAL_SIZE_PARAMS		:= $(shell echo "${PAL_SIZE_PARAMS}" | tr -s ' ')
PAL_PAGE_SIZE		:= 16384
PAL_OP_RATIO		:= 0.25

NumPages		:= $(shell echo "$(subst $() ,*,${PAL_SIZE_PARAMS})" | bc)
SizeDisk		:= $(shell echo "${NumPages} * ${PAL_PAGE_SIZE}" | bc)
SizeDisk_MiB		:= $(shell echo "${SizeDisk} / 1024 / 1024" | bc)
SizeDisk_GiB		:= $(shell echo "${SizeDisk_MiB} / 1024" | bc)
NumUsablePages		:= $(shell echo "${NumPages} * (1 - ${PAL_OP_RATIO})" | bc)
SizeUsableDisk		:= $(shell printf "%.0f" `echo "${NumUsablePages} * ${PAL_PAGE_SIZE}" | bc`)
SizeUsableDisk_KiB	:= $(shell echo "${SizeUsableDisk} / 1024" | bc)
SizeUsableDisk_MiB	:= $(shell echo "${SizeUsableDisk} / 1024 / 1024" | bc)
SizeUsableDisk_GiB	:= $(shell echo "${SizeUsableDisk_MiB} / 1024" | bc)

DISK_FILE		?= /tmp/nvme.img
${DISK_FILE}:
	@printf " Capacity | %6s | %8s | %7s |\n" " GiBs " " MiBs " " Pages "
	@printf "     Full | %6.2f | %8.2f | %7.0f |\n" ${SizeDisk_GiB} ${SizeDisk_MiB} ${NumPages}
	@printf "   Usable | %6.2f | %8.2f | %7.0f |\n" ${SizeUsableDisk_GiB} ${SizeUsableDisk_MiB} ${NumUsablePages}

	dd if=/dev/zero of=$(shell realpath ${DISK_FILE}) bs=1MiB count=${SizeUsableDisk_MiB}

disk: ${DISK_FILE}

SCRIPT_DIR		:= $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
EXT4_CONF		?= "${SCRIPT_DIR}/mke2fs.conf"
EXT4_BIN		?= /sbin/mkfs.ext4
ext4: ${DISK_FILE}
	@echo "script dir: ${SCRIPT_DIR}"
	MKE2FS_CONFIG=${EXT4_CONF} ${EXT4_BIN} ${DISK_FILE}

EXT4_INIT_SCRIPT_DIR	?= ${SCRIPT_DIR}/ext4-init-scripts
EXT4_INIT_SCRIPT_FILES	?= statdir-layout.sh
EXT4_INIT_SCRIPT_PATHS	:= $(addprefix ${EXT4_INIT_SCRIPT_DIR}/,${EXT4_INIT_SCRIPT_FILES})
ext4-init: ext4 ${EXT4_INIT_SCRIPT_PATHS}
	@for s in ${EXT4_INIT_SCRIPT_PATHS}; do $$s ${DISK_FILE} || exit $?; done
