#include "hil/scheduler/credit_scheduler.hh"
#include "util/def.hh"
#include "util/algorithm.hh"

namespace SimpleSSD {
namespace HIL {

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096ULL
#endif

#define PR_SECTION LOG_HIL_CREDIT_SCHEDULER

CreditScheduler::CreditScheduler(ICL::ICL* iclPtr,
                                 uint64_t creditPerRoundPages,
                                 uint64_t intervalTicks,
                                 uint64_t initialCredit)
    : pICL(iclPtr),
      creditPerRound(creditPerRoundPages),
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

CreditScheduler::UserInfo &
CreditScheduler::user(uint32_t uid)
{
    auto it = users.find(uid);
    if (it == users.end()) {
        UserInfo u;
        u.credit         = initialUserCredit;
        u.initialCredit  = initialUserCredit;
        u.lastRefillTick = currentTick;
        if (uid == 1001) u.weight = 2;
        if (uid == 1002) u.weight = 8;
        return users.emplace(uid, u).first->second;
    }
    return it->second;
}

void CreditScheduler::refillCredits(uint64_t now)
{
    debugprint(LOG_HIL, "refillCredits: now=%lu", now);

    /* ---------------------------------------------------- *
     * 可調參數                                             *
     * ---------------------------------------------------- */
    const uint32_t kRounds   = 20;      // 每 kRounds*roundInterval 才補一次
    const uint64_t baseCred  = creditPerRound;     // 每輪基本 token 數
    const uint64_t maxCredit = 100;    // 上限 (原本就有)

    for (auto &kv : users) {
        auto &user = kv.second;

        /* --------- 1. 判斷是否到補點門檻 ------------------ */
        uint64_t elapsed = now - user.lastRefillTick;
        if (elapsed < roundInterval * kRounds)
            continue;                              // 還沒到下一輪

        /* --------- 2. 權重式 token-bucket ---------------- */
        /* 例：weight = 8 or 2 → 8:2 配額                         */
        uint64_t creditToAdd = baseCred * user.weight;
        user.credit += creditToAdd;

        /* --------- 3. 上限 (cap) ------------------------- */
        if (user.credit > maxCredit)
            user.credit = maxCredit;

        user.lastRefillTick = now;                 // 重設基準

        debugprint(LOG_HIL,
                   "CreditScheduler | refill +%lu pages to user %u (total=%lu)",
                   creditToAdd, kv.first, user.credit);
    }
}


void CreditScheduler::drainPending() {
  debugprint(LOG_HIL, "DrainPending called, pendingQueue size=%zu", pendingQueue.size());
  size_t moved = 0;
  size_t sz = pendingQueue.size();
  for (size_t i = 0; i < sz; ++i) {
    Request req = pendingQueue.front();
    pendingQueue.pop();

    uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
    auto &u = user(req.userID);

    debugprint(LOG_HIL, "drainPending: checking uid=%u pages=%lu credit=%lu", req.userID, pages, u.credit);

    if (u.credit >= pages) {
      u.credit -= pages;
      u.consumed += pages;
      requestQueue.push(req);
      ++moved;
      debugprint(LOG_HIL, "drainPending: moved req %lu uid=%u to ready queue", req.reqID, req.userID);
    } else {
      pendingQueue.push(req);
    }
  }
  if (moved)
    debugprint(LOG_HIL, "CreditScheduler | moved %zu requests from pending to ready", moved);
}

void CreditScheduler::submitRequest(Request &req) {
  uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
  auto &u = user(req.userID);

  debugprint(LOG_HIL,
             "CreditScheduler | submit req %lu uid=%u pages=%lu credit=%lu, pendingQueue=%zu, requestQueue=%zu",
             req.reqID, req.userID, pages, u.credit, pendingQueue.size(), requestQueue.size());

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
  debugprint(LOG_HIL, "schedule called, requestQueue.size() = %zu", requestQueue.size());
  if (requestQueue.empty()) return;

  Request req = requestQueue.front();
  requestQueue.pop();

  currentTick += applyLatency(CPU::CREDIT_SCHEDULER, CPU::SCHEDULE);

  ICL::Request iclReq(req);
  
  switch (req.op) {
        case OpType::READ:
            pICL->read (iclReq, currentTick);
            break;
        case OpType::WRITE:
            pICL->write(iclReq, currentTick);
            break;
        case OpType::CREDIT_ONLY:
            debugprint(LOG_HIL,
                       "Token-only req uid=%u pages=%lu granted at tick=%lu",
                       req.userID,
                       (req.length + PAGE_SIZE - 1) / PAGE_SIZE,
                       currentTick);
            break;
        case OpType::ISC_RESULT:
            /* nothing to send to ICL, 只是純扣點 */
            break;
        default:
            panic("Unknown OpType in scheduler");
    }

    debugprint(LOG_HIL,
               "Dispatch req %lu uid=%u op=%d len=%lu tick=%lu",
               req.reqID, req.userID, static_cast<int>(req.op),
               req.length, currentTick);
} 


void CreditScheduler::tick(uint64_t now)
{
  currentTick = now;

  /* 先試著補 credit，再搬 pending 至 ready */
  refillCredits(now);
  drainPending();

  /* 執行一次 FCFS 排程 */
  schedule();
}

void CreditScheduler::getStatList(std::vector<Stats> &list, std::string prefix) {
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

bool CreditScheduler::checkCredit(uint32_t uid, size_t need) const {
    auto it = users.find(uid);
    if (it == users.end())
        return initialUserCredit >= need;
    return it->second.credit >= need;
}

void CreditScheduler::useCredit(uint32_t uid, size_t used) {
    auto &u = const_cast<CreditScheduler *>(this)->user(uid); // 建立帳戶 (若尚未存在)
    if (u.credit >= used)
        u.credit -= used;
    else
        u.credit = 0;
}

}  // namespace HIL
}  // namespace SimpleSSD
