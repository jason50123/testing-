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

#ifndef ISC_TEST
  /* ---------- NEW Credit Scheduler Flow Control ---------- */

  uint32_t uid = 0;
   
  if (simCtx)
    uid = ((HIL::Request *)simCtx)->userID;

  if (gScheduler) {
    pr("FTL::read() NEW Credit Scheduler branch entered");
    uint64_t pages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
    pr("FTL::read | uid=%u | I/O=%lu B (%lu pages) | simTick=%lu",
       uid, sz, pages, simTick);

    // 檢查用戶是否有足夠的credit
    if (!gScheduler->checkCredit(uid, pages)) {
        pr("FTL::read | uid=%u | Insufficient credit, submitting credit-only request", uid);
        
        HIL::Request credReq{};
        credReq.userID = uid;
        credReq.prio   = 0;
        credReq.length = pages * PAGE_SIZE;
        credReq.op     = HIL::OpType::CREDIT_ONLY;

        gScheduler->submitRequest(credReq);
        
        // 等待直到用戶不在pending queue
        while (gScheduler->pendingForUser(uid)) {
            simTick += 10;
            gScheduler->tick(simTick);
        }
        // ★ 注意：走 gate 的路徑不要再扣一次
    } else {
        // 足額 → 當下即時扣除
        gScheduler->useCreditISC(uid, pages);
    }
    
    pr("FTL::read | uid=%u | Credit charged: %lu pages, continue I/O | simTick=%lu",
       uid, pages, simTick);
  }

#endif /* !ISC_TEST */

  /* ---------- SimpleSSD Fast Path  ---------- */

#ifndef ISC_TEST
  auto hReq = *(HIL::Request *)simCtx;
  auto ns = (HIL::NVMe::Namespace *)hReq.ns;
  
  hReq.userID = uid;  
  size_t slba = ofs / FTL::lbaSize;
  size_t nlblk = (sz + FTL::lbaSize - 1) / FTL::lbaSize;
  ns->pParent->convertUnit(ns, slba, nlblk, hReq);

  ICL::Request cReq(hReq);
  pr("Changed cReq: {slpn,nlp}={%lu,%lu} | ofs,len=%lu,%lu", cReq.range.slpn,
     cReq.range.nlp, cReq.offset, cReq.length);

  // FTL latencies should already handled inside
  ((SimpleSSD::ICL::ICL *)cache)->read(cReq, simTick);

#endif

  doit(buf, ofs, sz _add_sim_params);
}

}  // namespace SIM
}  // namespace ISC
}  // namespace SimpleSSD
