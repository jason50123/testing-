#include "hil/scheduler/scheduler.hh"
#include "util/algorithm.hh"
#include "util/def.hh"

namespace SimpleSSD { namespace HIL {

Scheduler::Scheduler() : currentTick(0) {
  debugprint(LOG_HIL, "Scheduler initialized");
}

Scheduler::~Scheduler() {
  debugprint(LOG_HIL, "Scheduler destroyed");
}

void Scheduler::submitRequest(Request &req) {
  debugprint(LOG_HIL, "Scheduler | Submit request %" PRIu64 " | op=%d | len=%" PRIu64,
             req.reqID, int(req.op), req.length);
  requestQueue.push(req);
}

void Scheduler::schedule() {
  // 預設 FCFS 單步（讓子類覆寫）
  if (!requestQueue.empty()) {
    Request req = requestQueue.front();
    requestQueue.pop();
    debugprint(LOG_HIL, "Scheduler | Process request %" PRIu64 " | op=%d | len=%" PRIu64,
               req.reqID, int(req.op), req.length);
  }
}

void Scheduler::tick(uint64_t &now) {   // ★ by-ref
  currentTick = now;
  schedule();
  now = currentTick;                    // 預設回填
}

// ★ 預設「同步到完成」的行為（子類別會覆寫）
void Scheduler::processUntil(Request &req, uint64_t &now) {
  submitRequest(req);
  tick(now);
}

} } // namespace
