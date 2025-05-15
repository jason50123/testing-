#include "hil/scheduler/credit_scheduler.hh"
#include "util/def.hh"
#include "util/algorithm.hh"

namespace SimpleSSD {
namespace HIL {

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096ULL   // 確保有 PAGE_SIZE 定義
#endif

/*------------------------------------------------------------*
 |  ctor / dtor                                                |
 *------------------------------------------------------------*/
CreditScheduler::CreditScheduler(uint64_t creditPerRoundPages,
                                 uint64_t intervalTicks,
                                 uint64_t initialCredit)
    : creditPerRound(creditPerRoundPages),
      roundInterval(intervalTicks),
      nextRefillTick(0),
      initialUserCredit(initialCredit) {
  debugprint(LOG_HIL,
             "CreditScheduler | init creditPerRound=%lu pages interval=%lu initialCredit=%lu",
             creditPerRound, roundInterval, initialCredit);
}

CreditScheduler::~CreditScheduler() {
  debugprint(LOG_HIL, "CreditScheduler | destroyed");
}

/*------------------------------------------------------------*
 |  private helpers                                            |
 *------------------------------------------------------------*/
void CreditScheduler::refillCredits() {
  for (auto &kv : users) {
    auto &user = kv.second;
    uint64_t ticksSinceLastRefill = currentTick - user.lastRefillTick;
    uint64_t refillRounds = ticksSinceLastRefill / roundInterval;
    
    if (refillRounds > 0) {
      user.credit += creditPerRound * refillRounds;
      user.lastRefillTick = currentTick;
      debugprint(LOG_HIL, 
                 "CreditScheduler | refill +%lu pages to user %u (total=%lu)",
                 creditPerRound * refillRounds, kv.first, user.credit);
    }
  }
}

void CreditScheduler::drainPending() {
  size_t moved = 0;
  size_t sz = pendingQueue.size();
  while (sz--) {
    Request req = pendingQueue.front();
    pendingQueue.pop();

    uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
    auto &u = user(req.userID);
    if (u.credit >= pages) {
      u.credit -= pages;
      u.consumed += pages;
      requestQueue.push(req);
      ++moved;
    } else {
      // 還是不夠，放回隊尾
      pendingQueue.push(req);
    }
  }
  if (moved)
    debugprint(LOG_HIL, "CreditScheduler | moved %zu requests from pending to ready",
               moved);
}

/*------------------------------------------------------------*
 |  public API overrides                                       |
 *------------------------------------------------------------*/
void CreditScheduler::submitRequest(Request &req) {
  uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
  auto &u = user(req.userID);

  debugprint(LOG_HIL,
             "CreditScheduler | submit req %lu uid=%u pages=%lu credit=%lu",
             req.reqID, req.userID, pages, u.credit);

  if (u.credit >= pages) {
    u.credit -= pages;
    u.consumed += pages;
    requestQueue.push(req);
  } else {
    pendingQueue.push(req);
    debugprint(LOG_HIL,
               "CreditScheduler | uid=%u insufficient credit (%lu < %lu) -> pending",
               req.userID, u.credit, pages);
  }
}

void CreditScheduler::schedule() {
  if (requestQueue.empty()) return;

  Request req = requestQueue.front();
  requestQueue.pop();

  debugprint(LOG_HIL,
             "CreditScheduler | dispatch req %lu uid=%u len=%lu remaining ready=%zu",
             req.reqID, req.userID, req.length, requestQueue.size());
  // 真正送到底層的動作應由 HIL::HIL 端完成；
  // 這裡僅示範 FCFS 式 pop。
}

void CreditScheduler::tick(uint64_t now) {
  currentTick = now;
  if (now >= nextRefillTick) {
    refillCredits();
    nextRefillTick = now + roundInterval;
    drainPending();
  }
  schedule();
}

/*------------------------------------------------------------*
 |  stats                                                      |
 *------------------------------------------------------------*/
void CreditScheduler::getStatList(std::vector<Stats> &list,
                                  std::string prefix) {
  Stats t;
  t.name = prefix + "credit.consumed";
  t.desc = "Total credit consumed (pages)";
  list.push_back(t);

  t.name = prefix + "credit.pending";
  t.desc = "Pending queue length";
  list.push_back(t);

  t.name = prefix + "credit.ready";
  t.desc = "Ready queue length";
  list.push_back(t);
}

void CreditScheduler::getStatValues(std::vector<double> &val) {
  uint64_t consumed = 0;
  for (auto &kv : users) consumed += kv.second.consumed;
  val.push_back(consumed);
  val.push_back(pendingQueue.size());
  val.push_back(requestQueue.size());
}

void CreditScheduler::resetStatValues() {
  for (auto &kv : users) kv.second = {};
  // 不清 users 本身，保留帳戶關係
  while (!pendingQueue.empty()) pendingQueue.pop();
  while (!requestQueue.empty()) requestQueue.pop();
}

bool CreditScheduler::pendingForUser(uint32_t uid) const {
    std::queue<Request> q = pendingQueue;
    while (!q.empty()) {
        if (q.front().userID == uid) return true;
        q.pop();
    }
    return false;
}

}  // namespace HIL
}  // namespace SimpleSSD
