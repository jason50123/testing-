#include "hil/scheduler/fcfs_scheduler.hh"
#include "util/algorithm.hh"
#include "util/def.hh"

namespace SimpleSSD {
namespace HIL {

#define PR_SECTION LOG_HIL_FCFS_SCHEDULER

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096ULL
#endif

// Debug output control
#ifndef FCFS_DBG_ON
#define FCFS_DBG_ON 1
#endif
#define FCFS_DBG(fmt, ...) \
        do{ if (FCFS_DBG_ON) debugprint(LOG_HIL, fmt, ##__VA_ARGS__);}while(0)

FCFSScheduler::FCFSScheduler(ICL::ICL* iclPtr)
    : pICL(iclPtr), lastReportTick(0) {
  debugprint(LOG_HIL, "FCFS Scheduler initialized with page consumption tracking");
}

FCFSScheduler::~FCFSScheduler() {
  debugprint(LOG_HIL, "FCFS Scheduler destroyed");
}

void FCFSScheduler::submitRequest(Request &req) {
  debugprint(LOG_HIL,
             "FCFS Scheduler | Submit request %lu | Size %lu | Queue size: %zu",
             req.reqID, req.length, requestQueue.size());
  requestQueue.push(req);
}

void FCFSScheduler::schedule() {
  debugprint(LOG_HIL, "FCFS Scheduler | Schedule called | Queue size: %zu", requestQueue.size());
  
  if (!requestQueue.empty()) {
    Request req = requestQueue.front();
    requestQueue.pop();

    // Calculate page consumption for this request
    uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
    recordPageConsumption(req.userID, pages);

    // 可以這裡加 scheduler latency
    currentTick += applyLatency(CPU::FCFS_SCHEDULER, CPU::SCHEDULE);

    ICL::Request iclReq(req);

        switch (req.op) {
        case OpType::READ:
            pICL->read (iclReq, currentTick);
            break;
        case OpType::WRITE:
            pICL->write(iclReq, currentTick);
            break;
        default:
            panic("Unknown OpType in scheduler");
    }

    debugprint(LOG_HIL,
               "Dispatch req %lu uid=%u op=%d len=%lu pages=%lu tick=%lu",
               req.reqID, req.userID, static_cast<int>(req.op),
               req.length, pages, currentTick);
  }
}

void FCFSScheduler::tick(uint64_t now) {
  currentTick = now;
  
  // Check if we need to report page consumption (every second)
  if (now - lastReportTick >= REPORT_INTERVAL_TICKS) {
    reportPageConsumption();
    lastReportTick = now;
  }
  
  schedule();
}

void FCFSScheduler::getStatList(std::vector<Stats> &list, std::string prefix) {
  // 為預定義用戶範圍創建統計項目（支持 gem5 統計系統初始化）
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
  // 計算所有用戶的總消耗（包括預定義範圍外的用戶）
  uint64_t totalConsumed = 0;
  for (const auto &entry : userPageConsumption) {
    totalConsumed += entry.second;
  }
  
  // 按預定義用戶範圍順序返回統計值（與 getStatList 順序一致）
  for (uint32_t uid = MIN_USER_ID; uid <= MAX_USER_ID; uid++) {
    auto it = userPageConsumption.find(uid);
    if (it != userPageConsumption.end()) {
      values.push_back(static_cast<double>(it->second));
    } else {
      values.push_back(0.0);  // 沒有資料的用戶返回 0
    }
  }
  
  values.push_back(static_cast<double>(totalConsumed));
  values.push_back(static_cast<double>(requestQueue.size()));
}

void FCFSScheduler::resetStatValues() {
  userPageConsumption.clear();
  lastReportTick = 0;
  
  // Clear the request queue
  while (!requestQueue.empty()) {
    requestQueue.pop();
  }
  
  FCFS_DBG("FCFS Scheduler | Statistics reset");
}

void FCFSScheduler::recordPageConsumption(uint32_t uid, uint64_t pages) {
  userPageConsumption[uid] += pages;
  FCFS_DBG("FCFS Scheduler | User %u consumed %lu pages, total=%lu", 
           uid, pages, userPageConsumption[uid]);
}

void FCFSScheduler::processUntil(Request &req, uint64_t &completionTick) {
  debugprint(LOG_HIL, "FCFS | processUntil | uid=%u op=%d len=%" PRIu64,
             req.userID, static_cast<int>(req.op), req.length);
  
  // FCFS 直接處理，不需要等待
  submitRequest(req);
  tick(completionTick);
  
  // 處理完成後更新時間
  completionTick += applyLatency(CPU::FCFS_SCHEDULER, CPU::SCHEDULE);
}

void FCFSScheduler::reportPageConsumption() {
  if (userPageConsumption.empty()) {
    return;
  }
  
  // Calculate elapsed time in seconds
  double elapsedSeconds = static_cast<double>(REPORT_INTERVAL_TICKS) / 50000000.0;
  
  FCFS_DBG("FCFS Scheduler | Page consumption report (%.1f seconds):", elapsedSeconds);
  for (const auto& entry : userPageConsumption) {
    uint32_t uid = entry.first;
    uint64_t pages = entry.second;
    FCFS_DBG("  uid=%u: %lu pages consumed", uid, pages);
  }
}

}  // namespace HIL
}  // namespace SimpleSSD