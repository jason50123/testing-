/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hil/hil.hh"
#include "hil/nvme/namespace.hh"

#include "util/algorithm.hh"

#include "isc/sims/ftl.hh"

#include "isc/fs/ext4/ext4.hh"
#include "isc/runtime.hh"

#include "isc/sims/configs.hh"
#include "isc/slet/grep.hh"
#include "isc/slet/md5.hh"
#include "isc/slet/statdir.hh"
#include "isc/slet/stats32.hh"
#include "isc/slet/stats64.hh"

using SimpleSSD::ISC::byte;

namespace SimpleSSD {

namespace HIL {

HIL::HIL(ConfigReader &c) : conf(c), reqCount(0), lastScheduled(0) {
  pICL = new ICL::ICL(conf);
  ISC::SIM::FTL::setCache(pICL);

  memset(&stat, 0, sizeof(stat));

  completionEvent = allocate([this](uint64_t) { completion(); });
}

HIL::~HIL() {
  delete pICL;
}

void HIL::read(Request &req) {
  DMAFunction doRead = [this](uint64_t beginAt, void *context) {
    auto pReq = (Request *)context;
    uint64_t tick = beginAt;

    pReq->reqID = ++reqCount;

    debugprint(LOG_HIL,
               "READ  | REQ %7u | LCA %" PRIu64 " + %" PRIu64 " | BYTE %" PRIu64
               " + %" PRIu64,
               pReq->reqID, pReq->range.slpn, pReq->range.nlp, pReq->offset,
               pReq->length);

    ICL::Request reqInternal(*pReq);
    pICL->read(reqInternal, tick);

    stat.request[0]++;
    stat.iosize[0] += pReq->length;
    updateBusyTime(0, beginAt, tick);
    updateBusyTime(2, beginAt, tick);

    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };

  execute(CPU::HIL, CPU::READ, doRead, new Request(req));
}

#define PR_SECTION LOG_HIL

void HIL::isc_set(Request &req) {
  DMAFunction doSet = [this](uint64_t beginAt, void *ctx) {
    uint64_t tick = beginAt;
    const auto hReq = (Request *)ctx;
    const auto slba = ((NVMe::IOContext *)hReq->context)->slba;

    hReq->reqID = ++reqCount;
    pr("ISC-SET | REQ %7u | LCA %" PRIu64 " + %" PRIu64 " | BYTE %" PRIu64
       " + %" PRIu64,
       hReq->reqID, hReq->range.slpn, hReq->range.nlp, hReq->offset,
       hReq->length);

    if (ISC_SUBCMD_IS(slba, ISC_SUBCMD_INIT)) {
      pr("Runtime Initialization -----------------------------------------");
      if (ISC_STS_FAIL == ISC::Runtime::addSlet<ISC::Ext4>(tick, ctx) ||
          ISC_STS_FAIL == ISC::Runtime::addSlet<ISC::StatdirAPP>(tick, ctx) ||
          ISC_STS_FAIL == ISC::Runtime::addSlet<ISC::MD5APP>(tick, ctx) ||
          ISC_STS_FAIL == ISC::Runtime::addSlet<ISC::GrepAPP>(tick, ctx) ||
          ISC_STS_FAIL == ISC::Runtime::addSlet<ISC::Stats32APP>(tick, ctx) ||
          ISC_STS_FAIL == ISC::Runtime::addSlet<ISC::Stats64APP>(tick, ctx))
        panic("Failed to setup predefined slets");

      tick += applyLatency(CPU::ISC__RUNTIME, CPU::ISC__ADD_SLET__EXT4);
      tick += applyLatency(CPU::ISC__RUNTIME, CPU::ISC__ADD_SLET__STATDIR);
      tick += applyLatency(CPU::ISC__RUNTIME, CPU::ISC__ADD_SLET__MD5);
      tick += applyLatency(CPU::ISC__RUNTIME, CPU::ISC__ADD_SLET__GREP);
      tick += applyLatency(CPU::ISC__RUNTIME, CPU::ISC__ADD_SLET__STATS32);
      tick += applyLatency(CPU::ISC__RUNTIME, CPU::ISC__ADD_SLET__STATS64);
      pr("Initialization done    -----------------------------------------");
    }
    else if (ISC_SUBCMD_IS(slba, ISC_SUBCMD_FREE)) {
      ISC::Runtime::destory();
    }
    else if (ISC_SUBCMD_IS(slba, ISC_SUBCMD_SLET_OPT)) {
      auto id = ISC_SUBCMD_OPT(slba);
      auto data = ((NVMe::IOContext *)hReq->context)->buffer;

      // fixme: handle calloc fails
      char *key = (char *)calloc(1, ISC_KEY_LEN + 1);
      memcpy(key, data, ISC_KEY_LEN);

      byte *val = (byte *)calloc(1, ISC_VAL_LEN(hReq->length) + 1);
      memcpy(val, data + ISC_KEY_LEN, ISC_VAL_LEN(hReq->length));

      ISC::Runtime::setOpt(id, key, val, tick, ctx);
      free(key);
    }
    else {
      panic("Unexpected ISC-SET CMD: 0x%x", ISC_SUBCMD(slba));
    }

    // ICL::Request reqInternal(*hReq);

    stat.request[1]++;
    stat.iosize[1] += hReq->length;
    updateBusyTime(1, beginAt, tick);
    updateBusyTime(2, beginAt, tick);

    hReq->finishedAt = tick;
    completionQueue.push(*hReq);
    updateCompletion();

    delete hReq;
  };
  execute(CPU::HIL, CPU::ISC__SET, doSet, new Request(req));
}

void HIL::isc_get(Request &hReq) {
  DMAFunction doISC = [this](uint64_t tick, void *ctx) {
    const auto beginAt = tick;
    const auto hReq = (Request *)ctx;
    const auto slba = ((NVMe::IOContext *)hReq->context)->slba;

    hReq->reqID = ++reqCount;

    pr("ISC-GET  | REQ %7u | LCA %" PRIu64 " + %" PRIu64 " | BYTE %" PRIu64
       " + %" PRIu64,
       hReq->reqID, hReq->range.slpn, hReq->range.nlp, hReq->offset,
       hReq->length);

    if (ISC_SUBCMD_IS(slba, ISC_SUBCMD_SLET_RUN)) {
      pr("Runtime startSlet      -----------------------------------------");
      auto id = ISC_SUBCMD_OPT(slba);
      auto res = ISC::Runtime::startSlet(id, tick, ctx);
      if (ISC_STS_FAIL == res) {
        pr("failed to start slet: %d", id);
      }

      pr("startSlet done         -----------------------------------------");
    }
    else if (ISC_SUBCMD_IS(slba, ISC_SUBCMD_SLET_RES) ||
             ISC_SUBCMD_IS(slba, ISC_SUBCMD_SLET_RESSZ))
      ;  // nothing to do here, just add latency
    else {
      panic("Unexpected ISC-GET SUBCMD: 0x%x", ISC_SUBCMD(slba));
    }

    stat.request[0]++;
    stat.iosize[0] += hReq->length;
    updateBusyTime(0, beginAt, tick);
    updateBusyTime(2, beginAt, tick);

    hReq->finishedAt = tick;
    completionQueue.push(*hReq);

    updateCompletion();

    delete hReq;
  };
  execute(CPU::HIL, CPU::ISC__GET, doISC, new Request(hReq));
}

void HIL::write(Request &req) {
  DMAFunction doWrite = [this](uint64_t beginAt, void *context) {
    auto pReq = (Request *)context;
    uint64_t tick = beginAt;

    pReq->reqID = ++reqCount;

    debugprint(LOG_HIL,
               "WRITE | REQ %7u | LCA %" PRIu64 " + %" PRIu64 " | BYTE %" PRIu64
               " + %" PRIu64,
               pReq->reqID, pReq->range.slpn, pReq->range.nlp, pReq->offset,
               pReq->length);

    ICL::Request reqInternal(*pReq);
    pICL->write(reqInternal, tick);

    stat.request[1]++;
    stat.iosize[1] += pReq->length;
    updateBusyTime(1, beginAt, tick);
    updateBusyTime(2, beginAt, tick);

    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };

  execute(CPU::HIL, CPU::WRITE, doWrite, new Request(req));
}

void HIL::flush(Request &req) {
  DMAFunction doFlush = [this](uint64_t tick, void *context) {
    auto pReq = (Request *)context;

    pReq->reqID = ++reqCount;

    debugprint(LOG_HIL, "FLUSH | REQ %7u | LCA %" PRIu64 " + %" PRIu64,
               pReq->reqID, pReq->range.slpn, pReq->range.nlp);

    pICL->flush(pReq->range, tick);

    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };

  execute(CPU::HIL, CPU::FLUSH, doFlush, new Request(req));
}

void HIL::trim(Request &req) {
  DMAFunction doFlush = [this](uint64_t tick, void *context) {
    auto pReq = (Request *)context;

    pReq->reqID = ++reqCount;

    debugprint(LOG_HIL, "TRIM  | REQ %7u | LCA %" PRIu64 " + %" PRIu64,
               pReq->reqID, pReq->range.slpn, pReq->range.nlp);

    pICL->trim(pReq->range, tick);

    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };

  execute(CPU::HIL, CPU::FLUSH, doFlush, new Request(req));
}

void HIL::format(Request &req, bool erase) {
  DMAFunction doFlush = [this, erase](uint64_t tick, void *context) {
    auto pReq = (Request *)context;

    debugprint(LOG_HIL, "FORMAT| LCA %" PRIu64 " + %" PRIu64, pReq->reqID,
               pReq->range.slpn, pReq->range.nlp);

    if (erase) {
      pICL->format(pReq->range, tick);
    }
    else {
      pICL->trim(pReq->range, tick);
    }

    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };

  execute(CPU::HIL, CPU::FLUSH, doFlush, new Request(req));
}

void HIL::getLPNInfo(uint64_t &totalLogicalPages, uint32_t &logicalPageSize) {
  pICL->getLPNInfo(totalLogicalPages, logicalPageSize);
}

uint64_t HIL::getUsedPageCount(uint64_t lcaBegin, uint64_t lcaEnd) {
  return pICL->getUsedPageCount(lcaBegin, lcaEnd);
}

void HIL::updateBusyTime(int idx, uint64_t begin, uint64_t end) {
  if (end <= stat.lastBusyAt[idx]) {
    return;
  }

  if (begin < stat.lastBusyAt[idx]) {
    stat.busy[idx] += end - stat.lastBusyAt[idx];
  }
  else {
    stat.busy[idx] += end - begin;
  }

  stat.lastBusyAt[idx] = end;
}

void HIL::updateCompletion() {
  if (completionQueue.size() > 0) {
    if (lastScheduled != completionQueue.top().finishedAt) {
      lastScheduled = completionQueue.top().finishedAt;
      schedule(completionEvent, lastScheduled);
    }
  }
}

void HIL::completion() {
  uint64_t tick = getTick();

  while (completionQueue.size() > 0) {
    auto &req = completionQueue.top();

    if (req.finishedAt <= tick) {
      req.function(tick, req.context);

      completionQueue.pop();
    }
    else {
      break;
    }
  }

  updateCompletion();
}

void HIL::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "read.request_count";
  temp.desc = "Read request count";
  list.push_back(temp);

  temp.name = prefix + "read.bytes";
  temp.desc = "Read data size in byte";
  list.push_back(temp);

  temp.name = prefix + "read.busy";
  temp.desc = "Device busy time when read";
  list.push_back(temp);

  temp.name = prefix + "write.request_count";
  temp.desc = "Write request count";
  list.push_back(temp);

  temp.name = prefix + "write.bytes";
  temp.desc = "Write data size in byte";
  list.push_back(temp);

  temp.name = prefix + "write.busy";
  temp.desc = "Device busy time when write";
  list.push_back(temp);

  temp.name = prefix + "request_count";
  temp.desc = "Total request count";
  list.push_back(temp);

  temp.name = prefix + "bytes";
  temp.desc = "Total data size in byte";
  list.push_back(temp);

  temp.name = prefix + "busy";
  temp.desc = "Total device busy time";
  list.push_back(temp);

  pICL->getStatList(list, prefix);
}

void HIL::getStatValues(std::vector<double> &values) {
  values.push_back(stat.request[0]);
  values.push_back(stat.iosize[0]);
  values.push_back(stat.busy[0]);
  values.push_back(stat.request[1]);
  values.push_back(stat.iosize[1]);
  values.push_back(stat.busy[1]);
  values.push_back(stat.request[0] + stat.request[1]);
  values.push_back(stat.iosize[0] + stat.iosize[1]);
  values.push_back(stat.busy[2]);

  pICL->getStatValues(values);
}

void HIL::resetStatValues() {
  memset(&stat, 0, sizeof(stat));

  pICL->resetStatValues();
}

}  // namespace HIL

}  // namespace SimpleSSD
