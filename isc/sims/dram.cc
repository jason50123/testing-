#ifndef ISC_TEST
#include "dram/abstract_dram.hh"

using DRAM_SIM = SimpleSSD::DRAM::AbstractDRAM;
#endif

#include <cerrno>
#include <cstdlib>

#include <algorithm>
#include <list>

#include "sims/dram.hh"
#include "utils/debug.hh"

#define PR_SECTION LOG_ISC_UTIL_MEM

namespace SimpleSSD {
namespace ISC {
namespace SIM {

class BaseRegion : public DRAM::Region {
 public:
  BaseRegion() : Region() {}
  BaseRegion(void *a, size_t n, size_t u, Cmp_t c, Cpy_t cpi, Cpy_t cpo)
      : Region(a, n, u, c, cpi, cpo) {}

  virtual int read(size_t o, size_t s, void *d _ADD_SIM_PARAMS) override {
    cpout(d, addr + o, s);
    DRAM::read(d, s _add_sim_params);
    return 0;
  }
  virtual int write(size_t o, size_t s, void *d _ADD_SIM_PARAMS) override {
    cpin(addr + o, d, s);
    DRAM::write(d, s _add_sim_params);
    return 0;
  }
};

class LRURegion : public BaseRegion {
  std::list<size_t> ofsFree;
  std::list<size_t> ofsLRU;

 public:
  LRURegion(void *a, size_t n, size_t u, Cmp_t c, Cpy_t cpi, Cpy_t cpo)
      : BaseRegion(a, n, u, c, cpi, cpo) {
    for (size_t i = 0; i < nmem; ++i)
      ofsFree.push_back(i * unit);
  }

  virtual int read(size_t, size_t, void *dat _ADD_SIM_PARAMS) override {
    // check data still valid
    for (auto ofs : ofsLRU) {
      if (!cmp(addr + ofs, dat, unit)) {
        ofsLRU.remove(ofs);
        ofsLRU.push_front(ofs);
        pr("Found data at offset: %lu", ofs);
        return BaseRegion::read(ofs, unit, dat _add_sim_params);
      }
    }
    pr("Required data not exists or already be evicted");
    return -ENOENT;
  }

  virtual int write(size_t, size_t, void *dat _ADD_SIM_PARAMS) override {
    size_t ofs;

    // check data still want to overwrite
    for (auto ofs : ofsLRU) {
      if (!cmp(addr + ofs, dat, unit)) {
        ofsLRU.remove(ofs);
        ofsLRU.push_front(ofs);
        pr("Overwrite data at offset: %lu", ofs);
        return BaseRegion::write(ofs, unit, dat _add_sim_params);
      }
    }

    // evict if full
    assert(ofsLRU.size() <= nmem);
    if (ofsLRU.size() == nmem) {
      ofs = ofsLRU.back();
      ofsLRU.pop_back();
      ofsLRU.push_front(ofs);
      pr("Evict data at offset %lu", ofs);
    }
    else {
      assert(ofsFree.size() > 0);
      ofs = ofsFree.front();
      ofsFree.pop_front();
      ofsLRU.push_front(ofs);
      pr("Take unused offset: %lu", ofs);
    }
    return BaseRegion::write(ofs, unit, dat _add_sim_params);
  }
};

/* -------------------------------------------------------------------------- */
/*                             init static members                            */
/* -------------------------------------------------------------------------- */

#ifndef ISC_TEST
void *DRAM::pDRAM;
#endif

static size_t szUsed, szPeakUsed;
static std::list<BaseRegion *> regions;

/* -------------------------------------------------------------------------- */
/*                          function implementations                          */
/* -------------------------------------------------------------------------- */

DRAM::Region *DRAM::alloc(size_t nmem, size_t unit, TYPES type,
                          Region::Cmp_t cmp, Region::Cpy_t cpi,
                          Region::Cpy_t cpo) {
  auto addr = calloc(nmem, unit);
  if (!addr) {
    panic("Unabled to allocate memory...");
    assert(0);
  }

  // record this address for more checking
  auto szWanted = nmem * unit;
  szUsed += szWanted;
  szPeakUsed = std::max(szUsed, szPeakUsed);

  BaseRegion *reg;
  switch (type) {
    case TYPES::LRU_CACHE:
      reg = new LRURegion(addr, nmem, unit, cmp, cpi, cpo);
      break;
    default:
      reg = new BaseRegion(addr, nmem, unit, cmp, cpi, cpo);
  }
  regions.push_back(reg);
  return reg;
}

void DRAM::dealloc(DRAM::Region *reg) {
  auto it = std::find(regions.begin(), regions.end(), reg);
  if (it != regions.end()) {
    szUsed -= (*it)->getSize();
    regions.erase(it);
    delete reg;
  }
}

void DRAM::destroy() {
  pr("Peak DRAM Usage: %lu Bytes", szPeakUsed);

  for (auto &reg : regions)
    delete reg;
  regions.clear();
}

// Wrappers for external managed memory
void DRAM::read(void *data, size_t sz _ADD_SIM_PARAMS) {
#ifdef ISC_TEST
  unused_values(data, sz _add_sim_params);
#else
  ((DRAM_SIM *)pDRAM)->read(data, sz, simTick);
#endif
}

void DRAM::write(void *data, size_t sz _ADD_SIM_PARAMS) {
#ifdef ISC_TEST
  unused_values(data, sz);
#else
  ((DRAM_SIM *)pDRAM)->write(data, sz, simTick);
#endif
}

}  // namespace SIM
}  // namespace ISC
}  // namespace SimpleSSD