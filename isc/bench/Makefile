SHELL := /bin/bash

CROSS_COMPILE ?= aarch64-linux-gnu-
CXX := $(CROSS_COMPILE)g++

UTILS		:= utils/argparse utils/runtime
UTIL_OBJS	:= $(addsuffix .o, $(UTILS))

APPS		:= $(shell echo {md5,grep,stats}-{host,slet,fsa}) statdir-host statdir-fsa

CXXFLAGS_EX	:= -Iutils -Iutils/m5
CXXFLAGS	:= -Wall -Wextra -Werror -std=c++14 $(CXXFLAGS_EX) -O2

# 使用 ARM 版本的 lib
NVME_LIBS	:= nvme m5
NVME_LIB_FLAGS	:= -Llibs/arm64 $(addprefix -l, ${NVME_LIBS})

LDFLAGS		+= ${NVME_LIB_FLAGS}

all: apps

apps: LDFLAGS += -static
apps: ${APPS} checksum

debug: CXXFLAGS = -g -Wall -Wextra -Werror -std=c++14 -DNO_M5 -DISC_DEBUG \
	-fsanitize=undefined,leak,address $(CXXFLAGS_EX)
debug: LDFLAGS = -fsanitize=undefined,leak,address

${APPS}: %: %.cc ${UTIL_OBJS}
	${CXX} ${CXXFLAGS} $^ -o $@ ${LDFLAGS}

${UTIL_OBJS}: %.o: %.cc %.hh
	${CXX} ${CXXFLAGS} -c $< -o $@

checksum: ${APPS}
	@md5sum $^

clean:
	${RM} -rf ${APPS} ${UTIL_OBJS}
