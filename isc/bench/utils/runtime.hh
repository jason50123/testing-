#ifndef __SIMPLESSD_ISC_BENCH_UTILS_RUNTIME_HH__
#define __SIMPLESSD_ISC_BENCH_UTILS_RUNTIME_HH__

#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace SimpleSSD {
namespace ISC {

#define PR_ERR_FMT "(%s() at %s:%d):: "
#define PR_ERR_ARGS __func__, __FILE__, __LINE__

#define pr(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define perr(fmt, ...) pr(PR_ERR_FMT fmt, PR_ERR_ARGS, ##__VA_ARGS__)
#define perrno(fmt, ...)                                                       \
  perr(fmt ": %d(%s)", ##__VA_ARGS__, errno, strerror(errno))

struct nvme_config_t {
  // command packet members
  struct {
    union {
      struct {
        uint8_t opcode;
        uint8_t fuse : 2;       // fuse operation
        uint8_t reserved0 : 4;  //
        uint8_t psdt : 2;       // 00 for PRP, 11 reserved, otherwise SGL.
        uint16_t CID;           // command id
      };
      uint32_t cdw00;  // CDW 0: command info
    };

    union {
      uint32_t nsid;
      uint32_t cdw01;  // CDW 1: namespace id
    };

    uint32_t cdw02;  // command specific
    uint32_t cdw03;  // command specific

    union {
      uint64_t meta_addr;
      struct {
        uint32_t cdw04;
        uint32_t cdw05;
      };  // CDW 4,5: dword aligned physical address of the metadata buffer
    };

    union {
      struct {
        uint64_t prp1;  // physical address of the data buffer or PRP list
        uint64_t prp2;
      };
      struct {
        uint32_t cdw06;
        uint32_t cdw07;
        uint32_t cdw08;
        uint32_t cdw09;
      };
    };  // CDW 6:9: data buffer info

    union {
      struct {
        uint32_t cdw10;  // command specific
        uint32_t cdw11;  // command specific
      };
      uint64_t slba;  // the starting LBA
    };

    union {
      uint32_t cdw12;  // command specific
      struct {
        uint16_t nlb;  // number of logical blocks
        uint16_t unused;
      };
    };

    uint32_t cdw13;  // command specific
    uint32_t cdw14;  // command specific
    uint32_t cdw15;  // command specific
  };

  // runtime user data
  struct {
    bool dry;
    int devfd;

    uint8_t flags;
    uint16_t rsvd;
    uint32_t result;
    uint32_t timeout_ms;

    char *data;
    uint32_t data_len;
    char *metadata;
    uint32_t metadata_len;
  };
};

extern int send_passthru(nvme_config_t);
extern int initRuntime(nvme_config_t);
extern int setOpt(uint32_t id, nvme_config_t, const char *, void *, size_t);
extern int getResult(uint32_t id, nvme_config_t, void *, size_t);
extern int getResultSize(uint32_t id, nvme_config_t, size_t *);
extern int startSlet(uint32_t id, nvme_config_t);

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_BENCH_UTILS_RUNTIME_HH__ */