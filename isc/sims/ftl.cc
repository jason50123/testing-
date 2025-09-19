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

  // ★ Credit gate (blocking): ensure credit before proceeding
  if (gScheduler) {
    uint64_t pages = cReq.range.nlp ? cReq.range.nlp : 1;  // at least 1 page
    auto *cs = dynamic_cast<SimpleSSD::HIL::CreditScheduler*>(gScheduler);
    if (cs) {
      if (hReq.userID == 0) {
        // ★ Admin user (uid=0) bypass - 系統初始化不受credit限制
        pr("FTL: Admin user (uid=0) bypass credit check - pages=%lu", pages);
      } else {
        // ★ 正常用戶走credit檢查流程
        // 確保用戶與timer啟動
        cs->ensureActiveUser(hReq.userID);

        // 等待直到有足夠credit：推進模擬時間並觸發refill
        uint64_t period = cs->getPeriodTicks();
        if (period == 0) period = 10000000ULL; // 安全預設：10M ticks

        size_t guard = 100000; // 防止無限迴圈
        while (!cs->checkCredit(hReq.userID, pages) && guard--) {
          // 推進時間到下一個可能的refill相位，並手動觸發一次補發處理
          simTick += period;
          cs->processEvent(simTick);
        }

        // 最後一次檢查（保險），若仍不足則不阻塞避免卡死
        if (cs->checkCredit(hReq.userID, pages)) {
          cs->useCreditISC(hReq.userID, pages);
        } else {
          pr("FTL: WARNING credit gate not satisfied after waits, continue without charge (uid=%u pages=%lu)",
             hReq.userID, pages);
        }
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
