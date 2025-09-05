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

#include "hil/scheduler/scheduler.hh"
#include "util/algorithm.hh"
#include "util/def.hh"

namespace SimpleSSD {

namespace HIL {

Scheduler::Scheduler() : currentTick(0) {
  debugprint(LOG_HIL, "Scheduler initialized");
}

Scheduler::~Scheduler() {
  debugprint(LOG_HIL, "Scheduler destroyed");
}

void Scheduler::submitRequest(Request &req) {
  debugprint(LOG_HIL, "Scheduler | Submit request %" PRIu64 " | Type %s | Size %" PRIu64,
             req.reqID,
             req.range.slpn == 0 ? "READ" : "WRITE",
             req.length);
  requestQueue.push(req);
}

void Scheduler::schedule() {
  // 基本的 FCFS 排程
  if (!requestQueue.empty()) {
    Request req = requestQueue.front();
    requestQueue.pop();
    
    debugprint(LOG_HIL, "Scheduler | Process request %" PRIu64 " | Type %s | Size %" PRIu64,
               req.reqID,
               req.range.slpn == 0 ? "READ" : "WRITE",
               req.length);
  }
}

void Scheduler::tick(uint64_t now) {
  currentTick = now;
  schedule();
}

}  // namespace HIL

}  // namespace SimpleSSD 