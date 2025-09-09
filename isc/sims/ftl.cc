#include <fcntl.h>
#include <string.h>
#include <unistd.h>


#ifndef ISC_TEST
#include "hil/hil.hh"
#include "hil/nvme/subsystem.hh"
#include "icl/icl.hh"
#include "hil/scheduler/scheduler.hh"
#include "hil/scheduler/credit_scheduler.hh"
#endif

#include "sims/ftl.hh"

#include "utils/debug.hh"
#include "utils/math.hh"
#include "hil/hil.hh"  // for gScheduler

#define PR_SECTION LOG_ISC_UTIL_FTL

#ifndef PAGE_SIZE

#define PAGE_SIZE 4096ULL

#endif




namespace SimpleSSD {
namespace ISC {
namespace SIM {
  
using SimpleSSD::HIL::Request;   // for convenience


/* -------------------------------------------------------------------------- */
/*                             init static members                            */
/* -------------------------------------------------------------------------- */

size_t FTL::lbaSize;
char *FTL::pathFilesystemImg;
void *FTL::cache;

/* -------------------------------------------------------------------------- */
/*                             impl util functions                            */
/* -------------------------------------------------------------------------- */

void FTL::setImage(const char *p, size_t bsz) {
  if (pathFilesystemImg)
    free(pathFilesystemImg);
  pathFilesystemImg = strdup(p);

  FTL::lbaSize = bsz;
  pr("Setup disk image: '%s'", p);
}

#ifndef ISC_TEST
// for functions not apply the SimpleSSD-styled latency handling
void FTL::read(void *buf, size_t ofs, size_t sz) {
  pr("Read (ofs,sz=%lu,%lu) to %p", ofs, sz, buf);
  // open and read the pathFilesystemImage to buffer
  int fd;
  ssize_t szRead;

  if (!buf) {
    pr("buffer is null!!");
    goto out;
  }
  if ((fd = open(FTL::pathFilesystemImg, O_RDONLY)) == -1) {
    perr("open() fail");
    goto out;
  }

  if ((szRead = pread(fd, buf, sz, ofs)) == -1)
    perr("pread() fail");
  else if ((size_t)szRead != sz)
    pr("Expect %lu bytes read, but got %ld", sz, szRead);

  close(fd);
out:
  return;
}
#endif

void FTL::read(void *buf, size_t ofs, size_t sz _ADD_SIM_PARAMS) {
  auto doit = [](void *buf, size_t ofs, size_t sz _ADD_SIM_PARAMS) {
    pr("Read (ofs,sz=%lu,%lu) to %p", ofs, sz, buf);
    // open and read the pathFilesystemImage to buffer
    int fd;
    ssize_t szRead;

    if (!buf) {
      pr("buffer is null!!");
      goto out;
    }
    if ((fd = open(FTL::pathFilesystemImg, O_RDONLY)) == -1) {
      perr("open() fail");
      goto out;
    }

    if ((szRead = pread(fd, buf, sz, ofs)) == -1)
      perr("pread() fail");
    else if ((size_t)szRead != sz)
      pr("Expect %lu bytes read, but got %ld", sz, szRead);

    close(fd);
  out:
    return;
  };

  /* ---------- SimpleSSD Fast Path  ---------- */

#ifndef ISC_TEST
  auto hReq = *(HIL::Request *)simCtx;
  auto ns = (HIL::NVMe::Namespace *)hReq.ns;
    
  size_t slba = ofs / FTL::lbaSize;
  size_t nlblk = (sz + FTL::lbaSize - 1) / FTL::lbaSize;
  ns->pParent->convertUnit(ns, slba, nlblk, hReq);

  ICL::Request cReq(hReq);
  pr("Changed cReq: {slpn,nlp}={%lu,%lu} | ofs,len=%lu,%lu", cReq.range.slpn,
     cReq.range.nlp, cReq.offset, cReq.length);

  // ★ STRICT Credit Control - 同步等待直到credit可用
  if (gScheduler) {
    uint64_t pages = cReq.range.nlp;  // Use logical pages from ICL request
    auto *cs = dynamic_cast<SimpleSSD::HIL::CreditScheduler*>(gScheduler);
    if (cs) {
      // ★ 同步等待直到有足夠credit
      uint64_t waitStartTick = simTick;
      uint32_t waitCycles = 0;
      
      while (!cs->checkCredit(hReq.userID, pages)) {
        waitCycles++;
        // 每次等待一個scheduler週期 (10M ticks)
        simTick += 10000000;  // 等待下一個credit refill週期
        
        if (waitCycles % 100 == 0) {  // 每1000M ticks輸出一次等待狀態
          pr("FTL: ISC task uid=%u waiting for credit, pages=%lu, waitCycles=%u", 
             hReq.userID, pages, waitCycles);
        }
        
        // 安全檢查：避免無限等待 (最多等待10秒模擬時間)
        if (waitCycles > 1000) {
          pr("FTL: WARNING - ISC task uid=%u exceeded max wait cycles, proceeding anyway", 
             hReq.userID);
          break;
        }
      }
      
      // ★ 扣除credit並記錄等待時間
      cs->useCreditISC(hReq.userID, pages);
      if (waitCycles > 0) {
        uint64_t waitTime = simTick - waitStartTick;
        pr("FTL: ISC task uid=%u waited %u cycles (%lu ticks) for credit, pages=%lu", 
           hReq.userID, waitCycles, waitTime, pages);
      }
    }
  }

  // FTL latencies should already handled inside
  ((SimpleSSD::ICL::ICL *)cache)->read(cReq, simTick);

#endif

  doit(buf, ofs, sz _add_sim_params);
}

}  // namespace SIM
}  // namespace ISC
}  // namespace SimpleSSD
