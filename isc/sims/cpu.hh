#ifndef __SIMPLESSD_ISC_SIM_CPU_HH__
#define __SIMPLESSD_ISC_SIM_CPU_HH__

#include <cstdint>

#ifdef ISC_TEST
#include <functional>

#include "utils/debug.hh"
#else
#include "cpu/def.hh"
#include "sim/cpu.hh"
#endif

namespace SimpleSSD {
namespace ISC {
namespace SIM {

#ifdef ISC_TEST
typedef std::function<void(uint64_t begTick, void *data)> DMA;

namespace CPU {
// NOTE: these values should be added to simplessd/cpu/def.hh
typedef enum : uint16_t {
  FTL,
  FTL__PAGE_MAPPING,
  ICL,
  ICL__GENERIC_CACHE,
  HIL,
  NVME__CONTROLLER,
  NVME__PRPLIST,
  NVME__SGL,
  NVME__SUBSYSTEM,
  NVME__NAMESPACE,
  NVME__OCSSD,
  UFS__DEVICE,
  SATA__DEVICE,

  // ISC NAMESPACES
  ISC__RUNTIME,
  ISC__FSA,
  ISC__FSA__EXT4,
  ISC__SLET,
  ISC__SLET__GREP,
  ISC__SLET__LISTDIR,
  ISC__SLET__STATDIR,
  ISC__SLET__MD5,
  ISC__SLET__STATS32,
  ISC__SLET__STATS64,

  //Scheduler
  CREDIT_SCHEDULER,
  FCFS_SCHEDULER,

  // add before this
  NS_COUNT,
} NS;

// NOTE: these values should be added to simplessd/cpu/def.hh
typedef enum : uint16_t {
  ISC__GET,
  ISC__SET,

  // FS funcs
  ISC__INIT,
  ISC__GET_SUPER,
  ISC__GET_GROUP,
  ISC__GET_IMAP,
  ISC__GET_INODE,
  ISC__GET_INODE_PARENT,
  ISC__GET_EXTENT_SIZE,
  ISC__GET_EXTENT_INTERNAL,
  ISC__GET_EXTENT,
  ISC__DIR_SEARCH_FILE,
  ISC__NAMEI,

  // runtime funcs (each FSA/APP should have ADD_SLET)
  ISC__START_SLET,
  ISC__SET_OPT,
  ISC__GET_OPT,
  ISC__TASK1,
  ISC__TASK2,
  ISC__TASK3,
  ISC__TASK4,
  ISC__TASK5,
  ISC__ADD_SLET__EXT4,
  ISC__ADD_SLET__GREP,
  ISC__ADD_SLET__STATDIR,
  ISC__ADD_SLET__MD5,
  ISC__ADD_SLET__STATS32,
  ISC__ADD_SLET__STATS64,
  
  //Scheduler
  SCHEDULE,

  // add before this
  FCT_COUNT,
} FCT;
}  // namespace CPU

/**
 * @brief Execute the given function on the device CPU and update statistic info
 *
 * For testing the ISC module without integrating into SimpleSSD, we only need
 * to run the function with the given args; no need to handle statistic info
 * updating and CPU selection.
 *
 * @param ns The namespace id of the function to be executed
 * @param fct The function id under previously given namespace
 * @param func The function pointer of target function
 * @param ctx The args for the target function
 * @param delay The delay time in ns for executing the function
 */
static inline void execute(CPU::NS, CPU::FCT, DMA &, void *, uint64_t);
static inline void applyLatency(CPU::NS, CPU::FCT, size_t = 0) {}

#define simTick
#define simCtx
#define _sim_params
#define _add_sim_params
#define _SIM_PARAMS
#define _ADD_SIM_PARAMS
#define simApplyLatency(ns, fct) applyLatency((ns), (fct))
#define simApplyManyLatency(ns, fct, n) applyLatency((ns), (fct), (n))

#else
typedef SimpleSSD::DMAFunction DMA;

// SimpleSSD-styled wrappers
#define simTick tick
#define simCtx ctx
#define _sim_params simTick, simCtx
#define _add_sim_params , _sim_params
#define _SIM_PARAMS                                                            \
  uint64_t &simTick [[maybe_unused]], void *simCtx [[maybe_unused]]
#define _ADD_SIM_PARAMS , _SIM_PARAMS
#define _UNUSED_SIM_PARAMS unused_values(_sim_params)

#define simApplyLatency(ns, fct)                                               \
  do {                                                                         \
    auto old = simTick;                                                        \
    simTick += applyLatency((ns), (fct));                                      \
    pr("applyLatency '%s::%s' | %lu - %lu (%lu)", str(ns), str(fct), old,      \
       simTick, simTick - old);                                                \
  } while (0)

#define simApplyManyLatency(ns, fct, times)                                    \
  do {                                                                         \
    auto old = simTick;                                                        \
    for (size_t iApplyTimes = 0; iApplyTimes < (times); ++iApplyTimes)         \
      simTick += applyLatency((ns), (fct));                                    \
    pr("applyLatency x%lu of '%s::%s' | %lu - %lu (%lu)", (size_t)(times),     \
       str(ns), str(fct), old, simTick, simTick - old);                        \
  } while (0)

#endif

}  // namespace SIM
}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SIM_CPU_HH__ */