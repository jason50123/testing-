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
  struct UserInfo {
    uint64_t credit = 0;
    uint64_t consumed = 0;
    uint64_t initialCredit = 0;
    uint64_t lastRefillTick = 0;
    uint32_t weight         = 1;
  };

  std::unordered_map<uint32_t, UserInfo> users;
  std::queue<Request> pendingQueue;

  uint64_t creditPerRound;
  uint64_t roundInterval;
  uint64_t nextRefillTick;
  uint64_t initialUserCredit;

  std::vector<Stats> stats;

  UserInfo &user(uint32_t uid);

  void drainPending();
  void refillCredits(uint64_t now);


 public:
  explicit CreditScheduler(ICL::ICL* iclPtr,
                           uint64_t creditPerRoundPages = 2,
                           uint64_t intervalTicks = 1000,
                           uint64_t initialCredit = 100);
  ~CreditScheduler() override;

  void submitRequest(Request &req) override;
  void schedule() override ;  // 修改為帶 tick 引用
  void tick(uint64_t now) override;   // 修改為帶 tick 引用

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
  bool pendingForUser(uint32_t uid) const override;
  bool checkCredit(uint32_t uid, size_t need) const override;
  void useCredit(uint32_t uid, size_t used) override;
};

}  // namespace HIL
}  // namespace SimpleSSD

#endif  // __HIL_SCHEDULER_CREDIT_SCHEDULER__



