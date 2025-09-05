/*
 * Copyright (C) 2017 CAMELab
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
#include "util/def.hh"
#include <cstdlib>
#include <iostream>

namespace SimpleSSD {

LPNRange::_LPNRange() : slpn(0), nlp(0) {}
LPNRange::_LPNRange(uint64_t s, uint64_t n) : slpn(s), nlp(n) {}

namespace HIL {

Request::_Request()
    : reqID(0),
      reqSubID(0),
      offset(0),
      length(0),
      userID(0),
      prio(0),
      op(OpType::READ),
      ns(nullptr),                           // 加入 ns 初始化
      finishedAt(0),
      function(nullptr),                     // 加入 function 初始化
      context(nullptr),
      state(RequestState::NORMAL),           // 新增欄位初始化
      creditNeeded(0),                       // 新增欄位初始化
      deferTime(0),                          // 新增欄位初始化
      originalFunction(nullptr),             // 新增欄位初始化
      originalContext(nullptr) {}            // 新增欄位初始化

Request::_Request(DMAFunction &f, void *c)
    : reqID(0),
      reqSubID(0),
      offset(0),
      length(0),
      userID(0), 
      prio(0),
      op(OpType::READ),
      ns(nullptr),                           // 加入 ns 初始化
      finishedAt(0),
      function(f),
      context(c),
      state(RequestState::NORMAL),           // 新增欄位初始化
      creditNeeded(0),                       // 新增欄位初始化
      deferTime(0),                          // 新增欄位初始化
      originalFunction(nullptr),             // 新增欄位初始化
      originalContext(nullptr) {}            // 新增欄位初始化

bool Request::operator()(const Request &a, const Request &b) {
  return a.finishedAt > b.finishedAt;
}

}  // namespace HIL

namespace ICL {

Request::_Request() 
    : reqID(0), 
      reqSubID(0), 
      offset(0), 
      length(0),
      userID(0),
      prio(0) {}    // 移除 state 初始化

Request::_Request(HIL::Request &r)
    : reqID(r.reqID),
      reqSubID(r.reqSubID),
      offset(r.offset),
      length(r.length),
      range(r.range),
      userID(r.userID),
      prio(r.prio) {}  // 不複製 state

}  // namespace ICL

namespace FTL {

Request::_Request(uint32_t iocount)
    : reqID(0), reqSubID(0), lpn(0), ioFlag(iocount) {}

Request::_Request(uint32_t iocount, ICL::Request &r)
    : reqID(r.reqID),
      reqSubID(r.reqSubID),
      lpn(r.range.slpn / iocount),
      ioFlag(iocount),
      iclReq(r) {
  ioFlag.set(r.range.slpn % iocount);
}

}  // namespace FTL

namespace PAL {

Request::_Request(uint32_t iocount)
    : reqID(0),
      reqSubID(0),
      blockIndex(0),
      pageIndex(0),
      ioFlag(iocount),
      ftlReq({}) {}

Request::_Request(FTL::Request &r)
    : reqID(r.reqID),
      reqSubID(r.reqSubID),
      blockIndex(0),
      pageIndex(0),
      ioFlag(r.ioFlag),
      ftlReq(r) {}

}  // namespace PAL

}  // namespace SimpleSSD