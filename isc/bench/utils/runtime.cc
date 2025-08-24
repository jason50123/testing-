#include <libnvme.h>

#include "common.hh"
#include "runtime.hh"
#include "uid_debug.hh"

#include "../../sims/configs.hh"

#define HOST_PAGE_SIZE getpagesize()
#define NVME_LBA_SIZE 512

#define ALIGN_MASK(num, mask) (((num) + (mask)) & ~(mask))
#define ALIGN_UP(num, to) ((num + (to - 1)) & ~(to - 1))

namespace SimpleSSD {
namespace ISC {

static_assert(offsetof(nvme_config_t, dry) == 64, "Wrong packet size");

int send_passthru(nvme_config_t config) {
  pr("opcode       : 0x%02x", config.opcode);
  pr("nsid         : 0x%02x", config.nsid);
  pr("flags        : 0x%04x", config.flags);
  pr("rsvd         : 0x%08x", config.rsvd);
  pr("cdw2         : 0x%08x", config.cdw02);
  pr("cdw3         : 0x%08x", config.cdw03);
  pr("data_addr    : %p", config.data);
  pr("madata_addr  : %p", config.metadata);
  pr("data_len     : 0x%08x", config.data_len);
  pr("mdata_len    : 0x%08x", config.metadata_len);
  pr("slba         : 0x%08lx", config.slba);
  pr("nlb          : 0x%08x", config.nlb);
  pr("cdw10        : 0x%08x", config.cdw10);
  pr("cdw11        : 0x%08x", config.cdw11);
  pr("cdw12        : 0x%08x", config.cdw12);
  pr("cdw13        : 0x%08x", config.cdw13);
  pr("cdw14        : 0x%08x", config.cdw14);
  pr("cdw15        : 0x%08x", config.cdw15);

  if (config.dry) {
    return 0;
  }

#ifdef NO_M5
  int err = 0;
#else
  int err = nvme_io_passthru(
      config.devfd, config.opcode, config.flags, config.rsvd, config.nsid,
      config.cdw02, config.cdw03, config.cdw10, config.cdw11, config.cdw12,
      config.cdw13, config.cdw14, config.cdw15, config.data_len, config.data,
      config.metadata_len, config.metadata, config.timeout_ms, &config.result);
#endif

  switch (err) {
    case 0:  // success
      break;

    default:
      perr("Request failed (err,res=%d,%u): %s", err, config.result,
           err >= ENVME_CONNECT_RESOLVE ? nvme_errno_to_string(err)
                                        : strerror(err));
      break;
  }

  return err;
}

int initRuntime(nvme_config_t config) {
  config.opcode = ISC_OPCODE_SET;
  config.data_len = NVME_LBA_SIZE;  // 必須至少有一個 LBA 大小的 buffer
  config.nlb = 0;
  config.metadata_len = 0;
  config.metadata = nullptr;
  config.slba = 0;

  setupSubcmd(&config.slba, ISC_SUBCMD_INIT, 0);

  
  char *buffer = (char *)aligned_alloc(HOST_PAGE_SIZE, config.data_len);
  if (!buffer) {
    perr("Buffer allocation failed");
    return -ENOMEM;
  }
  memset(buffer, 0, config.data_len);
  config.data = buffer;

  auto res = send_passthru(config);
  pr("request succeed, copy data to user buffer");

  free(buffer);

  return res;
}


/* the NLB field is fixed to 4KiB, @dlen param is used for memcpy only */
int setOpt(uint32_t id, nvme_config_t config, const char *key, void *data,
           size_t dlen) {
  config.opcode = ISC_OPCODE_SET;
  config.slba = 0;
  config.data_len = ALIGN_UP(dlen, HOST_PAGE_SIZE);
  config.nlb = (config.data_len / NVME_LBA_SIZE) - 1;
  config.data = (char *)aligned_alloc(HOST_PAGE_SIZE, config.data_len);
  config.metadata_len = 0;
  config.metadata = nullptr;

  setupSubcmd(&config.slba, ISC_SUBCMD_SLET_OPT, id);

  if (!config.data) {
    exit(-1);
  }
  memset(config.data, 0, config.data_len);
  strncpy(&config.data[0], key, 32);
  memcpy(&config.data[ISC_KEY_LEN], data, dlen);

#ifdef ISC_DEBUG
  dumpReqDataB64(config.data, config.data_len);
#endif

  auto res = send_passthru(config);
  if (config.data)
    free(config.data);
  return res;
}

int getResult(uint32_t id, nvme_config_t config, void *data, size_t dlen) {
  config.opcode = ISC_OPCODE_GET;
  config.slba = 0;
  config.nlb = 7;
  config.data_len = ALIGN_UP(dlen, HOST_PAGE_SIZE);
  config.nlb = (config.data_len / NVME_LBA_SIZE) - 1;
  config.data = (char *)aligned_alloc(HOST_PAGE_SIZE, config.data_len);
  config.metadata_len = 0;
  config.metadata = nullptr;

  setupSubcmd(&config.slba, ISC_SUBCMD_SLET_RES, id);
  config.cdw03 = getuid();

  if (!config.data) {
    perrno("malloc failed");
    exit(-1);
  }
  memset(config.data, 0, config.data_len);

  auto res = send_passthru(config);

  if (!res) {
    pr("request succeed, copy data to user buffer");
    memcpy(data, config.data, dlen);
  }

  if (config.data)
    free(config.data);

  return res;
}

int getResultSize(uint32_t id, nvme_config_t config, uint64_t *data) {
  pr("get resultsize start");
  config.opcode = ISC_OPCODE_GET;
  config.slba = 0;
  config.nlb = 7;
  config.data_len = ALIGN_UP(sizeof(uint64_t), HOST_PAGE_SIZE);
  config.nlb = (config.data_len / NVME_LBA_SIZE) - 1;
  config.metadata_len = 0;
  config.metadata = nullptr;
  
  pr("get resultsize alloc start");
  config.data = (char *)aligned_alloc(HOST_PAGE_SIZE, config.data_len);
  pr("get resultsize alloc end");
  setupSubcmd(&config.slba, ISC_SUBCMD_SLET_RESSZ, id);
  config.cdw03 = getuid();
  if (!config.data) {
    perrno("malloc failed");
    exit(-1);
  }
  memset(config.data, 0, config.data_len);

  auto res = send_passthru(config);

  if (!res) {
    pr("request succeed, copy data to user buffer");
    memcpy(data, config.data, sizeof(uint64_t));
  }

  if (config.data)
    free(config.data);

  return res;
}

int startSlet(uint32_t id, nvme_config_t config) {
  config.opcode = ISC_OPCODE_GET;
  config.data_len = NVME_LBA_SIZE;
  config.nlb      = (config.data_len / NVME_LBA_SIZE) - 1;
  config.metadata_len = 0;
  config.metadata = nullptr;
  config.slba = 0;

  setupSubcmd(&config.slba, ISC_SUBCMD_SLET_RUN, id);
  
  config.cdw03 = getuid();
  dump_uids("startSlet");

  char *buffer = (char *)aligned_alloc(HOST_PAGE_SIZE, config.data_len);
  if (!buffer) {
    perr("Buffer allocation failed");
    return -ENOMEM;
  }
  memset(buffer, 0, config.data_len);
  config.data = buffer;

  auto res = send_passthru(config);
  if (config.data)
    free(config.data);
  pr("this is STATRT RUNTIME");
  return res;
}

int setScheduler(nvme_config_t cfg, uint32_t type)
{
    cfg.opcode       = ISC_OPCODE_SET;
    cfg.data_len     = NVME_LBA_SIZE;        // 至少要有一個 LBA
    cfg.nlb          = 0;
    cfg.metadata_len = 0;
    cfg.metadata     = nullptr;
    cfg.slba         = 0;

    setupSubcmd(&cfg.slba, ISC_SUBCMD_SCHEDULER, type);

    char *buffer = (char *)aligned_alloc(HOST_PAGE_SIZE, cfg.data_len);
    if (!buffer) {
      perr("Buffer allocation failed");
      return -ENOMEM;
    }
    memset(buffer, 0, cfg.data_len);
    cfg.data = buffer;

    int ret = send_passthru(cfg);
    pr("scheduler choose end");
    free(buffer);
    return ret;
}

}  // namespace ISC
}  // namespace SimpleSSD