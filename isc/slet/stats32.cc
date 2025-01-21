#include "sims/cpu.hh"
#include "sims/ftl.hh"

#include "fs/ext4/ext4.hh"
#include "slet/stats32.hh"

#include "runtime.hh"

#include "sims/configs.hh"

#define PR_SECTION LOG_ISC_SLET_STATS32

namespace SimpleSSD {
namespace ISC {

template ISC_STS_SLET_ID Runtime::addSlet<Stats32APP>(_SIM_PARAMS);

using namespace SIM;

const char *Stats32APP::keyNumFiles = "numfiles";
const char *Stats32APP::keyFileSizes = "filesizes";
const char *Stats32APP::keyExts = "exts";

const char *Stats32APP::keyPath = "path";
const char *Stats32APP::keyResult = ISC_KEY_RESULT;
const char *Stats32APP::keyResultSize = ISC_KEY_RESULT_SIZE;

#define STATS32_TASK1_SUM CPU::ISC__TASK1

Stats32APP::Stats32APP(_SIM_PARAMS) {
  this->opt.name = strdup("Stats32APP");
  this->opt.cwd = strdup("/");
}

#define MIN32(a, b) ({ (b) ^ (((a) ^ (b)) & -((a) < (b))); })
#define MAX32(a, b) ({ (b) ^ (((a) ^ (b)) & -((a) > (b))); })

ISC_STS
Stats32APP::sum(const int32_t *src, size_t cnt, result_t *res _ADD_SIM_PARAMS) {
  for (size_t i = 0; i < cnt; ++i) {
    res->sum += src[i];
    res->max = MAX32(res->max, src[i]);
    res->min = MIN32(res->min, src[i]);
  }
  return ISC_STS_OK;
}

ISC_STS Stats32APP::builtin_startup(_SIM_PARAMS) {
  auto doit = [this](_SIM_PARAMS) -> ISC_STS {
    auto sts = ISC_STS_OK;

    // get options
    GenericFSA::ExtList *fileExtLists = nullptr;
    auto exts = (GenericFSA::Ext *)this->getOpt(keyExts);
    auto numFiles = (size_t *)this->getOpt(keyNumFiles);
    auto fileSizes = (size_t *)this->getOpt(keyFileSizes);
    auto nofsa = exts && numFiles && fileSizes;

    auto path = (const char *)this->getOpt(keyPath);
    auto isdir = path[strlen(path) - 1] == '/';

    // allocate result size buffer
    auto bufOutSz = (size_t *)calloc(1, sizeof(size_t));
    if (!bufOutSz)
      return ISC_STS_FAIL;

    size_t iFile = 0, oBufDir = 0, szBufDir = 0;
    char pathFile[512] = {0};

    // determine output buffer size
    result_t *bufOut;
    char *bufDir = nullptr;
    Ext4::fake_dirent *dirent;

    if (isdir && !nofsa) {
      auto dirExtList = Runtime::getExts(path _add_sim_params);
      for (size_t ie = 0; ie < dirExtList.len; ++ie)
        szBufDir += dirExtList.exts[ie].len * BLK_SIZE;

      // read dir entries
      bufDir = (char *)calloc(1, szBufDir);
      if (!bufDir) {
        free(dirExtList.exts);
        sts = ISC_STS_FAIL;
        goto out_free_ressize;
      }
      for (size_t ie = 0, ofsBuf = 0; ie < dirExtList.len; ++ie) {
        auto ofsData = dirExtList.exts[ie].slbn * BLK_SIZE;
        auto szData = dirExtList.exts[ie].len * BLK_SIZE;
        SIM::FTL::read(&bufDir[ofsBuf], ofsData, szData _add_sim_params);
        ofsBuf += szData;
      }

      // check number of files
      for (oBufDir = 0; oBufDir < szBufDir; oBufDir += dirent->rec_len) {
        dirent = (Ext4::fake_dirent *)&bufDir[oBufDir];

        if (dirent->inode == 0) {
          // skip 1 dummy tail dent (checksum)
          if (dirent->file_type == 0xde) {
            pr("End of directory block (+%d)", dirent->rec_len);
            continue;
          }
          break;
        }

        if (dirent->file_type != 2 /* dir */) {
          char name[256] = {0};
          strncpy(name, &bufDir[oBufDir + sizeof(*dirent)], dirent->name_len);

          *bufOutSz += BYTES_PER_RESULT;
          pr("[+%lu]: '%s'", oBufDir, name);
        }
      }
      free(dirExtList.exts);
    }
    else if (!nofsa) {
      strncpy(pathFile, path, 255);
      *bufOutSz = BYTES_PER_RESULT;
    }
    else {
      // convert exts to ExtList
      fileExtLists =
          (GenericFSA::ExtList *)calloc(*numFiles, sizeof(GenericFSA::ExtList));

      for (size_t i = 0, ie = 0; i < *numFiles; ++i, ++ie) {
        fileExtLists[i].bytes = fileSizes[i];

        // exts is separated by 'block == UINT64_MAX'
        fileExtLists[i].exts = &exts[ie];
        fileExtLists[i].len = 0;
        for (; exts[ie].block != UINT64_MAX; ++ie)
          fileExtLists[i].len++;
      }

      *bufOutSz = *numFiles * BYTES_PER_RESULT;
      if (!isdir)
        strncpy(pathFile, path, 255);
    }

    // allocate output buffer and start hashing
    bufOut = (result_t *)calloc(*bufOutSz + 1, sizeof(uint8_t));
    if (!bufOut) {
      sts = ISC_STS_FAIL;
      goto out_free_ressize;
    }
    if (!isdir)
      goto readfile;

    pr("Num files: %lu %s", *bufOutSz / BYTES_PER_RESULT, isdir ? "(dir)" : "");
    for (oBufDir = 0; iFile < (*bufOutSz / BYTES_PER_RESULT); ++iFile) {
      // find next file
      if (nofsa) {
        // we don't know the exact filename, give it a symbolic name
        snprintf(pathFile, 255, "%s[%lu]", path, iFile);
        goto readfile;
      }

      for (char name[256]; oBufDir < szBufDir; oBufDir += dirent->rec_len) {
        dirent = (Ext4::fake_dirent *)&bufDir[oBufDir];

        if (dirent->file_type == 2 /* dir */)
          continue;

        memset(name, 0, 256);
        strncpy(name, &bufDir[oBufDir + sizeof(*dirent)], dirent->name_len);

        pr("[+%lu]: Find file %s", oBufDir, name);
        snprintf(pathFile, 511, "%s%s", path, name);
        oBufDir += dirent->rec_len;
        break;
      }

      // read data of the path
    readfile:
      pr("File[%lu]: %s", iFile, pathFile);

      auto szBufFile = 0;
      GenericFSA::ExtList fileExtList;
      if (nofsa)
        fileExtList = fileExtLists[iFile];
      else
        fileExtList = Runtime::getExts(pathFile _add_sim_params);

      for (size_t ie = 0; ie < fileExtList.len; ++ie)
        szBufFile += fileExtList.exts[ie].len * BLK_SIZE;

      // read file data
      auto bufFile = (uint8_t *)calloc(1, szBufFile);
      if (!bufFile) {
        sts = ISC_STS_FAIL;
        goto out_free_fileExt;
      }
      for (size_t ie = 0, ofsBuf = 0; ie < fileExtList.len; ++ie) {
        auto ofsData = fileExtList.exts[ie].slbn * BLK_SIZE;
        auto szData = fileExtList.exts[ie].len * BLK_SIZE;
        SIM::FTL::read(&bufFile[ofsBuf], ofsData, szData _add_sim_params);
        ofsBuf += szData;
      }

      // main operation
      {
        result_t *res = &bufOut[iFile];
        size_t count = fileExtList.bytes / sizeof(int32_t);
        sts = sum((int32_t *)bufFile, count, res _add_sim_params);
        pr("Sum,Min,Max,Cnt=%ld,%d,%d,%lu", res->sum, res->min, res->max,
           count);
        simApplyManyLatency(CPU::ISC__SLET__STATS32, STATS32_TASK1_SUM, count);
      }

      free(bufFile);
    out_free_fileExt:
      if (!nofsa)
        free(fileExtList.exts);

      if (sts != ISC_STS_OK)
        goto out_free_result;
    }

    // set result
    sts = this->setOpt(keyResultSize, bufOutSz);
    if (sts == ISC_STS_OK)
      sts = this->setOpt(keyResult, bufOut);

  out_free_result:
    if (sts != ISC_STS_OK)
      free(bufOut);
    if (isdir && !nofsa)
      free(bufDir);
    if (nofsa)
      free(fileExtLists);
  out_free_ressize:
    if (sts != ISC_STS_OK)
      free(bufOutSz);
    return sts;
  };

  auto res = doit(_sim_params);
  simApplyLatency(CPU::ISC__SLET__STATS32, CPU::ISC__START_SLET);
  return res;
}

}  // namespace ISC
}  // namespace SimpleSSD
