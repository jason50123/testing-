/*
 * Copyright (C) 2024
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __HIL_SCHEDULER_SCHEDULER__
#define __HIL_SCHEDULER_SCHEDULER__

#include <queue>
#include <vector>
#include "util/def.hh"
#include "util/simplessd.hh"
#include "icl/icl.hh"

namespace SimpleSSD {

namespace HIL {

class Scheduler {
 protected:
  std::queue<Request> requestQueue;
  uint64_t currentTick;

 public:
  Scheduler();
  virtual ~Scheduler();

  // 基本排程器介面
  virtual void submitRequest(Request &req);
  virtual void schedule();
  virtual void tick(uint64_t now);
  virtual void processUntil(Request &req, uint64_t &completionTick) = 0;

  // 取得統計資訊
  virtual void getStatList(std::vector<Stats> &, std::string) = 0;
  virtual void getStatValues(std::vector<double> &) = 0;
  virtual void resetStatValues() = 0;
  virtual bool pendingForUser(uint32_t /*uid*/) const { return false; }
  virtual bool checkCredit(uint32_t /*uid*/, size_t /*needed*/) const { return true; }
  virtual void useCredit(uint32_t /*uid*/, size_t /*used*/) {}
  virtual void useCreditISC(uint32_t /*uid*/, size_t /*used*/) {}
};

}  // namespace HIL

}  // namespace SimpleSSD

#endif 