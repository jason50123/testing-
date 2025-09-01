#pragma once
#ifndef __HIL_SCHEDULER_CREDIT_SCHEDULER__
#define __HIL_SCHEDULER_CREDIT_SCHEDULER__

#include "hil/scheduler/scheduler.hh"
#include "sim/simulator.hh"      // SimpleSSD 的事件抽象（allocate/schedule/...）
#include <unordered_map>
#include <queue>
#include <vector>
#include <cstdint>
#include <cstddef>

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

    // ★ 覆寫 base 介面（by-ref tick + 同步處理）
    void tick(uint64_t &now) override;
    void processUntil(Request &req, uint64_t &now) override;
    
    /* --- 統計介面 --- */
    void getStatList (std::vector<Stats>& list, std::string prefix) override;
    void getStatValues(std::vector<double>& val) override;
    void resetStatValues() override;

    // ---- 供上層（例如 NVMe::Namespace）登記「等 credit 再執行」的 ISC 工作 ----
    //   - uid:   使用者
    //   - pages: 需要扣的頁數
    //   - ctx:   上層回呼要用的 context（例如 IOContext* 包在你自己的小結構內）
    //   - cb:    扣到 credit 後要呼叫的函式；原型：void (*cb)(void* ctx, uint64_t now)
    // 注意：Scheduler 只會幫你「等到夠、扣款、呼叫 cb」，不認 context 型別
    void submitISCDeferred(uint32_t uid, uint64_t pages, void* ctx,
                           void (*cb)(void* /*ctx*/, uint64_t /*now*/));


    // 新增：嘗試以使用者 credit 派發一筆請求；若不足，將請求移至延遲佇列。
    // 回傳 true 表示已完成扣款、可立刻派發；false 表示已被延遲。
    bool tryDispatchWithCredit(Request& req, Tick& now);

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
        uint64_t pendingGates = 0; 
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


    // ---- 一般請求的延遲佇列（Host/ISC 的 Request 直接排入等待扣款）----
    struct DeferredRequest {
        Request   req;
        uint64_t  pages;
        uint64_t  deferTime;
    };
    std::queue<DeferredRequest> deferredQueue;


    // ---- 自訂 ISC 延遲工作（回呼）----
    struct DeferredCustom {
        uint32_t uid = 0;
        uint64_t pages = 0;
        uint64_t deferTime = 0;
        void*    ctx = nullptr;
        void   (*resume)(void*, uint64_t) = nullptr;
    };
    std::queue<DeferredCustom> deferredISC_;


    // ★ 新增：完成查詢表（reqID -> finishedTick）
    std::unordered_map<uint64_t, uint64_t> completedAt_;

    // ------------------------------------------------------------
    // 核心流程
    void tickImpl(Tick &now);                // 私有版：實做補 token + RR + I/O
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
