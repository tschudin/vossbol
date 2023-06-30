# Makefile

# usage:
#   make compile [SERIAL=/dev/tty...] [BOARD=heltec]    default is for T-Beam
#   make upload  [SERIAL=/dev/tty...]
#   make monitor [SERIAL=/dev/tty...]


# place where the arduino-cli executable sits:
BINDIR=/opt/homebrew/bin    # /usr/local/bin

# name of the ino file:
MAIN=LORA_MESH.ino

ifeq ($(BOARD),heltec)
  FQBN=esp32:esp32:heltec_wifi_lora_32_V2
  PARTITIONS_CSV=partitions-6.5M.csv
else
  FQBN=esp32:esp32:t-beam   # default is T-Beam
  PARTITIONS_CSV=partitions-2.5M.csv
endif

ifndef SERIAL
  ifeq ($(BOARD),heltec) # heltec LORA32
    SERIAL = $(shell ls /dev/tty.usbserial-*)
  else # T-Beam
    SERIAL = $(shell ls /dev/tty.wchusbserial*)
  endif
endif

# ----------------------------------------------------------------------

all: compile

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

clean:
	rm -rf *~

# eof
