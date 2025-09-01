#include "hil/scheduler/fcfs_scheduler.hh"
#include "util/algorithm.hh"
#include "util/def.hh"

namespace SimpleSSD { namespace HIL {

#define PR_SECTION LOG_HIL_FCFS_SCHEDULER
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096ULL
#endif

FCFSScheduler::FCFSScheduler(ICL::ICL* iclPtr)
    : pICL(iclPtr), lastReportTick(0) {
  debugprint(LOG_HIL, "FCFS Scheduler initialized with page consumption tracking");
}

FCFSScheduler::~FCFSScheduler() {
  debugprint(LOG_HIL, "FCFS Scheduler destroyed");
}

void FCFSScheduler::submitRequest(Request &req) {
  debugprint(LOG_HIL,
             "FCFS | submit req=%" PRIu64 " len=%" PRIu64 " Q=%zu",
             req.reqID, req.length, requestQueue.size());
  requestQueue.push(req);
}

void FCFSScheduler::schedule() {
  debugprint(LOG_HIL, "FCFS | schedule Q=%zu", requestQueue.size());
  if (!requestQueue.empty()) {
    Request req = requestQueue.front();
    requestQueue.pop();

    uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
    recordPageConsumption(req.userID, pages);

    currentTick += applyLatency(CPU::FCFS_SCHEDULER, CPU::SCHEDULE);

    ICL::Request iclReq(req);
    switch (req.op) {
      case OpType::READ:  pICL->read (iclReq, currentTick); break;
      case OpType::WRITE: pICL->write(iclReq, currentTick); break;
      default:            panic("FCFS: unknown OpType");
    }

    debugprint(LOG_HIL, "FCFS | dispatched req=%" PRIu64 " uid=%u pages=%" PRIu64 " tick=%" PRIu64,
               req.reqID, req.userID, pages, currentTick);
  }
}

void FCFSScheduler::tick(uint64_t &now) {
  currentTick = now;

  if (now - lastReportTick >= REPORT_INTERVAL_TICKS) {
    reportPageConsumption();
    lastReportTick = now;
  }

  schedule();
  now = currentTick;          // ★ 回填完成時間
}

// ★ 同步處理直到「該 req 完成」：把所有在前面的先跑完，再跑到目標 req
void FCFSScheduler::processUntil(Request &req, uint64_t &now) {
  submitRequest(req);

  // 目標 reqID
  const uint64_t target = req.reqID;

  // 逐一把 queue 裡的 req 跑到 target 為止
  while (true) {
    if (requestQueue.empty()) {
      // 理論上不會發生（剛剛才 push），但保險一下
      break;
    }

    Request cur = requestQueue.front();
    requestQueue.pop();

    uint64_t pages = (cur.length + PAGE_SIZE - 1) / PAGE_SIZE;
    recordPageConsumption(cur.userID, pages);

    currentTick = now;
    currentTick += applyLatency(CPU::FCFS_SCHEDULER, CPU::SCHEDULE);

    ICL::Request iclReq(cur);
    switch (cur.op) {
      case OpType::READ:  pICL->read (iclReq, currentTick); break;
      case OpType::WRITE: pICL->write(iclReq, currentTick); break;
      default:            panic("FCFS: unknown OpType");
    }

    now = currentTick;        // ★ 更新呼叫端 now

    debugprint(LOG_HIL, "FCFS | processUntil dispatched req=%" PRIu64 " tick=%" PRIu64,
               cur.reqID, now);

    if (cur.reqID == target) break; // 目標已完成
  }
}

void FCFSScheduler::getStatList(std::vector<Stats> &list, std::string prefix) {
  for (uint32_t uid = MIN_USER_ID; uid <= MAX_USER_ID; uid++) {
    Stats s;
    s.name = prefix + "fcfs.user" + std::to_string(uid) + ".consumed";
    s.desc = "Pages consumed by uid " + std::to_string(uid);
    list.push_back(s);
  }
  Stats t;
  t.name = prefix + "fcfs.total_consumed";
  t.desc = "Total pages consumed by all users";
  list.push_back(t);

  t.name = prefix + "fcfs.queue_length";
  t.desc = "Current queue length";
  list.push_back(t);
}

void FCFSScheduler::getStatValues(std::vector<double> &values) {
  uint64_t totalConsumed = 0;
  for (const auto &entry : userPageConsumption) totalConsumed += entry.second;

  for (uint32_t uid = MIN_USER_ID; uid <= MAX_USER_ID; uid++) {
    auto it = userPageConsumption.find(uid);
    values.push_back(it != userPageConsumption.end() ? double(it->second) : 0.0);
  }
  values.push_back(double(totalConsumed));
  values.push_back(double(requestQueue.size()));
}

void FCFSScheduler::resetStatValues() {
  userPageConsumption.clear();
  lastReportTick = 0;
  while (!requestQueue.empty()) requestQueue.pop();
  debugprint(LOG_HIL, "FCFS | stats reset");
}

void FCFSScheduler::recordPageConsumption(uint32_t uid, uint64_t pages) {
  userPageConsumption[uid] += pages;
}

void FCFSScheduler::reportPageConsumption() {
  if (userPageConsumption.empty()) return;
  double elapsedSeconds = double(REPORT_INTERVAL_TICKS) / 50000000.0;
  debugprint(LOG_HIL, "FCFS | page consumption report (%.1f s)", elapsedSeconds);
  for (const auto& kv : userPageConsumption) {
    debugprint(LOG_HIL, "  uid=%u: %lu pages", kv.first, kv.second);
  }
}

} } // namespace
