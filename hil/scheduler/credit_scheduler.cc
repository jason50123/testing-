// hil/scheduler/credit_scheduler.cc
#include "hil/scheduler/credit_scheduler.hh"
#include "icl/icl.hh"
#include "sim/simulator.hh"
#include "sim/trace.hh"
#include <algorithm>
#include <inttypes.h>

namespace SimpleSSD { namespace HIL {

#define PR_SECTION LOG_HIL_CREDIT_SCHEDULER

// ------------ 建構 / 解構 ----------------------------------------------------
CreditScheduler::CreditScheduler(ICL::ICL* icl, uint64_t period_ticks, uint64_t ticks_per_sec)
    : pICL(icl), periodTicks_(period_ticks), ticksPerSec_(ticks_per_sec)
{
    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "ctor: periodTicks=%" PRIu64 ", ticksPerSec=%" PRIu64,
               periodTicks_, ticksPerSec_);

    pagesPerPeriod_ =
        static_cast<double>(PagesPerSec) *
        static_cast<double>(periodTicks_) /
        static_cast<double>(ticksPerSec_);
    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "pagesPerPeriod = %.3f pages / %" PRIu64 " ticks",
               pagesPerPeriod_, periodTicks_);

    statUsers_.clear();
    statUsers_.push_back(1001);
    statUsers_.push_back(1002);
    for (uint32_t uid : statUsers_) {
        auto &acc = getOrCreateUser(uid);
        acc.isActive       = false;
        acc.credit         = 0;
        acc.lastRefillTick = SimpleSSD::getTick();
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "init user uid=%u weight=%" PRIu64 " cap=%" PRIu64 " credit=%" PRIu64,
                   uid, acc.weight, acc.creditCap, acc.credit);
    }

    refillEvent = SimpleSSD::allocate([this](uint64_t now){
        this->processEvent(now);
    });
    timerStarted = false;
    lastGlobalRefillTick_ = 0;

    debugprint(LOG_HIL_CREDIT_SCHEDULER, "refillEvent allocated, idle");
}

CreditScheduler::~CreditScheduler() {
    if (SimpleSSD::scheduled(refillEvent, nullptr)) {
        SimpleSSD::deschedule(refillEvent);
        debugprint(LOG_HIL_CREDIT_SCHEDULER, "dtor: deschedule timer");
    }
    SimpleSSD::deallocate(refillEvent);
    debugprint(LOG_HIL_CREDIT_SCHEDULER, "dtor: deallocate timer");
}

// ------------ submitRequest -------------------------------------------------
void CreditScheduler::submitRequest(Request& req)
{
    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "submit: uid=%u op=%d len=%" PRIu64 " now=%" PRIu64,
               req.userID, int(req.op), (uint64_t)req.length, SimpleSSD::getTick());

    if (req.userID == 0) {               // admin
        adminQueue.push(req);
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "submit: -> adminQ size=%zu", adminQueue.size());
        return;
    }

    auto& acc = getOrCreateUser(req.userID);
    const bool isGate = (req.op == OpType::CREDIT_ONLY || req.op == OpType::ISC_RESULT);

    if (isGate) {
        if (req.op == OpType::CREDIT_ONLY) { acc.pendingGates += 1; }
        std::queue<Request> newQ;
        newQ.push(req);
        while (!acc.queueISC.empty()) { newQ.push(acc.queueISC.front()); acc.queueISC.pop(); }
        acc.queueISC.swap(newQ);
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "submit: -> user[%u].ISC size=%zu", req.userID, acc.queueISC.size());
    } else {
        acc.queue.push(req);
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "submit: -> user[%u] Q size=%zu, credit=%" PRIu64,
                   req.userID, acc.queue.size(), acc.credit);
    }

    if (!acc.isActive) {
        acc.isActive       = true;
        acc.credit         = 0;
        acc.lastRefillTick = SimpleSSD::getTick();
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "submit: user[%u] activated at tick=%" PRIu64,
                   req.userID, acc.lastRefillTick);

        if (!timerStarted) {
            auto first = SimpleSSD::getTick() + periodTicks_;
            lastGlobalRefillTick_ = first; // 下一次補發相位
            SimpleSSD::schedule(refillEvent, first);
            timerStarted = true;
            debugprint(LOG_HIL_CREDIT_SCHEDULER,
                       "timer start: first phase @%" PRIu64, first);
        }
    }
}

// ------------ processEvent --------------------------------------------------
void CreditScheduler::processEvent(uint64_t now)
{
    debugprint(LOG_HIL_CREDIT_SCHEDULER, "timer: now=%" PRIu64, now);
    // 補 token + 嘗試派發（不需要知道 caller 的 now）
    tick(now);  // by-ref，但這裡的 now 是本地變數
    SimpleSSD::schedule(refillEvent, lastGlobalRefillTick_);  // 下一個相位
}

// ---- 外部登記 ISC 延遲（只等信用 -> 扣款 -> 呼叫 resume()）----
void CreditScheduler::submitISCDeferred(uint32_t uid, uint64_t pages, void* ctx,
                                        void (*cb)(void*, uint64_t))
{
    DeferredCustom dc;
    dc.uid       = uid;
    dc.pages     = pages;
    dc.deferTime = SimpleSSD::getTick();
    dc.ctx       = ctx;
    dc.resume    = cb;
    deferredISC_.push(dc);
    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "ISC-defer: uid=%u pages=%" PRIu64 " (enqueued)", uid, pages);
}

// ------------ tryDispatchWithCredit ----------------------------------------
bool CreditScheduler::tryDispatchWithCredit(Request& req, Tick &now)
{
    uint64_t pages = (req.length + PageSz - 1) / PageSz;
    auto &acc = getOrCreateUser(req.userID);

    if (acc.credit < pages) {
        DeferredRequest dr;
        dr.req       = req;
        dr.pages     = pages;
        dr.deferTime = now;
        deferredQueue.push(dr);
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "defer: uid=%u needs %" PRIu64 " pages, only has %" PRIu64,
                   req.userID, pages, acc.credit);
        return false;
    }

    acc.credit        -= pages;
    acc.totalConsumed += pages;
    if (req.op == OpType::READ || req.op == OpType::WRITE)
        acc.consumedHost += pages;
    else
        acc.consumedISC  += pages;

    return true;
}

// ------------ tick()  -------------------------------------------------------
void CreditScheduler::tickImpl(Tick &now)
{
    if (inTick) return;
    inTick = true;

    // (0) 先把 admin queue 派光
    while (!adminQueue.empty()) {
        Request req = adminQueue.front();
        adminQueue.pop();
        dispatchICL(req, now);
    }

    // (1) 固定相位補發：逐期補齊所有到期的 period
    while (timerStarted && now >= lastGlobalRefillTick_) {
        uint64_t activeTotalWeight = 0;
        for (auto &kv : users) {
            auto &acc = kv.second;
            bool emptyQ = acc.queue.empty() && acc.queueISC.empty();
            if (acc.isActive) {
                if (emptyQ) {
                    if (acc.idlePeriods < UINT32_MAX) acc.idlePeriods++;
                    if (IdleGracePeriods > 0 && acc.idlePeriods > IdleGracePeriods) {
                        acc.isActive = false;
                        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                   "deactivate: uid=%u idlePeriods=%u (> %u)",
                                   (unsigned)kv.first, acc.idlePeriods, IdleGracePeriods);
                    }
                } else {
                    acc.idlePeriods = 0;
                }
            }
            if (acc.isActive) activeTotalWeight += acc.weight;
        }

        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "refill-phase: phaseTick=%" PRIu64 " activeW=%" PRIu64,
                   lastGlobalRefillTick_, activeTotalWeight);

        uint64_t add1001 = 0, add1002 = 0;

        if (activeTotalWeight > 0) {
            for (auto &kv : users) {
                auto &acc = kv.second;
                if (!acc.isActive) continue;

                double exact =
                    pagesPerPeriod_ *
                    (static_cast<double>(acc.weight) / static_cast<double>(activeTotalWeight));
                double sum  = exact + acc.carry;
                uint64_t add = static_cast<uint64_t>(sum);
                acc.carry   = sum - static_cast<double>(add);

                uint64_t before = acc.credit;
                acc.credit = std::min(acc.credit + add, acc.creditCap);
                acc.lastRefillTick = lastGlobalRefillTick_;

                if (add) {
                    debugprint(LOG_HIL_CREDIT_SCHEDULER,
                               "refill: uid=%u add=%" PRIu64 " carry=%.4f credit=%" PRIu64 " cap=%" PRIu64,
                               (unsigned)kv.first, add, acc.carry, acc.credit, acc.creditCap);
                    if (before + add > acc.creditCap) {
                        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                   "refill-cap: uid=%u cap=%" PRIu64 " before=%" PRIu64 " add=%" PRIu64,
                                   (unsigned)kv.first, acc.creditCap, before, add);
                    }
                    if ((unsigned)kv.first == 1001) add1001 += add;
                    if ((unsigned)kv.first == 1002) add1002 += add;
                }
            }
        }

        uint64_t c1 = 0, c2 = 0;
        { auto it = users.find(1001); if (it != users.end()) c1 = it->second.credit; }
        { auto it = users.find(1002); if (it != users.end()) c2 = it->second.credit; }
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "CREDSTAT: phase=%" PRIu64 " add{1001}=%" PRIu64 " add{1002}=%" PRIu64
                   " credit{1001}=%" PRIu64 " credit{1002}=%" PRIu64,
                   lastGlobalRefillTick_, add1001, add1002, c1, c2);

        lastGlobalRefillTick_ += periodTicks_;
    }

    // (2) 先處理延遲佇列：credit 夠就扣、派發到 ICL
    if (!deferredQueue.empty()) {
        size_t n = deferredQueue.size();
        for (size_t i = 0; i < n; ++i) {
            DeferredRequest dr = deferredQueue.front();
            deferredQueue.pop();
            auto& acc = getOrCreateUser(dr.req.userID);
            if (acc.credit >= dr.pages) {
                acc.credit        -= dr.pages;
                acc.totalConsumed += dr.pages;
                if (dr.req.op == OpType::READ || dr.req.op == OpType::WRITE)
                    acc.consumedHost += dr.pages;
                else
                    acc.consumedISC  += dr.pages;

                dispatchICL(dr.req, now);   // ★ 可能會把 now 推到 I/O 完成時間
            } else {
                deferredQueue.push(dr);
            }
        }
    }

    // (2b) 自訂 ISC 延遲回呼
    if (!deferredISC_.empty()) {
        size_t n = deferredISC_.size();
        for (size_t i = 0; i < n; ++i) {
            DeferredCustom dc = deferredISC_.front();
            deferredISC_.pop();
            auto& acc = getOrCreateUser(dc.uid);
            if (acc.credit >= dc.pages) {
                acc.credit        -= dc.pages;
                acc.totalConsumed += dc.pages;
                acc.consumedISC   += dc.pages;
                if (dc.resume) {
                    // 避免 re-entrant
                    inTick = false;
                    dc.resume(dc.ctx, now);
                    inTick = true;
                }
            } else {
                deferredISC_.push(dc);
            }
        }
    }

    // (3) RR 交錯派發：每輪最多派發若干筆
    if (!users.empty()) {
        const size_t MAX_DISPATCH_PER_TICK = 4096;
        size_t rounds = 0;

        auto try_dispatch_class = [&](bool isc)->bool {
            uint32_t &last = isc ? lastChosenUidISC : lastChosenUid;
            auto it = users.find(last);
            if (it == users.end() || ++it == users.end()) it = users.begin();

            size_t visited = 0;
            while (visited++ < users.size()) {
                if (it == users.end()) it = users.begin();
                UserAccount &acc = it->second;
                auto &Q = isc ? acc.queueISC : acc.queue;
                if (!Q.empty()) {
                    Request front = Q.front();

                    if (!isc) {
                        if (tryDispatchWithCredit(front, now)) {
                            last = it->first;
                            Q.pop();
                            dispatchICL(front, now);
                            return true;
                        } else {
                            Q.pop();
                            debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                       "deferred[HOST]: uid=%u Qsize=%zu",
                                       (unsigned)it->first, Q.size());
                        }
                    } else {
                        uint64_t need = (front.length + PageSz - 1) / PageSz;
                        if (acc.credit >= need) {
                            acc.credit        -= need;
                            acc.totalConsumed += need;
                            acc.consumedISC   += need;
                            last = it->first;
                            Q.pop();
                            dispatchICL(front, now);
                            return true;
                        } else {
                            DeferredRequest dr{front, need, now};
                            deferredQueue.push(dr);
                            Q.pop();
                            debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                       "deferred[ISC]: uid=%u need=%" PRIu64 " credit=%" PRIu64,
                                       (unsigned)it->first, need, acc.credit);
                        }
                    }
                }
                ++it;
            }
            return false;
        };

        for (; rounds < MAX_DISPATCH_PER_TICK; ++rounds) {
            bool dispatched = false;
            // 先 ISC 再 Host（若要 normal-first，調換順序）
            dispatched = try_dispatch_class(/*isc=*/true)
                      || try_dispatch_class(/*isc=*/false);
            if (!dispatched) break;
        }
    }

    inTick = false;
}

// ------------ dispatch 到 ICL ----------------------------------------------
void CreditScheduler::dispatchICL(const Request& req, Tick &t)
{
    Request req_mut = req;     // ICL::Request 需要 mutable
    ICL::Request iclReq(req_mut);

    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "ICL: t=%" PRIu64 " uid=%u op=%d len=%" PRIu64 " reqID=%" PRIu64,
               (uint64_t)t, req.userID, int(req.op), (uint64_t)req.length, req.reqID);

    switch (req.op) {
        case OpType::READ:  pICL->read (iclReq, t); break;
        case OpType::WRITE: pICL->write(iclReq, t); break;
        default:            /* 其它類型略 */       break;
    }

    // ★ 記錄該 req 完成時間，用於 processUntil()
    completedAt_[req.reqID] = t;
}

// ------------ User 管理 ------------------------------------------------------
CreditScheduler::UserAccount&
CreditScheduler::getOrCreateUser(uint32_t uid)
{
    auto it = users.find(uid);
    if (it != users.end()) return it->second;

    UserAccount acc;
    acc.weight        = (uid == 1002 ? 8 : uid == 1001 ? 2 : 1);
    acc.creditCap     = static_cast<uint64_t>(pagesPerPeriod_ * 500);
    if (acc.creditCap < 50) acc.creditCap = 50;

    users.emplace(uid, acc);

    totalWeight_ = 0;
    for (auto it2 = users.begin(); it2 != users.end(); ++it2) {
        totalWeight_ += it2->second.weight;
    }
    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "user created: uid=%u weight=%" PRIu64 " totalWeight=%" PRIu64,
               uid, acc.weight, totalWeight_);

    return users[uid];
}


// ───────── Scheduler 統計介面（per-user credit）─────────
void CreditScheduler::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats s;
  
  // (1) 總消耗
  s.name = prefix + "credit.total.consumed";
  s.desc = "Total credit consumed (pages)";
  list.push_back(s);
  
  // (2) 每 user 消耗（固定順序：constructor 初始化的 statUsers_）
  for (uint32_t uid : statUsers_) {
    s.name = prefix + "credit.user.uid" + std::to_string(uid) + ".consumed";
    s.desc = "Per-user credit consumed (pages)";
    list.push_back(s);
  }
  // (2.5) 類別總體統計（Host / ISC）
  s.name = prefix + "credit.host.total.consumed";
  s.desc = "Total HOST-class credit consumed (pages)";
  list.push_back(s);
  s.name = prefix + "credit.isc.total.consumed";
  s.desc = "Total ISC-class credit consumed (pages)";
  list.push_back(s);

  // (2.6) 每 user 類別消耗（Host / ISC）
  for (uint32_t uid : statUsers_) {
    s.name = prefix + "credit.user.uid" + std::to_string(uid) + ".consumed.host";
    s.desc = "Per-user HOST-class credit consumed (pages)";
    list.push_back(s);
  }
  for (uint32_t uid : statUsers_) {
    s.name = prefix + "credit.user.uid" + std::to_string(uid) + ".consumed.isc";
    s.desc = "Per-user ISC-class credit consumed (pages)";
    list.push_back(s);
  }
  // (3) 每 user 隊列中的請求數量
  for (uint32_t uid : statUsers_) {
    s.name = prefix + "credit.user.uid" + std::to_string(uid) + ".queue_size";
    s.desc = "Per-user pending requests in queue";
    list.push_back(s);
  }
  
  // (4) 總體統計
  s.name = prefix + "credit.pending";
  s.desc = "Total requests awaiting credit";
  list.push_back(s);
  
  s.name = prefix + "credit.ready";
  s.desc = "Total requests ready to dispatch";
  list.push_back(s);
}

void CreditScheduler::getStatValues(std::vector<double>& val) {
    // (1) 總消耗
    uint64_t consumed_total = 0;
    uint64_t consumed_host_total = 0;
    uint64_t consumed_isc_total  = 0;
    for (auto const& kv : users){
        consumed_total      += kv.second.totalConsumed;
        consumed_host_total += kv.second.consumedHost;
        consumed_isc_total  += kv.second.consumedISC;
    }
    val.push_back(static_cast<double>(consumed_total));

    // (2) 每 user 消耗（若該 user 尚未建立，值為 0）
    for (uint32_t uid : statUsers_) {
        auto it = users.find(uid);
        double v = (it == users.end()) ? 0.0 : static_cast<double>(it->second.totalConsumed);
        val.push_back(v);
    }
    // (2.5) 類別總體統計（Host / ISC）
    val.push_back(static_cast<double>(consumed_host_total));
    val.push_back(static_cast<double>(consumed_isc_total));

    // (2.6) 每 user 類別消耗
    for (uint32_t uid : statUsers_) {
        auto it = users.find(uid);
        double hostv = (it == users.end()) ? 0.0 : static_cast<double>(it->second.consumedHost);
        val.push_back(hostv);
    }
    for (uint32_t uid : statUsers_) {
        auto it = users.find(uid);
        double iscv = (it == users.end()) ? 0.0 : static_cast<double>(it->second.consumedISC);
        val.push_back(iscv);
    }
     
    // (3) 每 user 隊列中的請求數量（Host + ISC 總和）
    for (uint32_t uid : statUsers_) {
        auto it = users.find(uid);
        double queueSize = 0.0;
        if (it != users.end()) queueSize = static_cast<double>(it->second.queue.size() + it->second.queueISC.size());
        val.push_back(queueSize);
    }

    // (4) 掃描所有 queue（含 ISC），估算 pending / ready
    uint64_t pending = static_cast<uint64_t>(deferredQueue.size() + deferredISC_.size()); // 延遲請求也算等待中
    uint64_t ready   = adminQueue.size();     // admin I/O 視為 ready
    for (auto const& kv : users) {
        const auto& acc = kv.second;
        if (acc.queue.empty() && acc.queueISC.empty()) continue;
        std::queue<Request> tmpA = acc.queueISC; // 先估 ISC
        std::queue<Request> tmpB = acc.queue;    // 再估 Host
        uint64_t creditLeft = acc.credit;
        auto consume = [&](std::queue<Request>& tmpQ)->bool{
          while (!tmpQ.empty()) {
            const Request& rq = tmpQ.front();
            uint64_t need = (rq.length + PageSz - 1) / PageSz;
            if (creditLeft >= need) {
                ++ready;
                creditLeft -= need;
                tmpQ.pop();
            } else {
                pending += tmpQ.size();   // 剩餘全部視為欠 token
                return false;
            }
          }
          return true;
        };
        if (!consume(tmpA)) continue;
            consume(tmpB);

    }
    val.push_back(static_cast<double>(pending));
    val.push_back(static_cast<double>(ready));

}
void CreditScheduler::resetStatValues() {
    debugprint(LOG_HIL_CREDIT_SCHEDULER, "stats: reset");
    for (auto& kv : users) {
        auto& acc = kv.second;
        acc.credit         = 0;
        acc.carry          = 0.0;
        acc.totalConsumed  = 0;
        acc.consumedHost   = 0;
        acc.consumedISC    = 0;
        acc.isActive       = false;
        acc.lastRefillTick = SimpleSSD::getTick();
        acc.pendingGates  = 0;
        while (!acc.queueISC.empty()) acc.queueISC.pop();
    }
    while (!adminQueue.empty()) adminQueue.pop();
    lastChosenUid = 0;
    // 相位重置：下次啟動時再設定
    lastGlobalRefillTick_ = 0;
}

// ========= 外部查詢/扣款 API =========

bool CreditScheduler::pendingForUser(uint32_t uid) const {
    auto it = users.find(uid);
    if (it == users.end()) return false;
    return it->second.pendingGates != 0;  // 現在：只等「自己」塞的 gate 被吃掉
}

bool CreditScheduler::checkCredit(uint32_t uid, size_t need) const {
    auto it = users.find(uid);
    if (it == users.end()) return false;       // 新用戶尚未拿到 token
    return it->second.credit >= need;
}

void CreditScheduler::useCredit(uint32_t uid, size_t used) {
    auto &acc = const_cast<CreditScheduler*>(this)->getOrCreateUser(uid);
    uint64_t take = (used > acc.credit) ? acc.credit : used;
    acc.credit        -= take;
    acc.totalConsumed += take;
}

void CreditScheduler::useCreditISC(uint32_t uid, size_t used) {
    auto &acc = const_cast<CreditScheduler*>(this)->getOrCreateUser(uid);
    uint64_t take = (used > acc.credit) ? acc.credit : used;
    acc.credit        -= take;
    acc.totalConsumed += take;
    acc.consumedISC   += take;   // ★ 把 FTL 端直接扣款歸到 ISC
    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "charge[ISC]: uid=%u used=%" PRIu64 " credit-left=%" PRIu64
               " consumedISC=%" PRIu64 " totalConsumed=%" PRIu64,
               uid, (uint64_t)used, acc.credit, acc.consumedISC, acc.totalConsumed);
}

// （選用）小工具
void CreditScheduler::chargeUserCredit(uint32_t uid, uint64_t pages) { useCredit(uid, pages); }
uint64_t CreditScheduler::getUserCredit(uint32_t uid) const {
    auto it = users.find(uid);
    return (it == users.end()) ? 0 : it->second.credit;
}
uint64_t CreditScheduler::getUserWeight(uint32_t uid) const {
    auto it = users.find(uid);
    return (it == users.end()) ? 1 : it->second.weight;
}
// ------------ 覆寫：tick(by-ref) 與「阻塞到完成」 ---------------------------
void CreditScheduler::tick(uint64_t &now) {
    Tick tmp = now;
    this->tickImpl(tmp);   // 呼叫私有版（同名多載）
    now = tmp;
}

// ★ 同步到「指定 reqID 完成」
void CreditScheduler::processUntil(Request &req, uint64_t &now) {
    submitRequest(req);
    const uint64_t target = req.reqID;

    // 可能被 RR 交錯、或卡在 credit；所以我們迭代推進時間
    while (true) {
        // 先嘗試在當前 now 進行一次 tick（這會盡力派發 I/O，並把 now 推到最後一筆完成時間）
        tick(now);

        auto it = completedAt_.find(target);
        if (it != completedAt_.end()) {
            // 目標已完成：把完成時間回填給呼叫端
            if (now < it->second) now = it->second;
            completedAt_.erase(it);
            break;
        }

        // 還沒完成：若 token 不足，多半要等到下一個相位；否則至少推進 1 tick 避免忙等
        if (timerStarted && now < lastGlobalRefillTick_) {
            now = lastGlobalRefillTick_;
        } else {
            now += 1;
        }
    }
}

}} // namespace SimpleSSD::HIL
