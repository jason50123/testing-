#ifndef __HIL_SCHEDULER_CREDIT_SCHEDULER__
#define __HIL_SCHEDULER_CREDIT_SCHEDULER__

#include "hil/scheduler/scheduler.hh"
#include <unordered_map>
#include <queue>
#include <vector>

namespace SimpleSSD {
namespace HIL {

/**
 * Credit‑Based I/O Scheduler
 * ------------------------------------------------------------
 * * 以「頁面數 (page)」為計價單位，每個 user 帳戶持有 credit。
 * * submitRequest() 時，若 credit 足夠就立刻排入 readyQueue，並扣點；
 *   否則先放入 pendingQueue，等待之後 refillCredits() 再重試。
 * * tick() 會做兩件事：
 *     1. 週期性為所有 user 補充 credit。
 *     2. 嘗試把 pendingQueue 裡已經有足夠 credit 的 request 移到 readyQueue。
 * * schedule() 採 FCFS，從 readyQueue 取一筆送到底層 (這裡僅示範 pop/print)。
 */
class CreditScheduler : public Scheduler {
 private:
  struct UserInfo {
    uint64_t credit = 0;       ///< 目前剩餘 credit (page 為單位)
    uint64_t consumed = 0;     ///< 統計：已消耗 credit
    uint64_t initialCredit = 0; ///< 初始 credit 值
    uint64_t lastRefillTick = 0; ///< 上次補充 credit 的時間
  };

  // ---------------- internal state ----------------
  std::unordered_map<uint32_t, UserInfo> users;   ///< key = userID
  std::queue<Request> pendingQueue;               ///< 沒錢暫存區

  uint64_t creditPerRound;   ///< 每輪補充額度 (page)
  uint64_t roundInterval;    ///< 幾個 tick 算一輪 (ns / us 皆可，由 HIL 層決定)
  uint64_t nextRefillTick;   ///< 下一次補充的 tick
  uint64_t initialUserCredit; ///< 新用戶的初始 credit 值

  // 統計資料
  std::vector<Stats> stats;

  // 取得 / 建立使用者資料
  inline UserInfo &user(uint32_t uid) {
    auto it = users.find(uid);
    if (it == users.end()) {
      // 新用戶，設置初始 credit
      UserInfo newUser;
      newUser.credit = initialUserCredit;
      newUser.initialCredit = initialUserCredit;
      newUser.lastRefillTick = currentTick;
      return users.emplace(uid, newUser).first->second;
    }
    return it->second;
  }

  // 嘗試把 pendingQueue 的 request 搬到 readyQueue
  void drainPending();
  // 給所有 user 補 credit
  void refillCredits();

 public:
  explicit CreditScheduler(uint64_t creditPerRoundPages = 1024,
                           uint64_t intervalTicks = 1000,
                           uint64_t initialCredit = 100);
  ~CreditScheduler() override;

  // 基本排程介面覆寫 --------------------------------
  void submitRequest(Request &req) override;
  void schedule() override;
  void tick(uint64_t now) override;

  // 統計介面 ----------------------------------------
  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
  bool pendingForUser(uint32_t uid) const override;


};

}  // namespace HIL
}  // namespace SimpleSSD

#endif  // __HIL_SCHEDULER_CREDIT_SCHEDULER__
