#ifndef __HIL_SCHEDULER_FCFS_SCHEDULER__
#define __HIL_SCHEDULER_FCFS_SCHEDULER__

#include "hil/scheduler/scheduler.hh"
#include <vector>
#include <map>


namespace SimpleSSD {
namespace HIL {

class FCFSScheduler : public Scheduler {
 public:
  explicit FCFSScheduler(ICL::ICL* iclPtr);
  ~FCFSScheduler() override;

  void submitRequest(Request &req) override;
  void schedule() override;
  void tick(uint64_t now) override;
  void processUntil(Request &req, uint64_t &completionTick);

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
  bool pendingForUser(uint32_t) const override { return false; }

 protected:
  ICL::ICL* pICL;
  std::queue<Request> requestQueue;
  uint64_t currentTick = 0;
  
  // Page consumption tracking per user
  std::map<uint32_t, uint64_t> userPageConsumption;
  uint64_t lastReportTick = 0;
  static const uint64_t REPORT_INTERVAL_TICKS = 50000000ULL;  // 1 second in ticks
  
  // 預定義用戶範圍以支持統計系統
  static const uint32_t MIN_USER_ID = 1001;
  static const uint32_t MAX_USER_ID = 1020;  // 支持20個用戶
  
  void recordPageConsumption(uint32_t uid, uint64_t pages);
  void reportPageConsumption();
};

}  // namespace HIL
}  // namespace SimpleSSD

#endif