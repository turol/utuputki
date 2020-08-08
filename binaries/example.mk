# example of local configuration
# copy to local.mk


# location of source
TOPDIR:=..


LTO:=n
ASAN:=n
TSAN:=n
UBSAN:=n


# compiler options etc
CXX:=g++
CC:=gcc
CFLAGS:=-g -Wall -Wextra -Wshadow
CFLAGS+=-Wno-unused-local-typedefs
CFLAGS+=-fvisibility=hidden   # needed to fix warning caused by pybind
CFLAGS+=$(shell pkg-config --cflags libvlc)
CFLAGS+=$(shell pkg-config --cflags python3)
CFLAGS+=$(shell pkg-config --cflags sqlite3)

# for development
#CFLAGS+=-DOVERRIDE_TEMPLATES

OPTFLAGS:=-Os
OPTFLAGS+=-march=native
# OPTFLAGS+=-ffunction-sections -fdata-sections


# lazy assignment because CFLAGS is changed later
CXXFLAGS=$(CFLAGS)
CXXFLAGS+=-std=c++14


LDFLAGS:=-g
#LDFLAGS+=-Wl,--gc-sections
# you can enable this if you're using gold linker
#LFDLAGS+=-Wl,--icf=all
LDLIBS:=-lpthread
LDLIBS_libvlc:=$(shell pkg-config --libs libvlc)
LDLIBS_python+=$(shell pkg-config --libs python3) $(shell pkg-config --libs python3-embed)
LDLIBS_sqlite3:=$(shell pkg-config --libs sqlite3)

LTOCFLAGS:=-flto -fuse-linker-plugin -fno-fat-lto-objects
LTOLDFLAGS:=-flto -fuse-linker-plugin


OBJSUFFIX:=.o
EXESUFFIX:=-bin
