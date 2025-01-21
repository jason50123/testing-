#include <fcntl.h>
#include <string.h>

#ifndef ISC_TEST
#include "hil/hil.hh"
#include "hil/nvme/subsystem.hh"
#include "icl/icl.hh"
#endif

#include "sims/ftl.hh"

#include "utils/debug.hh"
#include "utils/math.hh"

#define PR_SECTION LOG_ISC_UTIL_FTL

namespace SimpleSSD {
namespace ISC {
namespace SIM {

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
  auto hReq = *(HIL::Request *)simCtx;
  auto ns = (HIL::NVMe::Namespace *)hReq.ns;

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
