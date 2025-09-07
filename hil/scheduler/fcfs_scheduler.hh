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

  // ISC Support (aligned with Credit Scheduler interface)
  void submitRequestISC(uint32_t uid, Request &req);
  
  // Controller interface (unified with Credit Scheduler)
  bool checkCredit(uint32_t, size_t) const override { return true; } // FCFS always allows
  void useCredit(uint32_t, size_t) override {} // No-op for FCFS
  void useCreditISC(uint32_t, size_t) override {} // No-op for FCFS

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
  bool pendingForUser(uint32_t uid) const override;

 protected:
  ICL::ICL* pICL;
  uint64_t currentTick = 0;
  
  // Multi-User Architecture with ISC Support
  struct UserAccount {
    uint64_t totalConsumed = 0;
    uint64_t consumedHost = 0;    // Host I/O consumption
    uint64_t consumedISC = 0;     // ISC task consumption
    bool isActive = false;
    
    std::queue<Request> queue;     // Host I/O requests (READ/WRITE)
    std::queue<Request> queueISC;  // ISC task requests
  };

  // User management
  std::map<uint32_t, UserAccount> users;
  std::vector<uint32_t> statUsers_; // For statistics consistency: {1001, 1002}
  
  // Round-Robin state (similar to Credit Scheduler)
  uint32_t lastChosenUid = 0;     // For Host I/O Round-Robin
  uint32_t lastChosenUidISC = 0;  // For ISC Round-Robin
  
  // Statistics and reporting
  uint64_t lastReportTick = 0;
  static const uint64_t REPORT_INTERVAL_TICKS = 1000000000ULL; // 1 second in ticks
  
  // Constants (aligned with Credit Scheduler)
  static constexpr uint64_t PageSz = 4096;
  static const uint32_t MIN_USER_ID = 1001;
  static const uint32_t MAX_USER_ID = 1020;  // 支持20個用戶
  
  // Helper methods
  UserAccount& getOrCreateUser(uint32_t uid);
  void recordPageConsumption(uint32_t uid, uint64_t pages, bool isISC = false);
  void recordPageConsumptionByOpType(uint32_t uid, uint64_t pages, OpType op);
  void reportPageConsumption();
  bool tryDispatchHost();
  bool tryDispatchISC();
  void dispatchRequest(const Request& req, UserAccount&, bool);
  void dispatchICL(const Request& req, uint64_t &tick);
};

}  // namespace HIL
}  // namespace SimpleSSD

#endif