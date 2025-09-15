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

#ifndef __HIL_NVME_ABSTRACT_SUBSYSTEM__
#define __HIL_NVME_ABSTRACT_SUBSYSTEM__

#include "hil/nvme/namespace.hh"
#include "util/simplessd.hh"

namespace SimpleSSD {

namespace HIL {

namespace NVMe {

class Controller;

class AbstractSubsystem : public StatObject {
 protected:
  Controller *pParent;

  ConfigData &cfgdata;
  ConfigReader &conf;

 public:
  AbstractSubsystem(Controller *, ConfigData &);
  virtual ~AbstractSubsystem();

  virtual void init() = 0;
  virtual void submitCommand(SQEntryWrapper &, RequestFunction) = 0;
  virtual void getNVMCapacity(uint64_t &, uint64_t &) = 0;
  virtual uint32_t validNamespaceCount() = 0;
  virtual uint32_t getNamespaceLbaSize(uint32_t nsid) const = 0;
  
  // Credit-aware scheduling interface
  // Check if the subsystem can serve a request for the given UID.
  // Overload with and without an explicit resource requirement (e.g., pages).
  virtual bool canServe(uint32_t uid) const = 0;
  virtual bool canServe(uint32_t uid, size_t need) const = 0;
};

}  // namespace NVMe

}  // namespace HIL

}  // namespace SimpleSSD

#endif
