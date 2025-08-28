#pragma once
#ifndef __HIL_SCHEDULER_CREDIT_SCHEDULER__
#define __HIL_SCHEDULER_CREDIT_SCHEDULER__

#include "hil/scheduler/scheduler.hh"
#include "sim/simulator.hh"      // SimpleSSD 的事件抽象（allocate/schedule/...）
#include <unordered_map>
#include <queue>
#include <vector>
#include <cstdint>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "sim/core.hh"
#pragma GCC diagnostic pop

namespace SimpleSSD {
namespace HIL {

using Tick = uint64_t;

class CreditScheduler : public Scheduler
{
  public:
    // period_ticks：事件週期（tick）
    // ticks_per_sec：每秒 tick 數（用來把「每秒頁數」換成「每 tick 的配額」）
    explicit CreditScheduler(ICL::ICL* icl, uint64_t period_ticks, uint64_t ticks_per_sec);
    ~CreditScheduler();

    // 對外唯一 API
    void submitRequest(Request& req) override;     // HIL 呼叫
    void processEvent(uint64_t now);               // 週期事件回呼（由建構子排好）
 
    /* --- 統計介面 --- */
    void getStatList (std::vector<Stats>& list, std::string prefix) override;
    void getStatValues(std::vector<double>& val) override;
    void resetStatValues() override;

  private:
    // ------------------------------------------------------------
    // 基本型別
    struct UserAccount {
        uint64_t   weight         = 1;
        uint64_t   creditCap      = 0;        // page
        uint64_t   credit         = 0;        // 可用 token（page）
        double     carry          = 0.0;      // 小數餘量累積
        uint64_t   totalConsumed  = 0;
        uint64_t   consumedHost   = 0;        // ★ 新增：host 類 I/O 消耗
        uint64_t   consumedISC    = 0;        // ★ 新增：ISC 類 I/O 消耗
        bool       isActive       = false;
        uint64_t   lastRefillTick = 0;        // 上次補充時間（tick）
        uint32_t   idlePeriods    = 0;
        std::queue<Request> queue;
        std::queue<Request> queueISC; 
    };

    static constexpr uint32_t IdleGracePeriods = 8; // 0 = 關閉寬限
    
    // ------------------------------------------------------------
    // 常數
    static constexpr uint64_t PageSz      = 4096;
    static constexpr uint64_t SsdIops     = 80000;   // 根據實測調整
    static constexpr uint64_t PagesPerSec = SsdIops; // 4K read

    // ------------------------------------------------------------
    // 成員
    ICL::ICL* pICL = nullptr;

    std::queue<Request> adminQueue;
    std::unordered_map<uint32_t, UserAccount> users;

    uint32_t        lastChosenUid = 0;
    uint32_t        lastChosenUidISC  = 0; 
    bool            inTick        = false;
    bool            timerStarted  = false;

    // 週期事件（走 SimpleSSD 的模擬器抽象）
    SimpleSSD::Event refillEvent   = 0;
    uint64_t         periodTicks_  = 0;   // 事件週期（tick）
    uint64_t         ticksPerSec_  = 0;   // 每秒 tick 數

    // 發佈到 stats 的「固定 user 列表」（避免初始化時沒有 user 導致無法對齊）
    std::vector<uint32_t> statUsers_;     // 預設 {1001, 1002}
    
    // throughput-based 批量發放新增成員
    double           pagesPerPeriod_ = 0.0;  // 每週期應發放的總 pages
    uint64_t         totalWeight_    = 0;    // 所有用戶權重總和

    // —— 新增：全域上次補發時間（節流，只在到期補發）——
    uint64_t         lastGlobalRefillTick_ = 0;

    // ------------------------------------------------------------
    // 核心流程
    void tick(Tick &now);                 // 補 token + RR 發 I/O
    void dispatchICL(const Request& req, Tick &tickNow);

    // 工具
    UserAccount& getOrCreateUser(uint32_t uid);


    // ---- 外部查詢/扣款 API （FTL/Namespace 會呼叫）----
    bool pendingForUser(uint32_t uid) const override;
    bool checkCredit(uint32_t uid, size_t need) const override;
    void useCredit(uint32_t uid, size_t used) override;
    void useCreditISC(uint32_t uid, size_t used) override;
    // （選用）小工具：若你要在別處查詢/扣款
    void     chargeUserCredit(uint32_t uid, uint64_t pages);
    uint64_t getUserCredit(uint32_t uid) const;
    uint64_t getUserWeight(uint32_t uid) const;
    
};

}  // namespace HIL
}  // namespace SimpleSSD

#endif  // __HIL_SCHEDULER_CREDIT_SCHEDULER__
