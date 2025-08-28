// ============================================================================
// credit_scheduler.cc —— Simple token-bucket + RR + admin-queue
// 修正點：
//  A) 固定相位補發：用 lastGlobalRefillTick_ 作為「下一次」補發相位（不改名，改語意）；
//     while(now >= lastGlobalRefillTick_) 逐期補齊，避免“誰先喚醒就拿大鍋”。
//  B) submit 可觸發派發，但補發嚴格依相位進行（不再把相位綁到 submit 時刻）。
//  C) 真正 RR 交錯：每成功派發 1 筆，就從 lastChosenUid 的下一位重啟一輪掃描。
// ============================================================================
#include "hil/scheduler/credit_scheduler.hh"
#include "icl/icl.hh"           // pICL->read/write(...)
#include "sim/simulator.hh"     // allocate/schedule/deschedule/scheduled/getTick
#include "sim/trace.hh"         // debugprint, LOG_HIL_CREDIT_SCHEDULER
#include <algorithm>            // std::min
#include <cstdio>
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

    // 計算每週期應發放的總 pages (throughput-based 批量發放)
    pagesPerPeriod_ =
        static_cast<double>(PagesPerSec) *
        static_cast<double>(periodTicks_) /
        static_cast<double>(ticksPerSec_);
    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "pagesPerPeriod = %.3f pages / %" PRIu64 " ticks", 
               pagesPerPeriod_, periodTicks_);

    // 1) 先固定把 1001、1002 建起來，確保 stats 一開始就有 per-user 欄位
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

    // 配置週期事件（使用 SimpleSSD::Simulator 的事件 API）
    refillEvent = SimpleSSD::allocate([this](uint64_t now){
        this->processEvent(now);
    });

    // 僅 allocate，不立即 schedule
    timerStarted = false;

    // 重要：把 lastGlobalRefillTick_ 改作「下一次應補發的相位」
    // 尚未啟動前不生效，等第一次 start 時設成 now + periodTicks_
    lastGlobalRefillTick_ = 0;

    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "refillEvent allocated, will be scheduled when HIL is ready");
}

CreditScheduler::~CreditScheduler() {
    // 確保事件已取消並釋放
    if (SimpleSSD::scheduled(refillEvent, nullptr)) {
        SimpleSSD::deschedule(refillEvent);
        debugprint(LOG_HIL_CREDIT_SCHEDULER, "dtor: deschedule timer");
    }
    debugprint(LOG_HIL_CREDIT_SCHEDULER, "dtor: deallocate timer");
    SimpleSSD::deallocate(refillEvent);
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
    } else {
        auto& acc = getOrCreateUser(req.userID);
        const bool isGate = (req.op == OpType::CREDIT_ONLY || req.op == OpType::ISC_RESULT);

        if (isGate) {
            // ★ Gate 放到 ISC 佇列；若你想「插到最前端」，用重建 queue 的方式：
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
            
            // 第一個用戶啟動時開始 timer（相位固定為 now + periodTicks_）
            if (!timerStarted) {
                auto first = SimpleSSD::getTick() + periodTicks_;
                lastGlobalRefillTick_ = first; // 注意：此變數語意改為「下一次補發相位」
                debugprint(LOG_HIL_CREDIT_SCHEDULER,
                           "starting timer, first refill phase at tick=%" PRIu64, first);
                SimpleSSD::schedule(refillEvent, first);
                timerStarted = true;
            }
        }
    }

}

// ------------ processEvent --------------------------------------------------
void CreditScheduler::processEvent(uint64_t now)
{
    debugprint(LOG_HIL_CREDIT_SCHEDULER, "timer: now=%" PRIu64, now);
    tick(now);
    SimpleSSD::schedule(refillEvent, lastGlobalRefillTick_);
}

// ------------ tick()  -------------------------------------------------------
void CreditScheduler::tick(Tick &now)
{
    if (inTick) return;
    inTick = true;

    // (0) 先把 admin queue 派光
    size_t admin_dispatched = 0;
    while (!adminQueue.empty()) {
        Request req = adminQueue.front();
        adminQueue.pop();
        dispatchICL(req, now);
        ++admin_dispatched;
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "tick:%" PRIu64 " admin-dispatched=%zu", now, admin_dispatched);
    }

    // (1) 固定相位補發：逐期補齊所有到期的 period
    while (timerStarted && now >= lastGlobalRefillTick_) {
        uint64_t activeTotalWeight = 0;
        // Idle 寬限期：queue 暫空但尚在寬限內仍計入分母
        for (auto &kv : users) {
            const auto &acc = kv.second;
            if (acc.isActive) activeTotalWeight += acc.weight;
        }

        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "refill-phase: phaseTick=%" PRIu64 " activeW=%" PRIu64,
                   lastGlobalRefillTick_, activeTotalWeight);

        // 相位內彙總（便於 CREDSTAT）：只針對常見 UID 1001/1002（若不存在則為 0）
        uint64_t add1001 = 0, add1002 = 0;

        if (activeTotalWeight > 0) {
            for (auto &kv : users) {
                auto &acc = kv.second;
                if (!acc.isActive) continue;

                double exact =
                    pagesPerPeriod_ *
                    (static_cast<double>(acc.weight) / static_cast<double>(activeTotalWeight));
                double sum  = exact + acc.carry;
                uint64_t add = static_cast<uint64_t>(sum);   // 向下取整灌 token
                acc.carry   = sum - static_cast<double>(add);

                uint64_t before = acc.credit;
                acc.credit = std::min(acc.credit + add, acc.creditCap);
                acc.lastRefillTick = lastGlobalRefillTick_;  // 真正補發時刻

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

        // 低噪音彙總：關心 1001/1002 的相位配額與目前 credit（若不存在則 0）
        uint64_t c1 = 0, c2 = 0;
        { auto it = users.find(1001); if (it != users.end()) c1 = it->second.credit; }
        { auto it = users.find(1002); if (it != users.end()) c2 = it->second.credit; }
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "CREDSTAT: phase=%" PRIu64 " activeW=%" PRIu64
                   " add{1001}=%" PRIu64 " add{1002}=%" PRIu64
                   " credit{1001}=%" PRIu64 " credit{1002}=%" PRIu64,
                   lastGlobalRefillTick_, activeTotalWeight, add1001, add1002, c1, c2);
 
        // 推進到下一個固定相位
        lastGlobalRefillTick_ += periodTicks_;
    }

    // (2) 真正 RR 交錯派發：每輪最多派發 1 筆，再從 lastChosenUid 的下一位重啟
    if (!users.empty()) {
        const size_t MAX_DISPATCH_PER_TICK = 4096; // 安全上限，避免無窮迴圈
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
                    const Request& front = Q.front();
                    uint64_t need = (front.length + PageSz - 1) / PageSz;
                    if (acc.credit >= need) {
                        acc.credit        -= need;
                        acc.totalConsumed += need;
                        if (isc) acc.consumedISC += need; else acc.consumedHost += need; // ★ 累加分流
                        last               = it->first;
                        if (!isc) dispatchICL(front, now); // ISC gate 不下 I/O
                        Q.pop();
                        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                   "dispatch[%s]: uid=%u need=%" PRIu64 " credit-left=%" PRIu64
                                   " totalConsumed=%" PRIu64 " Qsize=%zu",
                                   isc ? "ISC" : "HOST",
                                   (unsigned)last, need, acc.credit, acc.totalConsumed, Q.size());
                        return true;
                    } else {
                        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                   "skip[%s]: uid=%u need=%" PRIu64 " credit=%" PRIu64 " (不足)",
                                   isc ? "ISC" : "HOST", (unsigned)it->first, need, acc.credit);
                    }
                }
                ++it;
            }
            return false;
        };


        for (; rounds < MAX_DISPATCH_PER_TICK; ++rounds) {
            bool dispatched = false;
            // ★ 策略位：若要 normal-first，調換呼叫順序即可
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
    // ICL::Request 需要「非 const 參考」：建立可變副本避免 const 轉換錯誤
    Request req_mut = req;
    ICL::Request iclReq(req_mut);

    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "ICL: t=%" PRIu64 " uid=%u op=%d len=%" PRIu64,
               (uint64_t)t, req.userID, int(req.op), (uint64_t)req.length);
           
    switch (req.op) {
        case OpType::READ:  pICL->read (iclReq, t); break;
        case OpType::WRITE: pICL->write(iclReq, t); break;
        default:            /* 管理/ISC 可自行擴充 */  break;
    }
}

// ------------ User 管理 ------------------------------------------------------
CreditScheduler::UserAccount&
CreditScheduler::getOrCreateUser(uint32_t uid)
{
    auto it = users.find(uid);
    if (it != users.end()) return it->second;

    UserAccount acc;
    acc.weight        = (uid == 1002 ? 8 : uid == 1001 ? 2 : 1); // 範例：依 UID 給不同權重
    acc.creditCap     = static_cast<uint64_t>(pagesPerPeriod_ * 500); // 500 periods 緩衝
    if (acc.creditCap < 50) acc.creditCap = 50;

    users.emplace(uid, acc);

    // 重新計算總權重（保留原本語義）
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
    uint64_t pending = 0;
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
    return !it->second.queueISC.empty();  // 只看 ISC gate 佇列
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



}} // namespace SimpleSSD::HIL
