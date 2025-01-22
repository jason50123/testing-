SHELL := /bin/bash

UTILS		:= utils/argparse utils/runtime
UTIL_OBJS	:= $(addsuffix .o, $(UTILS))
APPS		:= $(shell echo {md5,grep,stats}-{host,slet,fsa}) statdir-host statdir-fsa

CXXFLAGS_EX	:=
CXXFLAGS	:= -Wall -Wextra -Werror -std=c++14 $(CXXFLAGS_EX) -O2

NVME_LIBS	:= nvme m5
NVME_LIB_FLAGS	:= -Llibs $(addprefix -l, ${NVME_LIBS})

LDFLAGS		+= ${NVME_LIB_FLAGS}

all: apps

apps: LDFLAGS += -static
apps: ${APPS} checksum

debug: CXX = clang++
debug: CXXFLAGS = -g -Wall -Wextra -Werror -std=c++14 -DNO_M5 -DISC_DEBUG -fsanitize=undefined,leak,address $(CXXFLAGS_EX)
debug: ${APPS} checksum

${APPS}: %: %.cc ${UTIL_OBJS}
	${CXX} -o $@ $^ ${CXXFLAGS} ${LDFLAGS}

${UTIL_OBJS}: %.o: %.cc %.hh
	${CXX} -o $@ -c $< ${CXXFLAGS}

checksum: ${APPS}
	@md5sum $^

clean:
	${RM} -rf ${APPS} ${UTIL_OBJS}