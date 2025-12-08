#pragma once
#ifndef __HIL_SCHEDULER_CREDIT_SCHEDULER__
#define __HIL_SCHEDULER_CREDIT_SCHEDULER__

#include "hil/scheduler/scheduler.hh"
#include "sim/simulator.hh"      // SimpleSSD 的事件抽象（allocate/schedule/...）
#include <unordered_map>
#include <queue>
#include <deque>  // Phase 2: for latSamples
#include <vector>
#include <cstdint>
#include <cstddef>

// Removed gem5 sim/core.hh dependency - using SimpleSSD event system instead

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
    void processUntil(Request& req, uint64_t& completionTick);  // 同步處理直到完成
 
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
    
    // ---- 外部查詢/扣款 API （Controller會呼叫）----
    bool pendingForUser(uint32_t uid) const override;
    bool checkCredit(uint32_t uid, size_t need) const override;
    void useCredit(uint32_t uid, size_t used) override;
    void useCreditISC(uint32_t uid, size_t used) override;
    
    // ★ NEW: ICL Deferred Execution Support
    // These methods support strict credit control for ISC tasks via ICL
    uint64_t predictICLLatency(const ICL::Request& req, uint64_t tick);
    void submitICLDeferred(const ICL::Request& req, uint32_t uid,
                          uint64_t tick, uint64_t latency, uint64_t pages);
    void processDeferredICL(uint64_t now);

    // ★ NEW: Non-blocking ISC Request Queue Support
    // These methods implement the new architecture for ISC credit control
    bool submitISCRequest(uint32_t uid, uint64_t pages, void* iscContext);
    void processISCRequests(uint64_t currentTick);
    void executeISCRequest(void* iscContext, uint64_t executionTick);
    uint64_t predictISCLatency(uint64_t pages, uint64_t waitStartTime);
    
    // Ensure a user is active and the refill timer is started
    void ensureActiveUser(uint32_t uid);
    // Expose scheduler refill period (ticks)
    uint64_t getPeriodTicks() const { return periodTicks_; }
    
    // （選用）小工具：若你要在別處查詢/扣款
    void     chargeUserCredit(uint32_t uid, uint64_t pages);
    uint64_t getUserCredit(uint32_t uid) const;
    uint64_t getUserWeight(uint32_t uid) const;

    // ---- Phase 2: SLO Configuration API ----
    // 为用户设置 IOPS 目标 (4KiB 归一化)
    void setUserIopsTarget(uint32_t uid, double iops);
    // Phase 3: 为用户设置 Tail latency 目标
    void setUserTailTarget(uint32_t uid, double percentile, uint64_t target_us);

  private:
    // ------------------------------------------------------------
    // 基本型別
    struct UserAccount {
        // === 基础字段 (Phase 1) ===
        uint64_t   weight         = 1;
        uint64_t   creditCap      = 0;        // page
        uint64_t   credit         = 0;        // 可用 token（page）
        double     carry          = 0.0;      // 小數餘量累積
        uint64_t   totalConsumed  = 0;
        uint64_t   consumedHost   = 0;        // ★ host 類 I/O 消耗
        uint64_t   consumedISC    = 0;        // ★ ISC 類 I/O 消耗
        bool       isSLO          = false;    // ★ 是否屬於 SLO 組（true）或 BE 組（false）
        bool       isActive       = false;
        uint64_t   lastRefillTick = 0;        // 上次補充時間（tick）
        uint32_t   idlePeriods    = 0;
        uint64_t   pendingGates   = 0;
        std::queue<Request> queue;
        std::queue<Request> queueISC;

        // === Phase 2: SLO 目标与测量字段 ===
        enum class SLOMode {
            NONE,         // 无 SLO (BE 用户)
            IOPS_TARGET,  // IOPS 目标模式
            TAIL_TARGET   // Tail latency 目标模式 (Phase 3)
        };
        SLOMode    sloMode        = SLOMode::NONE;

        // IOPS 目标 (4KiB 归一化)
        double     targetIOPS     = 0.0;      // 例: 18000.0 = 18K IOPS

        // 测量窗口 (每秒统计)
        uint64_t   winStartTick   = 0;        // 窗口起始时间
        uint64_t   winHostPages   = 0;        // 本窗口完成的 Host pages
        uint64_t   winHostIOs     = 0;        // 本窗口完成的 Host I/O 笔数

        // Phase 3: Tail latency 保护 (预留)
        double     tailPercentile = 0.99;     // p99
        uint64_t   tailTargetUs   = 0;        // 微秒, 例: 2000 = 2ms
        double     budgetBoost    = 1.0;      // 临时加码系数 (1.0-3.0)
        std::deque<uint64_t> latSamples;      // 延迟样本 (ticks)
    };

    static constexpr uint32_t IdleGracePeriods = 100; // 增加寬限期以保持用戶在IO間隔期間active
    
    // ------------------------------------------------------------
    // 常數
    static constexpr uint64_t PageSz      = 4096;
    static constexpr uint64_t SsdIops     = 12000;   // 根據實測調整
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

    // Work-conserving with SLO-first 配置（骨架）
    bool             shareIdleCapacity_ = true;   // 是否僅在活躍者間分配（true）
    uint64_t         sloWindowTicks_    = 0;      // 可選：SLO 視窗（未啟用保證時僅佔位）


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

    // ---- ICL 延遲佇列（專門處理 ICL 請求的延遲執行）----
    struct DeferredICLRequest {
        ICL::Request iclReq;
        uint32_t uid = 0;
        uint64_t tick = 0;
        uint64_t predictedLatency = 0;
        uint64_t pages = 0;
        uint64_t deferTime = 0;
    };
    std::queue<DeferredICLRequest> deferredICL_;

    // ---- Non-blocking ISC Request Queue ----
    struct ISCDeferredRequest {
        uint32_t uid;
        uint64_t pagesNeeded;
        void* iscContext;       // Points to ISCRequestContext from FTL layer
        uint64_t submitTick;    // When request was submitted
        uint64_t estimatedLatency; // Predicted processing latency
    };
    std::queue<ISCDeferredRequest> deferredISCRequests_;

    // ------------------------------------------------------------
    // 核心流程
    void tick(Tick &now);                 // 補 token + RR 發 I/O
    void dispatchICL(const Request& req, Tick &tickNow);

    // 工具
    UserAccount& getOrCreateUser(uint32_t uid);


    
};

}  // namespace HIL
}  // namespace SimpleSSD

#endif  // __HIL_SCHEDULER_CREDIT_SCHEDULER__
