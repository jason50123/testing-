#ifndef __HIL_SCHEDULER_CREDIT_SCHEDULER__
#define __HIL_SCHEDULER_CREDIT_SCHEDULER__

#include "hil/scheduler/scheduler.hh"
#include <unordered_map>
#include <queue>
#include <vector>

namespace SimpleSSD {
namespace HIL {

class CreditScheduler : public Scheduler {
private:
  ICL::ICL* pICL;
  
  struct UserAccount {
    uint32_t weight;              // 用戶權重（付費等級）
    uint64_t credit;              // 當前可用credit (pages)
    uint64_t creditCap;           // credit上限 = weight × baseCreditCap
    uint64_t totalConsumed;       // 累計消耗的pages
    uint64_t lastActiveTime;      // 最後活動時間
    uint64_t overdraftCredit;     // 允許的透支額度
    bool isActive;                // 用戶是否已啟動
    std::queue<Request> readyQueue;  // 用戶專屬ready queue
  };

  std::unordered_map<uint32_t, UserAccount> users;
  std::queue<Request> pendingQueue;
  std::queue<Request> adminQueue;  // admin請求優先queue

  // 全局Credit參數
  uint64_t baseCreditRate;        // 基礎補充速率 (pages per refill)
  uint64_t baseCreditCap;         // 基礎credit上限 (pages)
  uint64_t refillInterval;        // 補充間隔 (ticks)
  uint64_t nextRefillTick;        // 下次補充時間
  uint64_t maxOverdraft;          // 最大透支額度
  
  // 定時中斷相關 (1秒tick)
  uint64_t lastRefillTimerTick;   // 上次定時補充時間
  uint64_t refillTimerInterval;   // 定時中斷間隔 (1秒 = 1G ticks)
  
  // 統計相關
  std::vector<Stats> stats;
  std::vector<uint32_t> statsUsers;
  
  // 動態負載檢測
  uint64_t currentTick;              // 當前時間
  uint64_t lastLoadCheckTick;        // 上次負載檢測時間
  double currentLoadFactor;          // 當前負載係數 (1.0 = 滿載)
  uint64_t pendingThresholdLow;      // 低負載閾值
  uint64_t pendingThresholdHigh;     // 高負載閾值
  
  // 早期credit重分配相關
  uint64_t lastEarlyRedistribution;   // 上次早期重分配時間
  uint64_t earlyRedistributionCount;  // 總重分配次數
  uint64_t emergencyReserve;          // 緊急reserve pool (pages)
  uint64_t maxEmergencyReserve;       // 最大reserve容量
  uint64_t lastReserveRefill;         // 上次reserve補充時間
  
  struct DepletionState {
    uint32_t totalActiveUsers;        // 總活躍用戶數
    uint32_t completelyDepletedUsers; // 完全耗盡用戶數
    uint64_t totalPendingPages;       // 總待處理頁數
    double depletionRatio;            // 耗盡比例
  };
  DepletionState depletionState;
  
  // 內部方法
  UserAccount& getOrCreateUser(uint32_t uid);
  void refillCredits();
  void refillCreditsTimer();         // 新增：定時中斷補充credit (1秒週期)
  void drainPending();
  void processOverdrafts();
  uint32_t selectNextUser();
  bool canServeRequest(const Request& req, UserAccount& user);
  void chargeCredit(uint32_t uid, uint64_t pages);
  void updateLoadFactor();           // 更新負載係數
  uint64_t calculateDynamicCreditCap(uint32_t weight);  // 動態計算 credit 上限

  // 早期重分配內部方法
  void updateDepletionState();
  void refillEmergencyReserve();
  double calculateRedistributionUrgency();
  uint64_t calculateEmergencyAllocation(uint32_t uid, uint64_t totalPool);

 public:
  explicit CreditScheduler(ICL::ICL* iclPtr,
                           uint64_t baseCreditRate = 50,     // 每次補充50 pages (限制性測試)
                           uint64_t baseCreditCap = 100,     // 基礎上限100 pages (更小的bucket讓限制效果明顯)
                           uint64_t refillIntervalTicks = 25000000);  // 25M ticks間隔 ≈ 0.5秒 (更頻繁補充)
  ~CreditScheduler() override;

  // 核心接口
  void submitRequest(Request &req) override;
  void schedule() override;
  void tick(uint64_t now) override;

  // 統計接口
  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
  
  // Credit管理接口
  bool pendingForUser(uint32_t uid) const override;
  bool checkCredit(uint32_t uid, size_t need) const override;
  void useCredit(uint32_t uid, size_t used) override;
  
  // 新增接口
  void chargeUserCredit(uint32_t uid, uint64_t pages);  // ISC使用
  uint64_t getUserCredit(uint32_t uid) const;
  uint64_t getUserWeight(uint32_t uid) const;
  
  // 早期重分配公開接口
  void checkEarlyRedistribution();
  bool shouldTriggerEarlyRedistribution();
  void distributeEmergencyCredits();
};

}  // namespace HIL
}  // namespace SimpleSSD

#endif  // __HIL_SCHEDULER_CREDIT_SCHEDULER__



