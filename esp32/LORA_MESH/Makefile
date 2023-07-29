# Makefile

# usage:
#   make compile [SERIAL=/dev/tty...] [BOARD=heltec]    default is for T-Beam
#   make upload  [SERIAL=/dev/tty...]
#   make monitor [SERIAL=/dev/tty...]

#   make firmware [BOARD=heltec]   --> build/merged-firmware.bin
#   make flash    [BOARD=heltec]


# place where the arduino-cli executable sits:
# BINDIR=/usr/local/bin
BINDIR=/opt/homebrew/bin

ESPTOOL=/Users/tschudin/Library/Arduino15/packages/esp32/tools/esptool_py/4.5.1/esptool

# name of the ino file:
MAIN=LORA_MESH.ino

ifeq ($(BOARD),heltec)
  FQBN=esp32:esp32:heltec_wifi_lora_32_V2
  PARTITIONS_CSV=partitions-6.5M.csv
  FLASH_SIZE=8MB
else
  FQBN=esp32:esp32:t-beam   # default is T-Beam
  PARTITIONS_CSV=partitions-2.5M.csv
  FLASH_SIZE=4MB
endif

ifndef SERIAL
  ifeq ($(BOARD),heltec) # heltec LORA32
    SERIAL = $(shell ls /dev/tty.usbserial-*)
  else # T-Beam
    SERIAL = $(shell ls /dev/tty.wchusbserial*)
  endif
endif

# ----------------------------------------------------------------------

all:
	$(info Either use "make compile && make upload" or "make firmware && make flash")
	$(info ...option: "BOARD=heltec", default is T-Beam)


compile:
	rm -f partitions.csv
	ln -s $(PARTITIONS_CSV) partitions.csv
	$(BINDIR)/arduino-cli compile -v --fqbn $(FQBN) $(MAIN) \
		--build-properties build.partitions=partitions.csv,upload.maximum_size=1572864

upload:
	$(foreach var,$(SERIAL), $(BINDIR)/arduino-cli upload -v --fqbn $(FQBN) -p $(var) .;)

monitor:
	$(info use CTRL-A CTRL-\ to quit the screen program)
	screen $(SERIAL) 115200

# ---------------------------------------------------------------------------

firmware:
	$(BINDIR)/arduino-cli compile -v \
		--fqbn $(FQBN) LORA_MESH.ino \
		--build-property build.partitions=$(PARTITIONS_CSV) \
		--build-property upload.maximum_size=1572864 \
		--build-path ./build   && \
	$(ESPTOOL) \
		--chip esp32 merge_bin --flash_mode dio --flash_freq 80m \
		--flash_size $(FLASH_SIZE) \
		0x1000 ./build/LORA_MESH.ino.bootloader.bin \
		0x8000 ./build/LORA_MESH.ino.partitions.bin \
		0xe000 /Users/tschudin/Library/Arduino15/packages/esp32/hardware/esp32/2.0.9/tools/partitions/boot_app0.bin \
		0x10000 ./build/LORA_MESH.ino.bin \
		-o ./build/merged-firmware.bin

flash:
	$(foreach var,$(SERIAL), \
		/Users/tschudin/Library/Arduino15/packages/esp32/tools/esptool_py/4.5.1/esptool \
		--chip esp32 \
		--port $(var) --baud 921600 \
		--before default_reset --after hard_reset \
		write_flash -z --flash_mode dio --flash_freq 80m \
		--flash_size $(FLASH_SIZE) \
		0x0 ./build/merged-firmware.bin;)

clean:
	rm -rf *~

# eof
