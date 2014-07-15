THIS_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
NACL_SDK_ROOT ?= $(abspath $(dir $(THIS_MAKEFILE))../..)

# Project Build flags
WARNINGS := -Wno-long-long -Wall -Wswitch-enum -pedantic

#
# Compute tool paths
#
GETOS := python $(NACL_SDK_ROOT)/tools/getos.py
OSHELPERS = python $(NACL_SDK_ROOT)/tools/oshelpers.py
OSNAME := $(shell $(GETOS))
RM := rm

CXXFLAGS := -I$(NACL_SDK_ROOT)/include
LDFLAGS := -O3 -L$(NACL_SDK_ROOT)/lib/newlib_x86_64/Release -lppapi_cpp -lppapi -lpthread -lopencv_objdetect -lopencv_calib3d -lopencv_features2d -lopencv_imgproc -lopencv_core -lopencv_contrib -lopencv_flann -lopencv_highgui -lz -lnacl_io -ljpeg -lpng
NACL_CXX := $(abspath $(NACL_SDK_ROOT)/toolchain/linux_x86_newlib/bin/x86_64-nacl-g++)

# Declare the ALL target first, to make the 'all' target the default build
all: facedetect.nexe

clean:
	$(RM) facedetect.nexe

facedetect.nexe: facedetect.cc
	$(NACL_CXX) -g -o $@ $< $(WARNINGS) $(CXXFLAGS) $(LDFLAGS)


#
# Makefile target to run the SDK's simple HTTP server and serve this example.
#
HTTPD_PY := python $(NACL_SDK_ROOT)/tools/httpd.py

.PHONY: serve
serve: all
	$(HTTPD_PY) -C $(CURDIR)
