BDIR                    := build
TARGET                  := runtime

CXX                     := clang++ # g++ did't warn -Wsometimes-uninitialized
CXXDEFS                 := ISC_TEST ISC_TEST_NO_XXD ISC_TEST_NO_DPR
CXXDEFINES              := $(addprefix -D, $(CXXDEFS))
CXXSANS                 := -fsanitize=undefined,leak,address,integer,implicit-conversion
CXXWARNS                := -Wall -Wextra -Werror

CXXFLAGS_EX             := # used for adding/overriding options
CXXFLAGS                = -std=c++11 -I. $(CXXWARNS) $(CXXSANS) $(CXXDEFINES) $(CXXFLAGS_EX)

LD_DIRS                 :=
LD_SHARED_LIBS          := ext2fs com_err z zstd dl
LD_STATIC_LIBS          := utils/lib/libcpptrace.a utils/lib/libdwarf.a
LDFLAGS_EX              :=
LDFLAGS                 = $(addprefix -L,$(LD_DIRS)) $(addprefix -l, $(LD_SHARED_LIBS)) $(LD_STATIC_LIBS) $(LDFLAGS_EX)

SIM_SRCS                := sims/ftl sims/dram
UTIL_SRCS               := utils/debug utils/types
SLET_SRCS               := slet/slet slet/grep slet/seqread slet/randread slet/listdir slet/statdir slet/md5 slet/stats32 slet/stats64
FSA_SRCS                := fs/ext4/ext4
ISC_SRCS                := runtime $(SLET_SRCS) $(FSA_SRCS) $(UTIL_SRCS) $(SIM_SRCS)
ISC_OBJS                := $(addsuffix .o, $(addprefix $(BDIR)/, $(basename $(ISC_SRCS))))

main: $(ISC_OBJS)

TEST_SRCS               := test_runtime test_slet test_ext4 test_grep test_statdir test_md5 test_stats3264
TEST_BINS               := $(addprefix $(BDIR)/, $(TEST_SRCS))
test: gtest $(TEST_BINS)
		@for tst in $(TEST_BINS); do ./$$tst $(GTEST_FLAGS) || exit; done

VG_BIN                  := $(shell which valgrind)
VG_FLAGS                := -s --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1
VALGRIND                = $(VG_BIN) $(VG_FLAGS)
vtest: CXXSANS  =
vtest: gtest $(TEST_BINS)
		for tst in $(TEST_BINS); do $(VALGRIND) $$tst ./$$tst $(GTEST_FLAGS) || exit; done

GTEST_DIR               := test/googletest
GTEST_INC_DIR           := $(GTEST_DIR)/googletest/include
GTEST_LIB_DIR           := $(GTEST_DIR)/build/lib
GTEST_CASE              := '*'
GTEST_FLAGS_EX          := --gtest_break_on_failure
GTEST_FLAGS             := --gtest_filter=$(GTEST_CASE) $(GTEST_FLAGS_EX)
GTEST_LIBS              := $(GTEST_LIB_DIR)/libgtest_main.a $(GTEST_LIB_DIR)/libgtest.a -lpthread
gtest:
		@git submodule update --init
		@cmake $(GTEST_DIR) -B $(GTEST_DIR)/build && cmake --build $(GTEST_DIR)/build

$(TEST_BINS): $(BDIR)/%: test/%.cc $(ISC_OBJS)
		@mkdir -p $(dir $@)
		$(CXX) $(CXXFLAGS) -I$(GTEST_INC_DIR) -o $@ $^ $(GTEST_LIBS) $(LDFLAGS)

$(ISC_OBJS): $(BDIR)/%.o: %.cc
		@mkdir -p $(dir $@)
		$(CXX) $(CXXFLAGS) -o $@ -c $^

clean:
		rm -rf $(BDIR)
