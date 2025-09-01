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

namespace SimpleSSD { namespace HIL {

class Scheduler {
 protected:
  std::queue<Request> requestQueue;
  uint64_t currentTick;

 public:
  Scheduler();
  virtual ~Scheduler();

  // 送件（仍保留，讓子類別去定義行為）
  virtual void submitRequest(Request &req);

  // 單步排程（保留），但 tick 以 by-ref 回填最新時間
  virtual void schedule();
  virtual void tick(uint64_t &now);     // ★ 改成 by-ref

  // ★ 新增：同步處理直到「指定 req 完成」，並把完成時間寫回 now
  //   預設實作 = submitRequest + tick（子類別可覆寫為真正阻塞到完成）
  virtual void processUntil(Request &req, uint64_t &now);

  // 統計
  virtual void getStatList(std::vector<Stats> &, std::string) = 0;
  virtual void getStatValues(std::vector<double> &) = 0;
  virtual void resetStatValues() = 0;

  // 供外部（FTL/ISC）查詢/扣款（子類可覆寫）
  virtual bool   pendingForUser(uint32_t /*uid*/) const { return false; }
  virtual bool   checkCredit(uint32_t /*uid*/, size_t /*needed*/) const { return true; }
  virtual void   useCredit(uint32_t /*uid*/, size_t /*used*/) {}
  virtual void   useCreditISC(uint32_t /*uid*/, size_t /*used*/) {}
};

} } // namespace

#endif
