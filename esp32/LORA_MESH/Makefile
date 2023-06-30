# Makefile

# usage:
#   make compile [BOARD=heltec], default is for T-Beam
#   make upload  SERIAL=/dev/tty.....
#   make monitor SERIAL=/dev/tty.....


BINDIR=/opt/homebrew/bin
#BINDIR=/usr/local/bin
MAIN=LORA_MESH


FQBN=esp32:esp32:t-beam   # default
ifeq ($(BOARD),heltec)
  FQBN=esp32:esp32:heltec_wifi_lora_32_V2
endif

ifndef SERIAL
  ifeq ($(BOARD),heltec) # heltec LORA32
    SERIAL = $(shell ls /dev/tty.usbserial-*)
  else # T-Beam
    SERIAL = $(shell ls /dev/tty.wchusbserial*)
  endif
endif

ifeq ($(BOARD),heltec)
  PARTITIONS_CSV=partitions-6.5M.csv
else
  PARTITIONS_CSV=partitions-2.5M.csv
endif

all: compile

compile:
	rm -f partitions.csv
	ln -s $(PARTITIONS_CSV) partitions.csv
	$(BINDIR)/arduino-cli compile -v --fqbn $(FQBN) $(MAIN).ino \
		--build-properties build.partitions=partitions.csv,upload.maximum_size=1572864

upload:
	$(foreach var,$(SERIAL), $(BINDIR)/arduino-cli upload -v --fqbn $(FQBN) -p $(var) .;)

monitor:
	$(info use CTRL-A CTRL-\ to quit the screen program)
	screen $(SERIAL) 115200

clean:
	rm -rf *~

# eof
