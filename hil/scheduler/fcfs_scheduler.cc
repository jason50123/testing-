#include "hil/scheduler/fcfs_scheduler.hh"
#include "util/algorithm.hh"
#include "util/def.hh"

namespace SimpleSSD {

namespace HIL {

FCFSScheduler::FCFSScheduler() {
  debugprint(LOG_HIL, "FCFS Scheduler initialized");
}

FCFSScheduler::~FCFSScheduler() {
  debugprint(LOG_HIL, "FCFS Scheduler destroyed");
}

void FCFSScheduler::submitRequest(Request &req) {
  debugprint(LOG_HIL, "FCFS Scheduler | Submit request %" PRIu64 " | Type %s | Size %" PRIu64 " | Queue size: %zu",
             req.reqID,
             req.range.slpn == 0 ? "READ" : "WRITE",
             req.length,
             requestQueue.size());
  requestQueue.push(req);
}

void FCFSScheduler::schedule() {
  debugprint(LOG_HIL, "FCFS Scheduler | Schedule called | Queue size: %zu", requestQueue.size());
  
  if (!requestQueue.empty()) {
    Request req = requestQueue.front();
    requestQueue.pop();
    
    debugprint(LOG_HIL, "FCFS Scheduler | Process request %" PRIu64 " | Type %s | Size %" PRIu64 " | Remaining queue: %zu",
               req.reqID,
               req.range.slpn == 0 ? "READ" : "WRITE",
               req.length,
               requestQueue.size());
  }
}

void FCFSScheduler::tick(uint64_t now) {
  debugprint(LOG_HIL, "FCFS Scheduler | Tick called at %" PRIu64, now);
  currentTick = now;
  schedule();
}

void FCFSScheduler::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "fcfs.request_count";
  temp.desc = "FCFS request count";
  list.push_back(temp);

  temp.name = prefix + "fcfs.queue_length";
  temp.desc = "Current queue length";
  list.push_back(temp);
}

void FCFSScheduler::getStatValues(std::vector<double> &values) {
  values.push_back(0);  // request_count
  values.push_back(requestQueue.size());  // queue_length
}

void FCFSScheduler::resetStatValues() {
  // 清空統計資料
  stats.clear();
}

}  // namespace HIL

}  // namespace SimpleSSD