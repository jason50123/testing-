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
    statUsers_.push_back(1003); // 新增 best-effort 使用者
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
            if (req.op == OpType::CREDIT_ONLY) { acc.pendingGates += 1; }
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
            acc.credit         = 0;  // 不給初始credit，確保權重分配公平性
            acc.idlePeriods    = 0;
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

// ───────── tryDispatchWithCredit ─────────
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
                   "defer: uid=%u needs %" PRIu64 " pages, only has %" PRIu64 " weight=%" PRIu64 " cap=%" PRIu64 " consumed=%" PRIu64,
                   req.userID, pages, acc.credit, acc.weight, acc.creditCap, acc.totalConsumed);
        return false;
    }

    acc.credit        -= pages;
    acc.totalConsumed += pages;
    // READ/WRITE 視為 HOST 類，其它保留給 ISC 類（若有）
    if (req.op == OpType::READ || req.op == OpType::WRITE)
        acc.consumedHost += pages;
    else
        acc.consumedISC  += pages;

    return true;
}

// ------------ processEvent --------------------------------------------------
void CreditScheduler::processEvent(uint64_t now)
{
    debugprint(LOG_HIL_CREDIT_SCHEDULER, "timer: now=%" PRIu64, now);
    tick(now);
    SimpleSSD::schedule(refillEvent, lastGlobalRefillTick_);
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

    // (1) 固定相位補發：逐期補齊所有到期的 period（SLO 先分、剩餘給 BE）
    while (timerStarted && now >= lastGlobalRefillTick_) {
        uint64_t activeWAll = 0, activeWSLO = 0, activeWBE = 0;
        // Idle 寬限期：queue 暫空但尚在寬限內仍計入分母；超過寬限則暫停發放
        for (auto &kv : users) {
            auto &acc = kv.second;
            bool emptyQ = acc.queue.empty() && acc.queueISC.empty();
            if (acc.isActive) {
                if (emptyQ) {
                    if (acc.idlePeriods < UINT32_MAX) acc.idlePeriods++;
                } else {
                    acc.idlePeriods = 0;
                }
            }
            if (acc.isActive) {
                activeWAll += acc.weight;
                if (acc.isSLO) activeWSLO += acc.weight; else activeWBE += acc.weight;
            }
        }

        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "refill-phase: t=%" PRIu64 " activeW(all=%" PRIu64 ", slo=%" PRIu64 ", be=%" PRIu64 ")",
                   lastGlobalRefillTick_, activeWAll, activeWSLO, activeWBE);

        // 相位內彙總（便於 CREDSTAT）：只針對常見 UID 1001/1002（若不存在則為 0）
        uint64_t add1001 = 0, add1002 = 0;

        // SLO 先分配：若 SLO 有活躍者，整個 phase 的配額都屬於 SLO。
        // 僅因 SLO 用戶達 cap 無法發放的部分才溢出給 BE；四捨五入的小數留在 SLO 的 carry，不外溢。
        double phaseBudget = pagesPerPeriod_;
        double spillToBE = 0.0;

        if (activeWSLO > 0) {
            for (auto &kv : users) {
                auto &acc = kv.second;
                if (!acc.isActive || !acc.isSLO) continue;

                double exact   = phaseBudget * (static_cast<double>(acc.weight) / static_cast<double>(activeWSLO));
                double sum     = exact + acc.carry;
                uint64_t target   = static_cast<uint64_t>(sum);   // 本期欲發放的整數頁
                uint64_t headroom = (acc.credit >= acc.creditCap) ? 0ULL : (acc.creditCap - acc.credit);
                uint64_t add      = (target > headroom) ? headroom : target;

                if (headroom < target) {
                    spillToBE += (sum - static_cast<double>(add));
                    acc.carry  = 0.0;  // cap 狀態不可累積小數
                } else {
                    acc.carry  = sum - static_cast<double>(add);
                }

                acc.credit += add;
                acc.lastRefillTick = lastGlobalRefillTick_;

                if (add) {
                    debugprint(LOG_HIL_CREDIT_SCHEDULER,
                               "refill-SLO: uid=%u add=%" PRIu64 " carry=%.4f credit=%" PRIu64 " cap=%" PRIu64 " w=%" PRIu64,
                               (unsigned)kv.first, add, acc.carry, acc.credit, acc.creditCap, acc.weight);
                    if ((unsigned)kv.first == 1001) add1001 += add;
                    if ((unsigned)kv.first == 1002) add1002 += add;
                }
            }
        } else {
            // 沒有 SLO 活躍者：全部配額可用於 BE
            spillToBE = phaseBudget;
        }

        // 將 SLO cap 溢出的部分給 BE（work-conserving）
        if (spillToBE > 0.0 && activeWBE > 0) {
            for (auto &kv : users) {
                auto &acc = kv.second;
                if (!acc.isActive || acc.isSLO) continue;

                double exact   = spillToBE * (static_cast<double>(acc.weight) / static_cast<double>(activeWBE));
                double sum     = exact + acc.carry;
                uint64_t target   = static_cast<uint64_t>(sum);
                uint64_t headroom = (acc.credit >= acc.creditCap) ? 0ULL : (acc.creditCap - acc.credit);
                uint64_t add      = (target > headroom) ? headroom : target;
                acc.carry  = sum - static_cast<double>(add);

                acc.credit += add;
                acc.lastRefillTick = lastGlobalRefillTick_;

                if (add) {
                    debugprint(LOG_HIL_CREDIT_SCHEDULER,
                               "refill-BE: uid=%u add=%" PRIu64 " carry=%.4f credit=%" PRIu64 " cap=%" PRIu64 " w=%" PRIu64,
                               (unsigned)kv.first, add, acc.carry, acc.credit, acc.creditCap, acc.weight);
                }
            }
        }

        // 低噪音彙總：關心 1001/1002 的相位配額與目前 credit（若不存在則 0）
        uint64_t c1 = 0, c2 = 0, w1 = 0, w2 = 0;
        { auto it = users.find(1001); if (it != users.end()) { c1 = it->second.credit; w1 = it->second.weight; } }
        { auto it = users.find(1002); if (it != users.end()) { c2 = it->second.credit; w2 = it->second.weight; } }
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "CREDSTAT: phase=%" PRIu64 " activeW=%" PRIu64 " pagesPerPeriod=%.2f add{1001}=%" PRIu64 " add{1002}=%" PRIu64
                   " credit{1001}=%" PRIu64 " credit{1002}=%" PRIu64
                   " expectedShare{1001}=%.3f expectedShare{1002}=%.3f",
                   lastGlobalRefillTick_, activeWAll, pagesPerPeriod_, add1001, add1002, c1, c2,
                   activeWAll > 0 ? static_cast<double>(w1) / activeWAll : 0.0,
                   activeWAll > 0 ? static_cast<double>(w2) / activeWAll : 0.0);

        // 推進到下一個固定相位
        lastGlobalRefillTick_ += periodTicks_;
    }
    // (2) 先處理「一般延遲佇列」（deferredQueue）：credit 夠就扣、派發到 ICL
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
                // 允許 re-entrant
                inTick = false;
                dispatchICL(dr.req, now);
                inTick = true;
                debugprint(LOG_HIL_CREDIT_SCHEDULER,
                           "resume[Q]: uid=%u after %" PRIu64 " ticks (pages=%" PRIu64 ") credit-left=%" PRIu64 " weight=%" PRIu64 " consumed=%" PRIu64,
                           dr.req.userID, now - dr.deferTime, dr.pages, acc.credit, acc.weight, acc.totalConsumed);
            } else {
                deferredQueue.push(dr);
            }
        }
    }
 
    // (2) 先處理「自訂 ISC 延遲回呼」：credit 夠就扣、呼叫上層 resume()
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
                debugprint(LOG_HIL_CREDIT_SCHEDULER,
                           "ISC-resume: uid=%u after %" PRIu64 " ticks (pages=%" PRIu64 ", credit-left=%" PRIu64 ")",
                           dc.uid, now - dc.deferTime, dc.pages, acc.credit);
                if (dc.resume) {
                    // 避免回呼內部又觸發 tick() 造成 re-entrant 卡住
                    inTick = false;
                    dc.resume(dc.ctx, now);
                    inTick = true;
                }
            } else {
                deferredISC_.push(dc);
            }
        }
    }
    
    // (2.5) 處理延遲的 ICL 請求 - 已移除，改為FTL層直接執行
    
    // (3) 真正 RR 交錯派發：每輪最多派發 1 筆，再從 lastChosenUid 的下一位重啟
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
                    Request front = Q.front();
                    if (!isc) {
                        // Host 類請求：用 helper 以 credit 派發；不足時直接移入延遲佇列
                        if (tryDispatchWithCredit(front, now)) {
                            last = it->first;
                            Q.pop();
                            inTick = false;
                            dispatchICL(front, now);
                            inTick = true;
                            debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                       "dispatch[HOST]: uid=%u pages=%" PRIu64 " credit-left=%" PRIu64
                                       " totalConsumed=%" PRIu64 " weight=%" PRIu64 " Qsize=%zu cap=%" PRIu64,
                                       (unsigned)last, (front.length + PageSz - 1) / PageSz, acc.credit, acc.totalConsumed, acc.weight, Q.size(), acc.creditCap);
                            return true;
                        } else {
                            // 已放入 deferredQueue，不阻塞 RR
                            Q.pop();
                            debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                       "deferred[HOST]: uid=%u pages=%" PRIu64 " credit=%" PRIu64 " weight=%" PRIu64 " queued; Qsize=%zu",
                                       (unsigned)it->first, (front.length + PageSz - 1) / PageSz, acc.credit, acc.weight, Q.size());
                        }
                    } else {
                        // 保留原 ISC 分支（若仍有 gate 類工作流）
                        uint64_t need = (front.length + PageSz - 1) / PageSz;
                        if (acc.credit >= need) {
                            acc.credit        -= need;
                            acc.totalConsumed += need;
                            acc.consumedISC   += need;
                            last = it->first;
                            Q.pop();
                            debugprint(LOG_HIL_CREDIT_SCHEDULER,
                                       "dispatch[ISC]: uid=%u need=%" PRIu64 " credit-left=%" PRIu64
                                       " totalConsumed=%" PRIu64 " Qsize=%zu",
                                       (unsigned)last, need, acc.credit, acc.totalConsumed, Q.size());
                            return true;
                        } else {
                            // 若需要，也可改為移入延遲佇列；這裡一併統一
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
    acc.weight        = (uid == 1002 ? 7 : uid == 1001 ? 3 : 1); // 範例：依 UID 給不同權重
    acc.isSLO         = (uid == 1001 || uid == 1002);            // 1001/1002 視為 SLO，其他（如 1003）為 BE
    acc.creditCap     = static_cast<uint64_t>(pagesPerPeriod_ * 500); // 500 periods 緩衝，避免cap限制
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
    if (it == users.end()) {
        // Bootstrap: first sight of a user -> allow first request to flow into submitRequest()
        // submitRequest() will create the account and start the timer/phase.
        debugprint(LOG_HIL_CREDIT_SCHEDULER, "checkCredit: uid=%u NOT FOUND -> ALLOW bootstrap", uid);
        return true;
    }
    
    const auto &acc = it->second;
    
    // 如果有足夠credit，允許
    if (acc.credit >= need) {
        debugprint(LOG_HIL_CREDIT_SCHEDULER, "checkCredit: uid=%u ALLOW (credit=%" PRIu64 " >= need=%zu)", 
                   uid, acc.credit, need);
        return true;
    }
    
    // 對於已初始化但還沒refill過的用戶（creditCap>0但credit=0），
    // 允許第一個請求以啟動refill機制
    if (acc.credit == 0 && acc.creditCap > 0 && acc.totalConsumed == 0) {
        debugprint(LOG_HIL_CREDIT_SCHEDULER, "checkCredit: uid=%u ALLOW first request (credit=0, cap=%" PRIu64 ", consumed=%" PRIu64 ")", 
                   uid, acc.creditCap, acc.totalConsumed);
        return true;  // 允許第一個請求觸發refill
    }
    
    debugprint(LOG_HIL_CREDIT_SCHEDULER, "checkCredit: uid=%u REJECT (credit=%" PRIu64 " < need=%zu, consumed=%" PRIu64 ")", 
               uid, acc.credit, need, acc.totalConsumed);
    return false;
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
// ------------ processUntil - 同步處理直到完成 --------------------------------
void CreditScheduler::processUntil(Request& req, uint64_t& completionTick) {
    debugprint(LOG_HIL_CREDIT_SCHEDULER,
               "processUntil: uid=%u op=%d len=%" PRIu64 " at=%" PRIu64,
               req.userID, int(req.op), req.length, completionTick);

    // 提交到 scheduler
    submitRequest(req);
    
    // 持續 tick 直到這個 request 被處理完
    uint64_t maxIterations = 1000000; // 防止無限迴圈
    uint64_t iterations = 0;
    
    while (iterations++ < maxIterations) {
        tick(completionTick);
        
        // 檢查是否還有 pending requests for this user
        bool hasPending = false;
        auto it = users.find(req.userID);
        if (it != users.end()) {
            hasPending = !it->second.queue.empty() || !it->second.queueISC.empty();
        }
        hasPending = hasPending || !deferredQueue.empty() || !deferredISC_.empty();
        
        if (!hasPending) {
            debugprint(LOG_HIL_CREDIT_SCHEDULER,
                       "processUntil: completed after %" PRIu64 " iterations", iterations);
            break;
        }
        
        // 推進時間一點點，讓 credit refill 有機會發生
        completionTick += periodTicks_ / 10;
    }
    
    if (iterations >= maxIterations) {
        debugprint(LOG_HIL_CREDIT_SCHEDULER,
                   "processUntil: WARNING - max iterations reached");
    }
}

void CreditScheduler::chargeUserCredit(uint32_t uid, uint64_t pages) { useCredit(uid, pages); }
uint64_t CreditScheduler::getUserCredit(uint32_t uid) const {
    auto it = users.find(uid);
    return (it == users.end()) ? 0 : it->second.credit;
}
uint64_t CreditScheduler::getUserWeight(uint32_t uid) const {
    auto it = users.find(uid);
    return (it == users.end()) ? 1 : it->second.weight;
}

// ============================================================================
// ICL 延遲執行支持方法
// ============================================================================

uint64_t CreditScheduler::predictICLLatency(const ICL::Request& req, uint64_t) {
    // 基於 ICL 請求預測延遲時間
    // 簡單估算：每個邏輯頁約需 100 ticks 的處理時間
    uint64_t baseLatency = 100;  // 基礎延遲
    uint64_t pagesLatency = req.range.nlp * baseLatency;  // 基於頁數的延遲
    
    // debugprint removed to prevent log explosion
    
    return pagesLatency;
}

void CreditScheduler::submitICLDeferred(const ICL::Request& req, uint32_t uid, 
                                      uint64_t tick, uint64_t latency, uint64_t pages) {
    DeferredICLRequest deferredReq;
    deferredReq.iclReq = req;
    deferredReq.uid = uid;
    deferredReq.tick = tick;
    deferredReq.predictedLatency = latency;
    deferredReq.pages = pages;
    deferredReq.deferTime = SimpleSSD::getTick();
    
    deferredICL_.push(deferredReq);
    
    // debugprint removed to prevent log explosion
}

void CreditScheduler::processDeferredICL(uint64_t) {
    // 處理延遲的 ICL 請求：檢查是否有足夠 credit 可以執行
    std::queue<DeferredICLRequest> remainingRequests;
    
    while (!deferredICL_.empty()) {
        DeferredICLRequest& deferredReq = deferredICL_.front();
        deferredICL_.pop();
        
        // 檢查用戶是否有足夠的 credit
        if (checkCredit(deferredReq.uid, deferredReq.pages)) {
            // 有足夠 credit，立即執行 ICL 請求
            useCreditISC(deferredReq.uid, deferredReq.pages);
            
            debugprint(LOG_HIL_CREDIT_SCHEDULER,
                       "processDeferredICL: EXECUTING uid=%u, pages=%" PRIu64 ", tick=%" PRIu64,
                       deferredReq.uid, deferredReq.pages, deferredReq.tick);
            
            // 執行實際的 ICL 操作
            uint64_t executionTick = deferredReq.tick;
            pICL->read(deferredReq.iclReq, executionTick);
            
        } else {
            // 仍無足夠 credit，重新放回佇列
            remainingRequests.push(deferredReq);
            // debugprint removed to prevent log explosion
        }
    }
    
    // 將仍無法執行的請求放回佇列
    deferredICL_ = std::move(remainingRequests);
}



}} // namespace SimpleSSD::HIL
