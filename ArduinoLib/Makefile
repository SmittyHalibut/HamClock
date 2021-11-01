# make libarduino.a

# N.B. this Makefile is not suitable for stand-alone use, used only by ../Makefile

.PHONY: default

CXX = g++

OBJS = \
	Adafruit_BME280.o \
	Adafruit_RA8875.o \
	Arduino.o \
	CourierPrimeSans6.o \
	DateStrings.o \
	EEPROM.o \
	ESP.o \
	ESP8266WiFi.o \
	ESP8266httpUpdate.o \
        GPIO.o \
        LittleFS.o \
	Serial.o \
        SPI.o \
	Time.o \
	WiFiClient.o \
	WiFiServer.o \
	WiFiUdp.o \
        Wire.o

default:
	@echo "This Makefile is not intended for stand-alone use -- use only from HamClock's main Makefile"

libarduino.a: $(OBJS)
	rm -f $@
	ar r $@ $(OBJS)
	ranlib $@

clean:
	touch x.o x.a
	rm -f *.o *.a
