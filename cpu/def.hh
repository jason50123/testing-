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

#pragma once

#ifndef __CPU_DEF__
#define __CPU_DEF__

#include <cinttypes>

namespace SimpleSSD {

namespace CPU {

typedef enum : uint16_t {
  FTL,
  FTL__PAGE_MAPPING,
  ICL,
  ICL__GENERIC_CACHE,
  HIL,
  NVME__CONTROLLER,
  NVME__PRPLIST,
  NVME__SGL,
  NVME__SUBSYSTEM,
  NVME__NAMESPACE,
  NVME__OCSSD,
  UFS__DEVICE,
  SATA__DEVICE,

  // ISC NAMESPACES
  ISC__RUNTIME,
  ISC__FSA,
  ISC__FSA__EXT4,
  ISC__SLET,
  ISC__SLET__STATDIR,
  ISC__SLET__MD5,
  ISC__SLET__GREP,
  ISC__SLET__STATS32,

  TOTAL_NAMESPACES
} NAMESPACE;

typedef enum : uint16_t {
  // Common
  READ,
  WRITE,
  FLUSH,
  TRIM,
  FORMAT,

  // FTL__PAGE_MAPPING
  READ_INTERNAL,
  WRITE_INTERNAL,
  ERASE_INTERNAL,
  TRIM_INTERNAL,
  SELECT_VICTIM_BLOCK,
  DO_GARBAGE_COLLECTION,

  // NVME__CONTROLLER / NVME__SUBSYSTEM
  CREATE_CQ,
  CREATE_SQ,

  // NVME__CONTROLLER
  COLLECT_SQ,
  HANDLE_REQUEST,
  WORK,
  COMPLETION,

  // NVME__PRPLIST
  GET_PRPLIST_FROM_PRP,

  // NVME__SGL
  PARSE_SGL_SEGMENT,

  // NVME__SUBSYSTEM / NVME__NAMESPACE
  SUBMIT_COMMAND,

  // NVME__SUBSYSTEM
  CONVERT_UNIT,
  FORMAT_NVM,

  // NVME__NAMESPACE
  DATASET_MANAGEMENT,

  // NVME__OCSSD
  VECTOR_CHUNK_READ,
  VECTOR_CHUNK_WRITE,
  VECTOR_CHUNK_RESET,
  PHYSICAL_PAGE_READ,
  PHYSICAL_PAGE_WRITE,
  PHYSICAL_BLOCK_ERASE,

  // UFS__DEVICE
  PROCESS_QUERY_COMMAND,
  PROCESS_COMMAND,
  PRDT_READ,
  PRDT_WRITE,

  // SATA__DEVICE
  READ_DMA,
  READ_NCQ,
  READ_DMA_SETUP,
  READ_DMA_DONE,
  WRITE_DMA,
  WRITE_NCQ,
  WRITE_DMA_SETUP,
  WRITE_DMA_DONE,

  ISC__GET,
  ISC__SET,

  // FS funcs
  ISC__INIT,
  ISC__GET_SUPER,
  ISC__GET_GROUP,
  ISC__GET_IMAP,
  ISC__GET_INODE,
  ISC__GET_INODE_PARENT,
  ISC__GET_EXTENT_SIZE,
  ISC__GET_EXTENT_INTERNAL,
  ISC__GET_EXTENT,
  ISC__DIR_SEARCH_FILE,
  ISC__NAMEI,

  // runtime funcs (each FSA/APP should have ADD_SLET)
  ISC__START_SLET,
  ISC__SET_OPT,
  ISC__GET_OPT,
  ISC__TASK1,
  ISC__TASK2,
  ISC__TASK3,
  ISC__TASK4,
  ISC__TASK5,
  ISC__ADD_SLET__EXT4,
  ISC__ADD_SLET__STATDIR,
  ISC__ADD_SLET__MD5,
  ISC__ADD_SLET__GREP,
  ISC__ADD_SLET__STATS32,

  TOTAL_FUNCTIONS,
} FUNCTION;

}  // namespace CPU

}  // namespace SimpleSSD

#endif
