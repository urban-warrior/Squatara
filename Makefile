# convenience Makefile for PlatformIO project

.DEFAULT: squatara

.PHONY: upload clean

squatara:
	pio run -e feather_esp32s3_revtft

upload:
	@if [ -z "$(PORT)" ];then \
		echo "Must specify PORT=<port device> to upload"; \
		exit 1; \
	fi
	@if ! esptool -p $(PORT) chip-id;then \
		echo "Bad port '$(PORT)'"; \
		exit 2; \
	fi
	pio run -t upload --upload-port $(PORT)

clean:
	pio run -t clean
