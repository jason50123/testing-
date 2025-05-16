#ifndef __SIMPLESSD_ISC_CONFIGS_HH__
#define __SIMPLESSD_ISC_CONFIGS_HH__

#include <cstdint>

namespace SimpleSSD {
namespace ISC {

#define NUM_ISC_CORES 1

#define ISC_SUBCMD_MASK 0xFFFF00000000
#define ISC_SUBCMD(slba) ((ISC_SUBCMD_MASK & (slba)) >> 32)
#define ISC_SUBCMD_IS(slba, cmd) (ISC_SUBCMD((slba)) == (cmd))

#define ISC_SUBCMD_INIT 0x0000
#define ISC_SUBCMD_FREE 0x0FFF
#define ISC_SUBCMD_SLET_OPT 0x0001
#define ISC_SUBCMD_SLET_RES 0x0002
#define ISC_SUBCMD_SLET_RESSZ 0x0003
#define ISC_SUBCMD_SLET_RUN 0x1000
#define ISC_SUBCMD_SLET_FREE 0x000F

#define ISC_SUBCMD_SCHEDULER 0x0010
#define ISC_SUBCMD_SCHEDULER_FCFS 0x0001
#define ISC_SUBCMD_SCHEDULER_CREDIT 0x0002
#define ISC_SUBCMD_SCHEDULER_FLIN 0x0003

#define ISC_SUBCMD_OPT_MASK 0x0000FFFFFFFF
#define ISC_SUBCMD_OPT(slba) (ISC_SUBCMD_OPT_MASK & (slba))

#define ISC_KEY_LEN (32)
#define ISC_VAL_LEN(dlen) ((dlen)-ISC_KEY_LEN)

#define ISC_KEY_NAME "name"
#define ISC_KEY_RESULT "result"
#define ISC_KEY_RESULT_SIZE "result-size"

#define ISC_OPCODE_SET 0xC1
#define ISC_OPCODE_GET 0xC2

/* -------------------------------------------------------------------------- */
/*                           utils for these subcmds                          */
/* -------------------------------------------------------------------------- */

typedef struct {
  uint32_t option;
  uint16_t subcmd;
  uint16_t unused;
} ISC_SUBCMD_t;

static inline void setupSubcmd(uint64_t *slba, uint16_t cmd, uint32_t opt) {
  ((ISC_SUBCMD_t *)slba)->subcmd = cmd;
  ((ISC_SUBCMD_t *)slba)->option = opt;
}

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_CONFIGS_HH__ */