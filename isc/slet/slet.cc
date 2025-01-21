#include <sys/mman.h>

#include <cstring>  // for strcmp

#include "sims/dram.hh"
#include "types.hh"

using namespace SimpleSSD::ISC::SIM;
#define DRAM SIM::DRAM  // shadow SimpleSSD::DRAM

#define PR_SECTION LOG_ISC_SLET

namespace SimpleSSD {
namespace ISC {

GenericSlet::~GenericSlet() {
  // free options
  if (this->opt.name)
    free(this->opt.name);
  if (this->opt.cwd)
    free(this->opt.cwd);
  if (!this->opt.extents.empty())
    this->opt.extents.clear();

  if (!this->opt.extra.empty()) {
    for (auto &entry : this->opt.extra) {
      free((char *)entry.first);
      free(entry.second);
    }
    this->opt.extra.clear();
  }
}

ISC_STS GenericSlet::setOpt(const char *key, void *data) {
  pr("Set option '%s'=(%p)'%s'", key, data, (char *)data);

  if (!strcmp(key, "name")) {
    if (this->opt.name)
      free(this->opt.name);
    this->opt.name = (char *)data;
  }
  else if (!strcmp(key, "cwd")) {
    if (this->opt.cwd)
      free(this->opt.cwd);
    this->opt.cwd = (char *)data;
  }
  else {
    auto it = this->opt.extra.find(key);
    if (it != this->opt.extra.end()) {
      free((char *)it->first);
      free(it->second);
      this->opt.extra.erase(it);
    }
    this->opt.extra.insert({strdup(key), data});
  }

  return ISC_STS_OK;
}

void *GenericSlet::getOpt(const char *key) {
  pr("Get option '%s'", key);

  void *val = nullptr;
  if (!strcmp(key, "name"))
    val = this->opt.name;
  else if (!strcmp(key, "cwd"))
    val = this->opt.cwd;

  auto it = this->opt.extra.find(key);
  if (it != this->opt.extra.end())
    val = it->second;

  if (!val)
    pr("but option not found...");
  else
    pr("found at: %p", val);
  return val;
}

}  // namespace ISC
}  // namespace SimpleSSD