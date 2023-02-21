# Makefile

# usage:
#   make compile [BOARD=heltec]
#   make upload  SERIAL=/dev/tty.....
#   make monitor SERIAL=/dev/tty.....


BINDIR=/opt/homebrew/bin
MAIN=LORA_MESH


FQBN=esp32:esp32:t-beam   # default
ifeq ($(BOARD),heltec)
  FQBN=esp32:esp32:heltec_wifi_lora_32_V2
endif

ifndef SERIAL
  ifeq ($(BOARD),heltec)
    SERIAL=/dev/tty.usbserial-0001           # heltec LORA32
  else
    #SERIAL=/dev/tty.wchusbserial54750108741  # fc84
    #SERIAL=/dev/tty.wchusbserial54750108751  # fcd4
    SERIAL=/dev/tty.wchusbserial54750388221  # f9d0
    #SERIAL=/dev/tty.wchusbserial54750389471  # f948
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
	$(info ** uploading to $(SERIAL))
	$(info )
	$(BINDIR)/arduino-cli upload -v --fqbn $(FQBN) -p $(SERIAL) .

monitor:
	$(info use CTRL-A CTRL-\ to quit the screen program)
	screen $(SERIAL) 115200

clean:
	rm -rf *~

# eof
