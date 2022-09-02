# Makefile


BINDIR=/opt/homebrew/bin
FQBN=esp32:esp32:t-beam
# SERIAL=/dev/tty.wchusbserial54750108741
# SERIAL=/dev/tty.wchusbserial54750108741
SERIAL=/dev/cu.wchusbserial54750389471

all: compile

compile:
	$(BINDIR)/arduino-cli compile -v --fqbn $(FQBN) vossbol_tbeam.ino \
		--build-properties build.partitions=partitions.csv,upload.maximum_size=1572864

upload:
	$(BINDIR)/arduino-cli upload -v --fqbn $(FQBN) -p $(SERIAL) .

monitor:
	screen $(SERIAL) 115200
	# use CTRL-A CTRL-\ to quit the screen program

clean:
	rm -rf *~

# eof
