/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu/cpu.hh"

#include <limits>

#include "sim/trace.hh"

// isc configs
#include "isc/sims/configs.hh"
#include "isc/utils/debug.hh"

namespace SimpleSSD {

namespace CPU {

#define enum2str(x) #x
#define SAME_STR(literal, s) (!const_strncmp(literal, s, sizeof(literal) - 1))
#define CHECK_ENUM_STRS(e, s) static_assert(SAME_STR(#e, s), "MISALIGNED")

#define ARRAY2D_SZ(arr) (sizeof((arr)) / sizeof((arr)[0]))
#define CHECK_NS2STR(e)                                                        \
  CHECK_ENUM_STRS(e, NS2STR[e]);                                               \
  static_assert(ARRAY2D_SZ(NS2STR) == TOTAL_NAMESPACES + 1, "")
#define CHECK_FCT2STR(e)                                                       \
  CHECK_ENUM_STRS(e, FCT2STR[e]);                                              \
  static_assert(ARRAY2D_SZ(FCT2STR) == TOTAL_FUNCTIONS + 1, "")

constexpr const char *NS2STR[] = {
    enum2str(FTL),
    enum2str(FTL__PAGE_MAPPING),
    enum2str(ICL),
    enum2str(ICL__GENERIC_CACHE),
    enum2str(HIL),
    enum2str(NVME__CONTROLLER),
    enum2str(NVME__PRPLIST),
    enum2str(NVME__SGL),
    enum2str(NVME__SUBSYSTEM),
    enum2str(NVME__NAMESPACE),
    enum2str(NVME__OCSSD),
    enum2str(UFS__DEVICE),
    enum2str(SATA__DEVICE),
    enum2str(ISC__RUNTIME),
    enum2str(ISC__FSA),
    enum2str(ISC__FSA__EXT4),
    enum2str(ISC__SLET),
    enum2str(ISC__SLET__GREP),
    enum2str(ISC__SLET__LISTDIR),
    enum2str(ISC__SLET__STATDIR),
    enum2str(TOTAL_NAMESPACES),
};

constexpr const char *FCT2STR[] = {
    enum2str(READ),
    enum2str(WRITE),
    enum2str(FLUSH),
    enum2str(TRIM),
    enum2str(FORMAT),
    enum2str(READ_INTERNAL),
    enum2str(WRITE_INTERNAL),
    enum2str(ERASE_INTERNAL),
    enum2str(TRIM_INTERNAL),
    enum2str(SELECT_VICTIM_BLOCK),
    enum2str(DO_GARBAGE_COLLECTION),
    enum2str(CREATE_CQ),
    enum2str(CREATE_SQ),
    enum2str(COLLECT_SQ),
    enum2str(HANDLE_REQUEST),
    enum2str(WORK),
    enum2str(COMPLETION),
    enum2str(GET_PRPLIST_FROM_PRP),
    enum2str(PARSE_SGL_SEGMENT),
    enum2str(SUBMIT_COMMAND),
    enum2str(CONVERT_UNIT),
    enum2str(FORMAT_NVM),
    enum2str(DATASET_MANAGEMENT),
    enum2str(VECTOR_CHUNK_READ),
    enum2str(VECTOR_CHUNK_WRITE),
    enum2str(VECTOR_CHUNK_RESET),
    enum2str(PHYSICAL_PAGE_READ),
    enum2str(PHYSICAL_PAGE_WRITE),
    enum2str(PHYSICAL_BLOCK_ERASE),
    enum2str(PROCESS_QUERY_COMMAND),
    enum2str(PROCESS_COMMAND),
    enum2str(PRDT_READ),
    enum2str(PRDT_WRITE),
    enum2str(READ_DMA),
    enum2str(READ_NCQ),
    enum2str(READ_DMA_SETUP),
    enum2str(READ_DMA_DONE),
    enum2str(WRITE_DMA),
    enum2str(WRITE_NCQ),
    enum2str(WRITE_DMA_SETUP),
    enum2str(WRITE_DMA_DONE),
    enum2str(ISC_GET),
    enum2str(ISC_SET),
    enum2str(ISC__INIT),
    enum2str(ISC__GET_SUPER),
    enum2str(ISC__GET_GROUP),
    enum2str(ISC__GET_IMAP),
    enum2str(ISC__GET_INODE),
    enum2str(ISC__GET_INODE_PARENT),
    enum2str(ISC__GET_EXTENT_SIZE),
    enum2str(ISC__GET_EXTENT_INTERNAL),
    enum2str(ISC__GET_EXTENT),
    enum2str(ISC__DIR_SEARCH_FILE),
    enum2str(ISC__NAMEI),
    enum2str(ISC__START_SLET),
    enum2str(ISC__SET_OPT),
    enum2str(ISC__GET_OPT),
    enum2str(ISC__ADD_SLET__EXT4),
    enum2str(ISC__ADD_SLET__GREP),
    enum2str(ISC__ADD_SLET__LISTDIR),
    enum2str(ISC__ADD_SLET__STATDIR),
    enum2str(TOTAL_FUNCTIONS),
};

CHECK_NS2STR(ISC__RUNTIME);
CHECK_NS2STR(ISC__FSA);
CHECK_NS2STR(ISC__SLET);
CHECK_NS2STR(ISC__SLET__GREP);     // slet start
CHECK_NS2STR(ISC__SLET__STATDIR);  // slet end
CHECK_NS2STR(TOTAL_NAMESPACES);
CHECK_FCT2STR(ISC__INIT);               // FSA start
CHECK_FCT2STR(ISC__NAMEI);              // FSA end
CHECK_FCT2STR(ISC__START_SLET);         // runtime start
CHECK_FCT2STR(ISC__ADD_SLET__STATDIR);  // runtime end
CHECK_FCT2STR(TOTAL_FUNCTIONS);

InstStat::_InstStat()
    : branch(0),
      load(0),
      store(0),
      arithmetic(0),
      floatingPoint(0),
      otherInsts(0),
      latency(0) {}

InstStat::_InstStat(uint64_t b, uint64_t l, uint64_t s, uint64_t a, uint64_t f,
                    uint64_t o, uint64_t c)
    : branch(b),
      load(l),
      store(s),
      arithmetic(a),
      floatingPoint(f),
      otherInsts(o) {
  latency = sum();
  latency *= c;
}

InstStat &InstStat::operator+=(const InstStat &rhs) {
  this->branch += rhs.branch;
  this->load += rhs.load;
  this->store += rhs.store;
  this->arithmetic += rhs.arithmetic;
  this->floatingPoint += rhs.floatingPoint;
  this->otherInsts += rhs.otherInsts;
  // Do not add up totalcycles

  return *this;
}

uint64_t InstStat::sum() {
  return branch + load + store + arithmetic + floatingPoint + otherInsts;
}

JobEntry::_JobEntry(DMAFunction &f, void *c, InstStat *i)
    : func(f), context(c), inst(i) {}

CPU::CoreStat::CoreStat() : busy(0) {}

CPU::Core::Core() : busy(false) {
  jobEvent = allocate([this](uint64_t) { jobDone(); });
}

CPU::Core::~Core() {}

bool CPU::CPU::Core::isBusy() {
  return busy;
}

uint64_t CPU::CPU::Core::getJobListSize() {
  return jobs.size();
}

CPU::CoreStat &CPU::Core::getStat() {
  return stat;
}

void CPU::Core::submitJob(JobEntry job, uint64_t delay) {
  job.delay = delay;
  job.submitAt = getTick();
  jobs.push(job);

  if (!busy) {
    handleJob();
  }
}

void CPU::Core::handleJob() {
  auto &iter = jobs.front();
  uint64_t now = getTick();
  uint64_t diff = now - iter.submitAt;
  uint64_t finishedAt;

  if (diff >= iter.delay) {
    finishedAt = now + iter.inst->latency;
  }
  else {
    finishedAt = now + iter.inst->latency + iter.delay - diff;
  }

  busy = true;

  schedule(jobEvent, finishedAt);
}

void CPU::Core::jobDone() {
  auto &iter = jobs.front();

  iter.func(getTick(), iter.context);
  stat.busy += iter.inst->latency;
  stat.instStat += *iter.inst;

  jobs.pop();
  busy = false;

  if (jobs.size() > 0) {
    handleJob();
  }
}

void CPU::Core::addStat(InstStat &inst) {
  stat.busy += inst.latency;
  stat.instStat += inst;
}

CPU::CPU(ConfigReader &c) : conf(c), lastResetStat(0) {
  clockSpeed = conf.readUint(CONFIG_CPU, CPU_CLOCK);
  clockPeriod = 1000000000000. / clockSpeed;  // in pico-seconds

  hilCore.resize(conf.readUint(CONFIG_CPU, CPU_CORE_HIL));
  iclCore.resize(conf.readUint(CONFIG_CPU, CPU_CORE_ICL));
  ftlCore.resize(conf.readUint(CONFIG_CPU, CPU_CORE_FTL) - NUM_ISC_CORES);
  iscCore.resize(NUM_ISC_CORES);

  assert(ftlCore.size() > 0 || !"Number of FTL Cores not expected to be zero");
  assert(iscCore.size() > 0 || !"Number of ISC Cores not expected to be zero");

  // Initialize CPU table
  cpi.insert({FTL, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({FTL__PAGE_MAPPING, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ICL, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ICL__GENERIC_CACHE, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({HIL, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__CONTROLLER, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__PRPLIST, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__SGL, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__SUBSYSTEM, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__NAMESPACE, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__OCSSD, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({UFS__DEVICE, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({SATA__DEVICE, std::unordered_map<uint16_t, InstStat>()});

  // Insert item (Use cpu/generator/generate.py to generate this code)
  cpi.find(0)->second.insert({0, InstStat(5, 32, 6, 13, 0, 1, clockPeriod)});
  cpi.find(0)->second.insert({1, InstStat(5, 32, 6, 13, 0, 1, clockPeriod)});
  cpi.find(0)->second.insert({3, InstStat(5, 32, 6, 13, 0, 1, clockPeriod)});
  cpi.find(0)->second.insert({4, InstStat(4, 24, 4, 6, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert({0, InstStat(8, 28, 7, 18, 0, 1, clockPeriod)});
  cpi.find(1)->second.insert({1, InstStat(8, 28, 7, 19, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert({3, InstStat(4, 28, 6, 11, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert(
      {4, InstStat(63, 180, 21, 147, 0, 2, clockPeriod)});
  cpi.find(1)->second.insert(
      {9, InstStat(177, 504, 113, 415, 118, 19, clockPeriod)});
  cpi.find(1)->second.insert(
      {10, InstStat(157, 616, 102, 338, 0, 2, clockPeriod)});
  cpi.find(1)->second.insert(
      {5, InstStat(45, 180, 15, 155, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert(
      {6, InstStat(133, 452, 54, 377, 91, 1, clockPeriod)});
  cpi.find(1)->second.insert(
      {8, InstStat(34, 140, 10, 146, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert(
      {7, InstStat(120, 236, 86, 260, 0, 1, clockPeriod)});
  cpi.find(2)->second.insert({0, InstStat(8, 88, 17, 27, 0, 1, clockPeriod)});
  cpi.find(2)->second.insert({1, InstStat(8, 88, 17, 27, 0, 1, clockPeriod)});
  cpi.find(2)->second.insert({2, InstStat(5, 40, 6, 12, 0, 0, clockPeriod)});
  cpi.find(2)->second.insert({3, InstStat(5, 40, 6, 12, 0, 0, clockPeriod)});
  cpi.find(2)->second.insert({4, InstStat(5, 40, 6, 12, 0, 0, clockPeriod)});
  cpi.find(3)->second.insert(
      {0, InstStat(90, 532, 64, 284, 0, 1, clockPeriod)});
  cpi.find(3)->second.insert(
      {1, InstStat(82, 496, 53, 312, 0, 5, clockPeriod)});
  cpi.find(3)->second.insert({2, InstStat(22, 120, 20, 59, 0, 2, clockPeriod)});
  cpi.find(3)->second.insert({3, InstStat(22, 120, 20, 61, 0, 2, clockPeriod)});
  cpi.find(3)->second.insert({4, InstStat(9, 72, 12, 86, 0, 1, clockPeriod)});
  cpi.find(4)->second.insert(
      {0, InstStat(61, 312, 102, 120, 0, 2, clockPeriod)});
  cpi.find(4)->second.insert(
      {1, InstStat(61, 312, 102, 120, 0, 2, clockPeriod)});
  cpi.find(4)->second.insert({2, InstStat(27, 100, 27, 49, 0, 1, clockPeriod)});
  cpi.find(5)->second.insert(
      {14, InstStat(44, 164, 32, 68, 0, 2, clockPeriod)});
  cpi.find(5)->second.insert({13, InstStat(0, 0, 0, 0, 0, 0, clockPeriod)});
  cpi.find(5)->second.insert(
      {16, InstStat(136, 360, 65, 230, 0, 3, clockPeriod)});
  cpi.find(5)->second.insert(
      {15, InstStat(54, 140, 36, 91, 0, 8, clockPeriod)});
  cpi.find(5)->second.insert({11, InstStat(0, 0, 0, 0, 0, 0, clockPeriod)});
  cpi.find(5)->second.insert({12, InstStat(0, 0, 0, 0, 0, 0, clockPeriod)});
  cpi.find(6)->second.insert(
      {17, InstStat(41, 168, 42, 75, 0, 1, clockPeriod)});
  cpi.find(6)->second.insert(
      {0, InstStat(99, 456, 94, 177, 0, 6, clockPeriod)});
  cpi.find(6)->second.insert(
      {1, InstStat(99, 456, 94, 177, 0, 6, clockPeriod)});
  cpi.find(7)->second.insert(
      {18, InstStat(44, 152, 35, 78, 0, 2, clockPeriod)});
  cpi.find(7)->second.insert(
      {0, InstStat(99, 456, 94, 177, 0, 6, clockPeriod)});
  cpi.find(7)->second.insert(
      {1, InstStat(99, 456, 94, 177, 0, 6, clockPeriod)});
  cpi.find(8)->second.insert(
      {19, InstStat(119, 220, 45, 160, 0, 6, clockPeriod)});
  cpi.find(8)->second.insert({20, InstStat(4, 40, 14, 110, 0, 1, clockPeriod)});
  cpi.find(8)->second.insert(
      {21, InstStat(70, 200, 42, 161, 0, 1, clockPeriod)});
  cpi.find(9)->second.insert({19, InstStat(27, 44, 5, 37, 0, 0, clockPeriod)});
  cpi.find(9)->second.insert(
      {0, InstStat(82, 292, 42, 128, 0, 4, clockPeriod)});
  cpi.find(9)->second.insert(
      {1, InstStat(86, 304, 47, 141, 0, 3, clockPeriod)});
  cpi.find(9)->second.insert({2, InstStat(51, 124, 28, 78, 0, 3, clockPeriod)});
  cpi.find(9)->second.insert(
      {22, InstStat(131, 364, 71, 200, 0, 7, clockPeriod)});
  cpi.find(10)->second.insert(
      {19, InstStat(155, 100, 12, 208, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {0, InstStat(93, 284, 60, 146, 0, 5, clockPeriod)});
  cpi.find(10)->second.insert(
      {1, InstStat(95, 276, 60, 150, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {22, InstStat(119, 328, 76, 186, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {5, InstStat(54, 172, 69, 89, 0, 1, clockPeriod)});
  cpi.find(10)->second.insert(
      {6, InstStat(72, 236, 77, 141, 0, 3, clockPeriod)});
  cpi.find(10)->second.insert(
      {7, InstStat(68, 204, 77, 116, 0, 1, clockPeriod)});
  cpi.find(10)->second.insert(
      {20, InstStat(65, 388, 63, 303, 0, 1, clockPeriod)});
  cpi.find(10)->second.insert(
      {23, InstStat(128, 368, 76, 204, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {24, InstStat(128, 384, 81, 209, 0, 6, clockPeriod)});
  cpi.find(10)->second.insert(
      {25, InstStat(69, 184, 43, 112, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {26, InstStat(206, 692, 157, 315, 0, 5, clockPeriod)});
  cpi.find(10)->second.insert(
      {27, InstStat(183, 620, 154, 284, 0, 6, clockPeriod)});
  cpi.find(10)->second.insert(
      {28, InstStat(162, 460, 78, 227, 0, 4, clockPeriod)});
  cpi.find(11)->second.insert(
      {29, InstStat(51, 132, 40, 97, 0, 0, clockPeriod)});
  cpi.find(11)->second.insert(
      {30, InstStat(212, 460, 117, 491, 0, 9, clockPeriod)});
  cpi.find(11)->second.insert(
      {31, InstStat(42, 172, 43, 74, 0, 2, clockPeriod)});
  cpi.find(11)->second.insert(
      {32, InstStat(42, 172, 43, 74, 0, 2, clockPeriod)});
  cpi.find(11)->second.insert({0, InstStat(29, 76, 17, 51, 0, 2, clockPeriod)});
  cpi.find(11)->second.insert({1, InstStat(29, 76, 17, 51, 0, 2, clockPeriod)});
  cpi.find(11)->second.insert({2, InstStat(25, 64, 18, 44, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {19, InstStat(157, 352, 69, 178, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {31, InstStat(42, 172, 43, 73, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert(
      {32, InstStat(42, 172, 43, 73, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert(
      {0, InstStat(28, 84, 23, 119, 0, 0, clockPeriod)});
  cpi.find(12)->second.insert(
      {1, InstStat(28, 84, 23, 120, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert({2, InstStat(25, 64, 18, 44, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {33, InstStat(57, 212, 36, 128, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert(
      {34, InstStat(34, 116, 29, 72, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert({35, InstStat(16, 64, 9, 38, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {36, InstStat(28, 72, 15, 48, 0, 2, clockPeriod)});
  cpi.find(12)->second.insert(
      {37, InstStat(57, 212, 36, 127, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert(
      {38, InstStat(34, 128, 31, 68, 0, 2, clockPeriod)});
  cpi.find(12)->second.insert(
      {39, InstStat(18, 56, 10, 37, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {40, InstStat(33, 100, 17, 61, 0, 3, clockPeriod)});

  // CPIs for ISC module
  cpi.insert({ISC__RUNTIME, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ISC__FSA, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ISC__FSA__EXT4, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ISC__SLET, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ISC__SLET__GREP, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ISC__SLET__LISTDIR, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ISC__SLET__STATDIR, std::unordered_map<uint16_t, InstStat>()});

  cpi.find(9)->second.insert(
      {41, InstStat(94, 408, 58, 170, 0, 2, clockPeriod)});
  cpi.find(8)->second.insert(
      {41, InstStat(39, 144, 28, 137, 0, 2, clockPeriod)});
  cpi.find(4)->second.insert(
      {41, InstStat(44, 176, 39, 91, 0, 1, clockPeriod)});
  cpi.find(9)->second.insert(
      {42, InstStat(89, 372, 57, 136, 0, 1, clockPeriod)});
  cpi.find(8)->second.insert(
      {42, InstStat(45, 180, 32, 147, 0, 1, clockPeriod)});
  cpi.find(4)->second.insert(
      {42, InstStat(115, 532, 104, 244, 0, 4, clockPeriod)});
  cpi.find(15)->second.insert(
      {43, InstStat(28, 124, 35, 203, 0, 1, clockPeriod)});
  cpi.find(15)->second.insert({44, InstStat(11, 20, 6, 30, 0, 0, clockPeriod)});
  cpi.find(15)->second.insert({45, InstStat(8, 20, 7, 31, 0, 0, clockPeriod)});
  cpi.find(15)->second.insert({46, InstStat(15, 44, 8, 61, 0, 1, clockPeriod)});
  cpi.find(15)->second.insert({47, InstStat(19, 60, 9, 66, 0, 0, clockPeriod)});
  cpi.find(15)->second.insert(
      {48, InstStat(18, 84, 16, 58, 0, 0, clockPeriod)});
  cpi.find(15)->second.insert(
      {49, InstStat(25, 84, 17, 71, 0, 2, clockPeriod)});
  cpi.find(15)->second.insert(
      {50, InstStat(26, 112, 29, 83, 0, 1, clockPeriod)});
  cpi.find(15)->second.insert(
      {51, InstStat(32, 92, 20, 80, 0, 0, clockPeriod)});
  cpi.find(15)->second.insert(
      {52, InstStat(39, 104, 22, 112, 0, 2, clockPeriod)});
  cpi.find(15)->second.insert(
      {53, InstStat(23, 52, 13, 67, 0, 2, clockPeriod)});
  cpi.find(13)->second.insert(
      {47, InstStat(17, 64, 11, 48, 0, 0, clockPeriod)});
  cpi.find(13)->second.insert(
      {51, InstStat(17, 56, 10, 51, 0, 1, clockPeriod)});
  cpi.find(13)->second.insert({54, InstStat(15, 40, 6, 37, 0, 1, clockPeriod)});
  cpi.find(13)->second.insert({55, InstStat(11, 28, 5, 26, 0, 1, clockPeriod)});
  cpi.find(13)->second.insert({56, InstStat(11, 28, 5, 26, 0, 0, clockPeriod)});
  cpi.find(13)->second.insert({57, InstStat(8, 28, 9, 25, 0, 0, clockPeriod)});
  cpi.find(15)->second.insert({54, InstStat(1, 0, 0, 1, 0, 0, clockPeriod)});
  cpi.find(13)->second.insert(
      {58, InstStat(11, 44, 13, 36, 0, 0, clockPeriod)});
  cpi.find(17)->second.insert(
      {54, InstStat(24, 44, 10, 66, 0, 1, clockPeriod)});
  cpi.find(13)->second.insert(
      {59, InstStat(11, 44, 26, 40, 0, 0, clockPeriod)});
  cpi.find(18)->second.insert(
      {54, InstStat(19, 56, 13, 51, 0, 2, clockPeriod)});
  cpi.find(13)->second.insert(
      {60, InstStat(13, 44, 13, 39, 0, 0, clockPeriod)});
  cpi.find(19)->second.insert(
      {54, InstStat(28, 148, 23, 76, 0, 4, clockPeriod)});

#define ERR_MSG "Unexpected FUNCTION ID"
  static_assert(FUNCTION::ISC__GET == 41, ERR_MSG);
  static_assert(FUNCTION::ISC__SET == 42, ERR_MSG);
  static_assert(FUNCTION::ISC__INIT == 43, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_SUPER == 44, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_GROUP == 45, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_IMAP == 46, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_INODE == 47, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_INODE_PARENT == 48, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_EXTENT_SIZE == 49, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_EXTENT_INTERNAL == 50, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_EXTENT == 51, ERR_MSG);
  static_assert(FUNCTION::ISC__DIR_SEARCH_FILE == 52, ERR_MSG);
  static_assert(FUNCTION::ISC__NAMEI == 53, ERR_MSG);
  static_assert(FUNCTION::ISC__START_SLET == 54, ERR_MSG);
  static_assert(FUNCTION::ISC__SET_OPT == 55, ERR_MSG);
  static_assert(FUNCTION::ISC__GET_OPT == 56, ERR_MSG);
  static_assert(FUNCTION::ISC__ADD_SLET__EXT4 == 57, ERR_MSG);
  static_assert(FUNCTION::ISC__ADD_SLET__GREP == 58, ERR_MSG);
  static_assert(FUNCTION::ISC__ADD_SLET__LISTDIR == 59, ERR_MSG);
  static_assert(FUNCTION::ISC__ADD_SLET__STATDIR == 60, ERR_MSG);
#undef ERR_MSG
#define ERR_MSG "Unexpected NAMESPACE ID"
  static_assert(NAMESPACE::HIL == 4, ERR_MSG);
  static_assert(NAMESPACE::NVME__SUBSYSTEM == 8, ERR_MSG);
  static_assert(NAMESPACE::NVME__NAMESPACE == 9, ERR_MSG);
  static_assert(NAMESPACE::ISC__RUNTIME == 13, ERR_MSG);
  static_assert(NAMESPACE::ISC__FSA == 14, ERR_MSG);
  static_assert(NAMESPACE::ISC__FSA__EXT4 == 15, ERR_MSG);
  static_assert(NAMESPACE::ISC__SLET == 16, ERR_MSG);
  static_assert(NAMESPACE::ISC__SLET__GREP == 17, ERR_MSG);
  static_assert(NAMESPACE::ISC__SLET__LISTDIR == 18, ERR_MSG);
  static_assert(NAMESPACE::ISC__SLET__STATDIR == 19, ERR_MSG);

  assert(cpi.size() == NAMESPACE::TOTAL_NAMESPACES || !"Some CPIs are missing");
}

CPU::~CPU() {}

void CPU::calculatePower(Power &power) {
  // Print stats before die
  ParseXML param;
  uint64_t simCycle = (getTick() - lastResetStat) / clockPeriod;
  uint32_t totalCore =
      hilCore.size() + iclCore.size() + ftlCore.size() + iscCore.size();
  uint32_t coreIdx = 0;

  param.initialize();

  // system
  {
    param.sys.number_of_L1Directories = 0;
    param.sys.number_of_L2Directories = 0;
    param.sys.number_of_L2s = 1;
    param.sys.Private_L2 = 0;
    param.sys.number_of_L3s = 0;
    param.sys.number_of_NoCs = 0;
    param.sys.homogeneous_cores = 0;
    param.sys.homogeneous_L2s = 1;
    param.sys.homogeneous_L1Directories = 1;
    param.sys.homogeneous_L2Directories = 1;
    param.sys.homogeneous_L3s = 1;
    param.sys.homogeneous_ccs = 1;
    param.sys.homogeneous_NoCs = 1;
    param.sys.core_tech_node = 40;
    param.sys.target_core_clockrate = clockSpeed / 1000000;
    param.sys.temperature = 340;
    param.sys.number_cache_levels = 2;
    param.sys.interconnect_projection_type = 1;
    param.sys.device_type = 0;
    param.sys.longer_channel_device = 1;
    param.sys.Embedded = 1;
    param.sys.opt_clockrate = 1;
    param.sys.machine_bits = 64;
    param.sys.virtual_address_width = 48;
    param.sys.physical_address_width = 48;
    param.sys.virtual_memory_page_size = 4096;
    param.sys.total_cycles = simCycle;
    param.sys.number_of_cores = totalCore;
  }

  // Now, we are using heterogeneous cores
  for (coreIdx = 0; coreIdx < totalCore; coreIdx++) {
    // system.core
    {
      param.sys.core[coreIdx].clock_rate = param.sys.target_core_clockrate;
      param.sys.core[coreIdx].opt_local = 0;
      param.sys.core[coreIdx].instruction_length = 32;
      param.sys.core[coreIdx].opcode_width = 7;
      param.sys.core[coreIdx].x86 = 0;
      param.sys.core[coreIdx].micro_opcode_width = 8;
      param.sys.core[coreIdx].machine_type = 0;
      param.sys.core[coreIdx].number_hardware_threads = 1;
      param.sys.core[coreIdx].fetch_width = 2;
      param.sys.core[coreIdx].number_instruction_fetch_ports = 1;
      param.sys.core[coreIdx].decode_width = 2;
      param.sys.core[coreIdx].issue_width = 4;
      param.sys.core[coreIdx].peak_issue_width = 7;
      param.sys.core[coreIdx].commit_width = 4;
      param.sys.core[coreIdx].fp_issue_width = 1;
      param.sys.core[coreIdx].prediction_width = 0;
      param.sys.core[coreIdx].pipelines_per_core[0] = 1;
      param.sys.core[coreIdx].pipelines_per_core[1] = 1;
      param.sys.core[coreIdx].pipeline_depth[0] = 8;
      param.sys.core[coreIdx].pipeline_depth[1] = 8;
      param.sys.core[coreIdx].ALU_per_core = 3;
      param.sys.core[coreIdx].MUL_per_core = 1;
      param.sys.core[coreIdx].FPU_per_core = 1;
      param.sys.core[coreIdx].instruction_buffer_size = 32;
      param.sys.core[coreIdx].decoded_stream_buffer_size = 16;
      param.sys.core[coreIdx].instruction_window_scheme = 0;
      param.sys.core[coreIdx].instruction_window_size = 20;
      param.sys.core[coreIdx].fp_instruction_window_size = 15;
      param.sys.core[coreIdx].ROB_size = 0;
      param.sys.core[coreIdx].archi_Regs_IRF_size = 32;
      param.sys.core[coreIdx].archi_Regs_FRF_size = 32;
      param.sys.core[coreIdx].phy_Regs_IRF_size = 64;
      param.sys.core[coreIdx].phy_Regs_FRF_size = 64;
      param.sys.core[coreIdx].rename_scheme = 0;
      param.sys.core[coreIdx].checkpoint_depth = 1;
      param.sys.core[coreIdx].register_windows_size = 0;
      strcpy(param.sys.core[coreIdx].LSU_order, "inorder");
      param.sys.core[coreIdx].store_buffer_size = 4;
      param.sys.core[coreIdx].load_buffer_size = 0;
      param.sys.core[coreIdx].memory_ports = 1;
      param.sys.core[coreIdx].RAS_size = 0;
      param.sys.core[coreIdx].number_of_BPT = 2;
      param.sys.core[coreIdx].number_of_BTB = 2;
    }

    // system.core.itlb
    param.sys.core[coreIdx].itlb.number_entries = 64;

    // system.core.icache
    {
      param.sys.core[coreIdx].icache.icache_config[0] = 32768;
      param.sys.core[coreIdx].icache.icache_config[1] = 8;
      param.sys.core[coreIdx].icache.icache_config[2] = 4;
      param.sys.core[coreIdx].icache.icache_config[3] = 1;
      param.sys.core[coreIdx].icache.icache_config[4] = 10;
      param.sys.core[coreIdx].icache.icache_config[5] = 10;
      param.sys.core[coreIdx].icache.icache_config[6] = 32;
      param.sys.core[coreIdx].icache.icache_config[7] = 0;
      param.sys.core[coreIdx].icache.buffer_sizes[0] = 4;
      param.sys.core[coreIdx].icache.buffer_sizes[1] = 4;
      param.sys.core[coreIdx].icache.buffer_sizes[2] = 4;
      param.sys.core[coreIdx].icache.buffer_sizes[3] = 0;
    }

    // system.core dtlb
    param.sys.core[coreIdx].dtlb.number_entries = 64;

    // system.core.dcache
    {
      param.sys.core[coreIdx].dcache.dcache_config[0] = 32768;
      param.sys.core[coreIdx].dcache.dcache_config[1] = 8;
      param.sys.core[coreIdx].dcache.dcache_config[2] = 4;
      param.sys.core[coreIdx].dcache.dcache_config[3] = 1;
      param.sys.core[coreIdx].dcache.dcache_config[4] = 10;
      param.sys.core[coreIdx].dcache.dcache_config[5] = 10;
      param.sys.core[coreIdx].dcache.dcache_config[6] = 32;
      param.sys.core[coreIdx].dcache.dcache_config[7] = 0;
      param.sys.core[coreIdx].dcache.buffer_sizes[0] = 4;
      param.sys.core[coreIdx].dcache.buffer_sizes[1] = 4;
      param.sys.core[coreIdx].dcache.buffer_sizes[2] = 4;
      param.sys.core[coreIdx].dcache.buffer_sizes[3] = 4;
    }

    // system.core.BTB
    param.sys.core[coreIdx].BTB.BTB_config[0] = 4096;
    param.sys.core[coreIdx].BTB.BTB_config[1] = 4;
    param.sys.core[coreIdx].BTB.BTB_config[2] = 2;
    param.sys.core[coreIdx].BTB.BTB_config[3] = 2;
    param.sys.core[coreIdx].BTB.BTB_config[4] = 1;
    param.sys.core[coreIdx].BTB.BTB_config[5] = 1;
  }

  // system.L2
  {
    param.sys.L2[0].L2_config[0] = 1048576;
    param.sys.L2[0].L2_config[1] = 32;
    param.sys.L2[0].L2_config[2] = 8;
    param.sys.L2[0].L2_config[3] = 8;
    param.sys.L2[0].L2_config[4] = 8;
    param.sys.L2[0].L2_config[5] = 23;
    param.sys.L2[0].L2_config[6] = 32;
    param.sys.L2[0].L2_config[7] = 1;
    param.sys.L2[0].buffer_sizes[0] = 16;
    param.sys.L2[0].buffer_sizes[1] = 16;
    param.sys.L2[0].buffer_sizes[2] = 16;
    param.sys.L2[0].buffer_sizes[3] = 16;
    param.sys.L2[0].clockrate = param.sys.target_core_clockrate;
    param.sys.L2[0].ports[0] = 1;
    param.sys.L2[0].ports[1] = 1;
    param.sys.L2[0].ports[2] = 1;
    param.sys.L2[0].device_type = 0;
  }

  // Etcetera
  param.sys.mc.req_window_size_per_channel = 32;

  // Apply stat values

  // Core stat
  coreIdx = 0;
  for (auto &cores : {&hilCore, &iclCore, &ftlCore, &iscCore}) {
    for (auto &core : *cores) {
      CoreStat &stat = core.getStat();

      param.sys.core[coreIdx].total_instructions = stat.instStat.sum();
      param.sys.core[coreIdx].int_instructions = stat.instStat.arithmetic;
      param.sys.core[coreIdx].fp_instructions = stat.instStat.floatingPoint;
      param.sys.core[coreIdx].branch_instructions = stat.instStat.branch;
      param.sys.core[coreIdx].load_instructions = stat.instStat.load;
      param.sys.core[coreIdx].store_instructions = stat.instStat.store;
      param.sys.core[coreIdx].busy_cycles = stat.busy / clockPeriod;

      coreIdx++;
    }
  }

  for (coreIdx = 0; coreIdx < totalCore; coreIdx++) {
    param.sys.core[coreIdx].total_cycles = simCycle;
    param.sys.core[coreIdx].idle_cycles =
        simCycle - param.sys.core[coreIdx].busy_cycles;
    param.sys.core[coreIdx].committed_instructions =
        param.sys.core[coreIdx].total_instructions;
    param.sys.core[coreIdx].committed_int_instructions =
        param.sys.core[coreIdx].int_instructions;
    param.sys.core[coreIdx].committed_fp_instructions =
        param.sys.core[coreIdx].fp_instructions;
    param.sys.core[coreIdx].pipeline_duty_cycle = 1;
    param.sys.core[coreIdx].IFU_duty_cycle = 0.9;
    param.sys.core[coreIdx].BR_duty_cycle = 0.72;
    param.sys.core[coreIdx].LSU_duty_cycle = 0.71;
    param.sys.core[coreIdx].MemManU_I_duty_cycle = 0.9;
    param.sys.core[coreIdx].MemManU_D_duty_cycle = 0.71;
    param.sys.core[coreIdx].ALU_duty_cycle = 0.76;
    param.sys.core[coreIdx].MUL_duty_cycle = 0.82;
    param.sys.core[coreIdx].FPU_duty_cycle = 0.0;
    param.sys.core[coreIdx].ALU_cdb_duty_cycle = 0.76;
    param.sys.core[coreIdx].MUL_cdb_duty_cycle = 0.82;
    param.sys.core[coreIdx].FPU_cdb_duty_cycle = 0.0;
    param.sys.core[coreIdx].ialu_accesses = param.sys.core[0].int_instructions;
    param.sys.core[coreIdx].fpu_accesses = param.sys.core[0].fp_instructions;
    param.sys.core[coreIdx].mul_accesses =
        param.sys.core[0].int_instructions * .5;
    param.sys.core[coreIdx].int_regfile_reads =
        param.sys.core[0].load_instructions;
    param.sys.core[coreIdx].float_regfile_reads =
        param.sys.core[coreIdx].fp_instructions * .4;
    param.sys.core[coreIdx].int_regfile_writes =
        param.sys.core[0].store_instructions;
    param.sys.core[coreIdx].float_regfile_writes =
        param.sys.core[coreIdx].fp_instructions * .4;

    // L1i and L1d
    {
      auto &core = param.sys.core[coreIdx];

      core.icache.total_accesses = core.load_instructions * .3;
      core.icache.total_hits = core.icache.total_accesses * .7;
      core.icache.total_misses = core.icache.total_accesses * .3;
      core.icache.read_accesses = core.icache.total_accesses;
      core.icache.read_hits = core.icache.total_hits;
      core.icache.read_misses = core.icache.total_misses;
      core.itlb.total_accesses = core.load_instructions * .2;
      core.itlb.total_hits = core.itlb.total_accesses * .8;
      core.itlb.total_misses = core.itlb.total_accesses * .2;

      core.dcache.total_accesses = core.load_instructions * .4;
      core.dcache.total_hits = core.dcache.total_accesses * .4;
      core.dcache.total_misses = core.dcache.total_accesses * .6;
      core.dcache.read_accesses = core.dcache.total_accesses * .6;
      core.dcache.read_hits = core.dcache.total_hits * .6;
      core.dcache.read_misses = core.dcache.total_misses * .6;
      core.dcache.write_accesses = core.dcache.total_accesses * .4;
      core.dcache.write_hits = core.dcache.total_hits * .4;
      core.dcache.write_misses = core.dcache.total_misses * .4;
      core.dcache.write_backs = core.dcache.total_misses * .4;
      core.dtlb.total_accesses = core.load_instructions * .2;
      core.dtlb.total_hits = core.itlb.total_accesses * .8;
      core.dtlb.total_misses = core.itlb.total_accesses * .2;
    }
  }

  // L2
  param.sys.L2[0].duty_cycle = 1.;

  if (param.sys.number_of_L2s > 0) {
    auto &L2 = param.sys.L2[0];

    L2.total_accesses = param.sys.core[0].dcache.write_backs;
    L2.total_hits = L2.total_accesses * .4;
    L2.total_misses = L2.total_accesses * .6;
    L2.read_accesses = L2.total_accesses * .5;
    L2.read_hits = L2.total_hits * .5;
    L2.read_misses = L2.total_misses * .5;
    L2.write_accesses = L2.total_accesses * .5;
    L2.write_hits = L2.total_hits * .5;
    L2.write_misses = L2.total_misses * .5;
    L2.write_backs = L2.total_misses * .4;
  }
  else {
    param.sys.L2[0].total_accesses = 0;
    param.sys.L2[0].total_hits = 0;
    param.sys.L2[0].total_misses = 0;
    param.sys.L2[0].read_accesses = 0;
    param.sys.L2[0].read_hits = 0;
    param.sys.L2[0].read_misses = 0;
    param.sys.L2[0].write_accesses = 0;
    param.sys.L2[0].write_hits = 0;
    param.sys.L2[0].write_misses = 0;
    param.sys.L2[0].write_backs = 0;
  }

  // L3
  param.sys.L3[0].duty_cycle = 1.;

  if (param.sys.number_of_L3s > 0) {
    auto &L3 = param.sys.L3[0];

    L3.total_accesses = param.sys.L2[0].write_backs;
    L3.total_hits = L3.total_accesses * .4;
    L3.total_misses = L3.total_accesses * .6;
    L3.read_accesses = L3.total_accesses * .5;
    L3.read_hits = L3.total_hits * .5;
    L3.read_misses = L3.total_misses * .5;
    L3.write_accesses = L3.total_accesses * .5;
    L3.write_hits = L3.total_hits * .5;
    L3.write_misses = L3.total_misses * .5;
    L3.write_backs = L3.total_misses * .4;
  }
  else {
    param.sys.L3[0].total_accesses = 0;
    param.sys.L3[0].total_hits = 0;
    param.sys.L3[0].total_misses = 0;
    param.sys.L3[0].read_accesses = 0;
    param.sys.L3[0].read_hits = 0;
    param.sys.L3[0].read_misses = 0;
    param.sys.L3[0].write_accesses = 0;
    param.sys.L3[0].write_hits = 0;
    param.sys.L3[0].write_misses = 0;
    param.sys.L3[0].write_backs = 0;
  }

  McPAT mcpat(&param);

  mcpat.getPower(power);
}

uint32_t CPU::leastBusyCPU(std::vector<Core> &list) {
  uint32_t idx = list.size();
  uint64_t busymin = std::numeric_limits<uint64_t>::max();

  for (uint32_t i = 0; i < list.size(); i++) {
    auto &core = list.at(i);
    auto busy = core.getStat().busy;

    if (!core.isBusy() && busy < busymin) {
      busymin = busy;
      idx = i;
    }
  }

  if (idx == list.size()) {
    uint64_t jobmin = std::numeric_limits<uint64_t>::max();

    for (uint32_t i = 0; i < list.size(); i++) {
      auto jobs = list.at(i).getJobListSize();

      if (jobs <= jobmin) {
        jobmin = jobs;
        idx = i;
      }
    }
  }

  return idx;
}

void CPU::execute(NAMESPACE ns, FUNCTION fct, DMAFunction &func, void *context,
                  uint64_t delay) {
  Core *pCore = nullptr;

  // Find dedicated core
  switch (ns) {
    case FTL:
    case FTL__PAGE_MAPPING:
      if (ftlCore.size() > 0) {
        pCore = &ftlCore.at(leastBusyCPU(ftlCore));
      }

      break;
    case ISC__RUNTIME ... TOTAL_NAMESPACES:
      if (iscCore.size() > 0) {
        pCore = &iscCore.at(leastBusyCPU(iscCore));
      }

      break;
    case ICL:
    case ICL__GENERIC_CACHE:
      if (iclCore.size() > 0) {
        pCore = &iclCore.at(leastBusyCPU(iclCore));
      }

      break;
    case HIL:
    case NVME__CONTROLLER:
    case NVME__PRPLIST:
    case NVME__SGL:
    case NVME__SUBSYSTEM:
    case NVME__NAMESPACE:
    case NVME__OCSSD:
    case UFS__DEVICE:
    case SATA__DEVICE:
      if (hilCore.size() > 0) {
        pCore = &hilCore.at(leastBusyCPU(hilCore));
      }

      break;
    default:
      panic("Undefined function namespace %u", ns);

      break;
  }

  if (pCore) {
    // Get CPI table
    auto table = cpi.find(ns);

    if (table == cpi.end()) {
      panic("Namespace %u not defined in CPI table", ns);
    }

    // Get CPI
    auto inst = table->second.find(fct);

    if (inst == table->second.end()) {
      panic("Namespace %u does not have function %u", ns, fct);
    }

    // debugprint(LOG_CPU, "EXEC: %s::%s (+%lu)", NS2STR[ns], FCT2STR[fct],
    // delay);
    pCore->submitJob(JobEntry(func, context, &inst->second), delay);
  }
  else {
    panic("CPI not found");
    func(getTick(), context);
  }
}

uint64_t CPU::applyLatency(NAMESPACE ns, FUNCTION fct) {
  Core *pCore = nullptr;

  // Find dedicated core
  switch (ns) {
    case FTL:
    case FTL__PAGE_MAPPING:
      if (ftlCore.size() > 0) {
        pCore = &ftlCore.at(leastBusyCPU(ftlCore));
      }

      break;
    case ISC__RUNTIME ... TOTAL_NAMESPACES:
      if (iscCore.size() > 0) {
        pCore = &iscCore.at(leastBusyCPU(iscCore));
      }

      break;
    case ICL:
    case ICL__GENERIC_CACHE:
      if (iclCore.size() > 0) {
        pCore = &iclCore.at(leastBusyCPU(iclCore));
      }

      break;
    case HIL:
    case NVME__CONTROLLER:
    case NVME__PRPLIST:
    case NVME__SGL:
    case NVME__SUBSYSTEM:
    case NVME__NAMESPACE:
    case NVME__OCSSD:
    case UFS__DEVICE:
    case SATA__DEVICE:
      if (hilCore.size() > 0) {
        pCore = &hilCore.at(leastBusyCPU(hilCore));
      }

      break;
    default:
      panic("Undefined function namespace %u", ns);

      break;
  }

  if (pCore) {
    // Get CPI table
    auto table = cpi.find(ns);

    if (table == cpi.end()) {
      panic("Namespace %u not defined in CPI table", ns);
    }

    // Get CPI
    auto inst = table->second.find(fct);

    if (inst == table->second.end()) {
      panic("Namespace %u does not have function %u", ns, fct);
    }

    pCore->addStat(inst->second);

    return inst->second.latency;
  }

  return 0;
}

void CPU::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;
  std::string number;

  std::tuple<std::vector<Core> *, std::string, std::string> coreList[] = {
      {&hilCore, ".hil", "HIL"},
      {&iclCore, ".icl", "ICL"},
      {&ftlCore, ".ftl", "FTL"},
      {&iscCore, ".isc", "ISC"},
  };

  for (auto &core : coreList) {
    for (uint32_t i = 0; i < std::get<0>(core)->size(); i++) {
      number = std::to_string(i);

      const auto namePfx = prefix + std::get<1>(core) + number;
      const auto descPfx = "CPU for " + std::get<2>(core) + " core " + number;
      ;

      temp.name = namePfx + ".busy";
      temp.desc = descPfx + " busy ticks";
      list.push_back(temp);

      temp.name = namePfx + ".insts.branch";
      temp.desc = descPfx + " executed branch instructions";
      list.push_back(temp);

      temp.name = namePfx + ".insts.load";
      temp.desc = descPfx + " executed load instructions";
      list.push_back(temp);

      temp.name = namePfx + ".insts.store";
      temp.desc = descPfx + " executed store instructions";
      list.push_back(temp);

      temp.name = namePfx + ".insts.arithmetic";
      temp.desc = descPfx + " executed arithmetic instructions";
      list.push_back(temp);

      temp.name = namePfx + ".insts.fp";
      temp.desc = descPfx + " executed floating point instructions";
      list.push_back(temp);

      temp.name = namePfx + ".insts.others";
      temp.desc = descPfx + " executed other instructions";
      list.push_back(temp);
    }
  }
}

void CPU::getStatValues(std::vector<double> &values) {
  for (auto &cores : {&hilCore, &iclCore, &ftlCore, &iscCore}) {
    for (auto &core : *cores) {
      auto &stat = core.getStat();

      values.push_back(stat.busy);
      values.push_back(stat.instStat.branch);
      values.push_back(stat.instStat.load);
      values.push_back(stat.instStat.store);
      values.push_back(stat.instStat.arithmetic);
      values.push_back(stat.instStat.floatingPoint);
      values.push_back(stat.instStat.otherInsts);
    }
  }
}

void CPU::resetStatValues() {
  lastResetStat = getTick();

  for (auto &cores : {&hilCore, &iclCore, &ftlCore, &iscCore}) {
    for (auto &core : *cores) {
      auto &stat = core.getStat();

      stat.busy = 0;
      stat.instStat = InstStat();
    }
  }
}

void CPU::printLastStat() {
  Power power;

  debugprint(LOG_CPU, "Begin CPU power calculation");

  calculatePower(power);

  debugprint(LOG_CPU, "Core:");
  debugprint(LOG_CPU, "  Area: %lf mm^2", power.core.area);
  debugprint(LOG_CPU, "  Peak Dynamic: %lf W", power.core.peakDynamic);
  debugprint(LOG_CPU, "  Subthreshold Leakage: %lf W",
             power.core.subthresholdLeakage);
  debugprint(LOG_CPU, "  Gate Leakage: %lf W", power.core.gateLeakage);
  debugprint(LOG_CPU, "  Runtime Dynamic: %lf W", power.core.runtimeDynamic);

  if (power.level2.area > 0.0) {
    debugprint(LOG_CPU, "L2:");
    debugprint(LOG_CPU, "  Area: %lf mm^2", power.level2.area);
    debugprint(LOG_CPU, "  Peak Dynamic: %lf W", power.level2.peakDynamic);
    debugprint(LOG_CPU, "  Subthreshold Leakage: %lf W",
               power.level2.subthresholdLeakage);
    debugprint(LOG_CPU, "  Gate Leakage: %lf W", power.level2.gateLeakage);
    debugprint(LOG_CPU, "  Runtime Dynamic: %lf W",
               power.level2.runtimeDynamic);
  }

  if (power.level3.area > 0.0) {
    debugprint(LOG_CPU, "L3:");
    debugprint(LOG_CPU, "  Area: %lf mm^2", power.level3.area);
    debugprint(LOG_CPU, "  Peak Dynamic: %lf W", power.level3.peakDynamic);
    debugprint(LOG_CPU, "  Subthreshold Leakage: %lf W",
               power.level3.subthresholdLeakage);
    debugprint(LOG_CPU, "  Gate Leakage: %lf W", power.level3.gateLeakage);
    debugprint(LOG_CPU, "  Runtime Dynamic: %lf W",
               power.level3.runtimeDynamic);
  }
}

}  // namespace CPU

}  // namespace SimpleSSD
