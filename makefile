all: build
.PHONY: all

dist/temp.pass:
	@echo "Generate shared password"
	mkdir -p dist
	hexdump -n16 -e '16/1 "%02X\n" "\n"' /dev/random > dist/temp.pass

PASSWORDS := dist/arduino.pass dist/js.pass
$(PASSWORDS): dist/temp.pass
	@echo "Create $@"
	tr -d '\n' < dist/temp.pass > dist/js.pass
	sed \
		-e 's/\(^.\{2\}\)/0x\1/' \
		-e '$$!s/$$/,/' \
		dist/temp.pass > dist/arduino.pass

# Random 12 bytes to use on first request
ARDUINO_IV = ${shell \
	hexdump -n12 -s1b -e '12/1 "0x%02X\n" "\n"' /dev/random | \
	sed -e '$$!s/$$/,/' \
}

dist/sketch.ino: sketch.ino dist/arduino.pass
	mkdir -p ${ARDUINO_SKETCH_DIR}
	mkdir -p dist
	sed \
		-e 's/{{NETWORK_NAME}}/${NETWORK_NAME}/' \
		-e 's/{{NETWORK_PASS}}/${NETWORK_PASS}/' \
		-e 's|{{SERVER}}|${SERVER}|' \
		-e 's/{{PORT}}/${PORT}/' \
		-e 's/{{ARDUINO_PASS}}/${shell cat ./dist/arduino.pass}/' \
		-e 's/{{ARDUINO_IV}}/${ARDUINO_IV}/' \
		sketch.ino > dist/sketch.ino
	cp dist/sketch.ino ${ARDUINO_SKETCH_DIR}/plant_monitor.ino

dist/index.js: index.js dist/js.pass serviceAccountKey.json
	mkdir -p dist
	sed \
		-e 's|{{DATABASE_URL}}|${DATABASE_URL}|' \
		-e 's/{{JS_PASS}}/${shell cat ./dist/js.pass}/' \
		index.js > dist/index.js
	cp ./serviceAccountKey.json dist/serviceAccountKey.json

dist/public/index.html: public/index.html
	mkdir -p dist/public
	sed \
		-e 's|{{SERVER_URL}}|${SERVER_URL}|' \
		public/index.html > dist/public/index.html

build: dist/sketch.ino dist/index.js dist/public/index.html

clean:
	rm -rf dist
.PHONY: clean

serve: dist/index.js dist/public/index.html
	yarn start
