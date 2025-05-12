#ifndef __HIL_SCHEDULER_FCFS_SCHEDULER__
#define __HIL_SCHEDULER_FCFS_SCHEDULER__

#include "hil/scheduler/scheduler.hh"
#include <vector>

namespace SimpleSSD {

namespace HIL {

class FCFSScheduler : public Scheduler {
 private:
  std::vector<Stats> stats;

 public:
  FCFSScheduler();
  ~FCFSScheduler() override;

  // 覆寫基本排程器介面
  void submitRequest(Request &req) override;
  void schedule() override;
  void tick(uint64_t now) override;

  // 實現統計資訊介面
  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};

}  // namespace HIL

}  // namespace SimpleSSD

#endif