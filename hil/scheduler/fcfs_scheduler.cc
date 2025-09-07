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
  // Initialize predefined users for statistics consistency
  statUsers_.clear();
  statUsers_.push_back(1001);
  statUsers_.push_back(1002);
  
  // Pre-create user accounts
  for (uint32_t uid : statUsers_) {
    auto &acc = getOrCreateUser(uid);
    acc.isActive = false;
    debugprint(LOG_HIL, "FCFS Scheduler | Init user uid=%u", uid);
  }
  
  debugprint(LOG_HIL, "FCFS Scheduler initialized with multi-user ISC support");
}

FCFSScheduler::~FCFSScheduler() {
  debugprint(LOG_HIL, "FCFS Scheduler destroyed");
}

void FCFSScheduler::submitRequest(Request &req) {
  auto &acc = getOrCreateUser(req.userID);
  acc.queue.push(req);
  acc.isActive = true;
  
  debugprint(LOG_HIL,
             "FCFS Scheduler | Submit HOST request %lu uid=%u | Size %lu | User queue size: %zu",
             req.reqID, req.userID, req.length, acc.queue.size());
}

void FCFSScheduler::submitRequestISC(uint32_t uid, Request &req) {
  auto &acc = getOrCreateUser(uid);
  acc.queueISC.push(req);
  acc.isActive = true;
  
  debugprint(LOG_HIL,
             "FCFS Scheduler | Submit ISC request %lu uid=%u | Size %lu | ISC queue size: %zu",
             req.reqID, uid, req.length, acc.queueISC.size());
}

void FCFSScheduler::schedule() {
  // Round-Robin dispatch: try Host I/O first, then ISC
  bool dispatched = false;
  
  // Try to dispatch one Host I/O request
  if (tryDispatchHost()) {
    dispatched = true;
  }
  
  // Try to dispatch one ISC request
  if (tryDispatchISC()) {
    dispatched = true;
  }
  
  if (!dispatched) {
    FCFS_DBG("FCFS Scheduler | No requests to dispatch");
  }
}

bool FCFSScheduler::tryDispatchHost() {
  if (users.empty()) return false;
  
  // Find starting point for Round-Robin
  auto it = users.find(lastChosenUid);
  if (it == users.end() || ++it == users.end()) {
    it = users.begin();
  }
  
  size_t visited = 0;
  while (visited++ < users.size()) {
    if (it == users.end()) it = users.begin();
    
    UserAccount &acc = it->second;
    if (!acc.queue.empty()) {
      Request req = acc.queue.front();
      acc.queue.pop();
      
      lastChosenUid = it->first;
      dispatchRequest(req, acc, false); // Host I/O
      
      FCFS_DBG("FCFS dispatch[HOST]: uid=%u reqID=%lu pages=%lu hostQueue=%zu",
               lastChosenUid, req.reqID, (req.length + PageSz - 1) / PageSz, acc.queue.size());
      return true;
    }
    ++it;
  }
  return false;
}

bool FCFSScheduler::tryDispatchISC() {
  if (users.empty()) return false;
  
  // Find starting point for Round-Robin
  auto it = users.find(lastChosenUidISC);
  if (it == users.end() || ++it == users.end()) {
    it = users.begin();
  }
  
  size_t visited = 0;
  while (visited++ < users.size()) {
    if (it == users.end()) it = users.begin();
    
    UserAccount &acc = it->second;
    if (!acc.queueISC.empty()) {
      Request req = acc.queueISC.front();
      acc.queueISC.pop();
      
      lastChosenUidISC = it->first;
      dispatchRequest(req, acc, true); // ISC
      
      FCFS_DBG("FCFS dispatch[ISC]: uid=%u reqID=%lu pages=%lu iscQueue=%zu",
               lastChosenUidISC, req.reqID, (req.length + PageSz - 1) / PageSz, acc.queueISC.size());
      return true;
    }
    ++it;
  }
  return false;
}

void FCFSScheduler::dispatchRequest(const Request& req, UserAccount&, bool) {
  // Calculate page consumption and record it (統一統計邏輯)
  uint64_t pages = (req.length + PageSz - 1) / PageSz;
  recordPageConsumptionByOpType(req.userID, pages, req.op);

  // 使用統一的ICL調用方法 (模仿Credit Scheduler)
  dispatchICL(req, currentTick);
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

bool FCFSScheduler::pendingForUser(uint32_t uid) const {
  auto it = users.find(uid);
  if (it != users.end()) {
    return !it->second.queue.empty() || !it->second.queueISC.empty();
  }
  return false;
}

FCFSScheduler::UserAccount& FCFSScheduler::getOrCreateUser(uint32_t uid) {
  auto it = users.find(uid);
  if (it == users.end()) {
    UserAccount newAccount;
    newAccount.isActive = false;
    users[uid] = newAccount;
    FCFS_DBG("FCFS Scheduler | Created new user uid=%u", uid);
  }
  return users[uid];
}

void FCFSScheduler::recordPageConsumption(uint32_t uid, uint64_t pages, bool isISC) {
  auto &acc = getOrCreateUser(uid);
  acc.totalConsumed += pages;
  
  if (isISC) {
    acc.consumedISC += pages;
  } else {
    acc.consumedHost += pages;
  }
  
  FCFS_DBG("FCFS Scheduler | User %u consumed %lu pages %s, total=%lu (host=%lu, isc=%lu)", 
           uid, pages, isISC ? "[ISC]" : "[HOST]", 
           acc.totalConsumed, acc.consumedHost, acc.consumedISC);
}

void FCFSScheduler::recordPageConsumptionByOpType(uint32_t uid, uint64_t pages, OpType op) {
  auto &acc = getOrCreateUser(uid);
  acc.totalConsumed += pages;
  
  // 統一統計邏輯：與Credit Scheduler相同的判斷
  if (op == OpType::READ || op == OpType::WRITE) {
    acc.consumedHost += pages;
  } else {
    acc.consumedISC += pages;
  }
  
  FCFS_DBG("FCFS Scheduler | User %u consumed %lu pages %s (op=%d), total=%lu (host=%lu, isc=%lu)", 
           uid, pages, (op == OpType::READ || op == OpType::WRITE) ? "[HOST]" : "[ISC]", 
           static_cast<int>(op), acc.totalConsumed, acc.consumedHost, acc.consumedISC);
}

void FCFSScheduler::dispatchICL(const Request& req, uint64_t &tick) {
  // 與Credit Scheduler的dispatchICL完全相同的邏輯
  Request reqCopy = req;
  ICL::Request iclReq(reqCopy);
  
  FCFS_DBG("FCFS ICL: t=%lu uid=%u op=%d len=%lu",
           tick, req.userID, static_cast<int>(req.op), req.length);
           
  switch (req.op) {
    case OpType::READ:
      pICL->read(iclReq, tick);
      break;
    case OpType::WRITE:
      pICL->write(iclReq, tick);
      break;
    default:
      // ISC tasks或其他OpType也能正確處理
      FCFS_DBG("FCFS ICL: Unsupported OpType %d, skipping ICL call", static_cast<int>(req.op));
      break;
  }
}

void FCFSScheduler::getStatList(std::vector<Stats> &list, std::string prefix) {
  // Create statistics for predefined user range (consistent with Credit Scheduler)
  for (uint32_t uid = MIN_USER_ID; uid <= MAX_USER_ID; uid++) {
    Stats s;
    s.name = prefix + "fcfs.user" + std::to_string(uid) + ".consumed_total";
    s.desc = "Total pages consumed by uid " + std::to_string(uid);
    list.push_back(s);
    
    s.name = prefix + "fcfs.user" + std::to_string(uid) + ".consumed_host";
    s.desc = "Host pages consumed by uid " + std::to_string(uid);
    list.push_back(s);
    
    s.name = prefix + "fcfs.user" + std::to_string(uid) + ".consumed_isc";
    s.desc = "ISC pages consumed by uid " + std::to_string(uid);
    list.push_back(s);
  }
  
  Stats t;
  t.name = prefix + "fcfs.total_consumed";
  t.desc = "Total pages consumed by all users";
  list.push_back(t);
  
  t.name = prefix + "fcfs.active_users";
  t.desc = "Number of active users";
  list.push_back(t);
}

void FCFSScheduler::getStatValues(std::vector<double> &values) {
  // Calculate total consumption across all users
  uint64_t totalConsumed = 0;
  uint32_t activeUsers = 0;
  
  for (const auto &entry : users) {
    totalConsumed += entry.second.totalConsumed;
    if (entry.second.isActive) activeUsers++;
  }
  
  // Return statistics in order consistent with getStatList
  for (uint32_t uid = MIN_USER_ID; uid <= MAX_USER_ID; uid++) {
    auto it = users.find(uid);
    if (it != users.end()) {
      values.push_back(static_cast<double>(it->second.totalConsumed));
      values.push_back(static_cast<double>(it->second.consumedHost));
      values.push_back(static_cast<double>(it->second.consumedISC));
    } else {
      values.push_back(0.0);  // totalConsumed
      values.push_back(0.0);  // consumedHost  
      values.push_back(0.0);  // consumedISC
    }
  }
  
  values.push_back(static_cast<double>(totalConsumed));
  values.push_back(static_cast<double>(activeUsers));
}

void FCFSScheduler::resetStatValues() {
  users.clear();
  lastReportTick = 0;
  lastChosenUid = 0;
  lastChosenUidISC = 0;
  
  // Reinitialize predefined users
  for (uint32_t uid : statUsers_) {
    getOrCreateUser(uid);
  }
  
  FCFS_DBG("FCFS Scheduler | Statistics reset");
}

void FCFSScheduler::processUntil(Request &req, uint64_t &completionTick) {
  debugprint(LOG_HIL, "FCFS | processUntil | uid=%u op=%d len=%" PRIu64,
             req.userID, static_cast<int>(req.op), req.length);
  
  // FCFS directly processes, no waiting required
  submitRequest(req);
  tick(completionTick);
  
  // Update completion time after processing
  completionTick += applyLatency(CPU::FCFS_SCHEDULER, CPU::SCHEDULE);
}

void FCFSScheduler::reportPageConsumption() {
  if (users.empty()) {
    return;
  }
  
  // Calculate elapsed time in seconds  
  double elapsedSeconds = static_cast<double>(REPORT_INTERVAL_TICKS) / 1000000000.0;
  
  FCFS_DBG("FCFS Scheduler | Page consumption report (%.1f seconds):", elapsedSeconds);
  for (const auto& entry : users) {
    uint32_t uid = entry.first;
    const UserAccount& acc = entry.second;
    if (acc.totalConsumed > 0) {
      FCFS_DBG("  uid=%u: total=%lu (host=%lu, isc=%lu) active=%s",
               uid, acc.totalConsumed, acc.consumedHost, acc.consumedISC,
               acc.isActive ? "true" : "false");
    }
  }
}

}  // namespace HIL
}  // namespace SimpleSSD