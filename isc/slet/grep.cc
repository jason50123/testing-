#include "sims/cpu.hh"
#include "sims/ftl.hh"

#include "slet/grep.hh"

#include "fs/ext4/ext4.hh"
#include "runtime.hh"

#include "sims/configs.hh"

#define PR_SECTION LOG_ISC_SLET_GREP

#define ALIGN_UP(num, to) ((num + (to - 1)) & ~(to - 1))

namespace SimpleSSD {
namespace ISC {

template ISC_STS_SLET_ID Runtime::addSlet<GrepAPP>(_SIM_PARAMS);

using namespace SIM;

using SIM::FTL;

const char *GrepAPP::keyNumFiles = "numfiles";
const char *GrepAPP::keyFileSizes = "filesizes";
const char *GrepAPP::keyExts = "exts";

const char *GrepAPP::keyPath = "path";
const char *GrepAPP::keyPatt = "pattern";
const char *GrepAPP::keyResult = ISC_KEY_RESULT;
const char *GrepAPP::keyResultSize = ISC_KEY_RESULT_SIZE;

GrepAPP::GrepAPP(_SIM_PARAMS) {
  this->opt.name = strdup("GrepAPP");
  this->opt.cwd = strdup("/");
}

char badChar[256] = {};

#define GREP_TASK1_GREP CPU::ISC__TASK1
#define GREP_TASK2_STRSTR CPU::ISC__TASK2

// LeetCode 28
int GrepAPP::strstr(const char *s, size_t slen, const char *t,
                    size_t tlen _ADD_SIM_PARAMS) {
  size_t count = 0;
  auto doit = [&count](const uint8_t *s, size_t slen, const char *t,
                       size_t tlen _ADD_SIM_PARAMS) -> int {
    size_t shift = 0;
    while (shift <= (slen - tlen)) {
      ++count;
      int notmatch = tlen - 1;

      while (notmatch >= 0 && t[notmatch] == s[shift + notmatch])
        notmatch--;

      if (notmatch < 0)
        return shift;
      shift += std::max(1, notmatch - badChar[s[shift + notmatch]]);
    }
    return -1;
  };

  auto res = doit((const uint8_t *)s, slen, t, tlen _add_sim_params);
  simApplyManyLatency(CPU::ISC__SLET__GREP, GREP_TASK2_STRSTR, count);
  return res;
}

ISC_STS
GrepAPP::grep(const char *src, size_t slen, const char *pat,
              void *result _ADD_SIM_PARAMS) {
  auto doit = [this](const char *src, size_t slen, const char *pat,
                     void *result _ADD_SIM_PARAMS) {
    auto res = (result_t *)result;
    auto tlen = strlen(pat);

    if (!src || (slen < tlen)) {
      pr("ERROR! The source string is null or shorter than the pattern");
      res->line = nullptr;
      return ISC_STS_EARGS;
    }

    memset(badChar, 0xff, 256);
    for (size_t i = 0; i < tlen; ++i)
      badChar[(uint8_t)pat[i]] = i;

    // get the first match pattern offset
    auto ofs = this->strstr(src, slen, pat, strlen(pat) _add_sim_params);
    pr("Find pattern at %d", ofs);

    const char *line;
    for (line = &src[ofs], res->len = tlen; line > src && *(line - 1) != '\n';
         ++res->len, --line)
      ;  // find \n of prev line

    for (auto ch = &src[ofs + tlen]; *ch != '\n' && ch < &src[slen];
         ++ch, ++res->len)
      ;  // find \n of this line

    res->line = (char *)calloc(res->len + 1, 1);
    memcpy(res->line, line, res->len);
    return ISC_STS_OK;
  };
  auto res = doit(src, slen, pat, result _add_sim_params);
  simApplyLatency(CPU::ISC__SLET__GREP, GREP_TASK1_GREP);
  return res;
}

ISC_STS GrepAPP::builtin_startup(_SIM_PARAMS) {
  auto doit = [this](_SIM_PARAMS) -> ISC_STS {
    auto sts = ISC_STS_OK;

    // get options
    GenericFSA::ExtList *fileExtLists = nullptr;
    auto exts = (GenericFSA::Ext *)this->getOpt(keyExts);
    auto numFiles = (size_t *)this->getOpt(keyNumFiles);
    auto fileSizes = (size_t *)this->getOpt(keyFileSizes);
    auto nofsa = exts && numFiles && fileSizes;

    auto path = (const char *)this->getOpt(keyPath);
    auto pattern = (const char *)this->getOpt(GrepAPP::keyPatt);
    auto isdir = path[strlen(path) - 1] == '/';

    // allocate result size buffer
    char *bufOut = NULL;
    auto bufOutSz = (size_t *)calloc(1, sizeof(size_t));
    if (!bufOutSz)
      return ISC_STS_FAIL;
    *bufOutSz = 0;

    size_t nFiles = 0, iFile = 0, oBufDir = 0, szBufDir = 0, oBufOut = 0;
    char pathFile[512] = {0};

    // get numFiles and filename list
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

          ++nFiles;
          pr("[+%lu]: '%s'", oBufDir, name);
        }
      }
      free(dirExtList.exts);
    }
    else if (!nofsa) {
      strncpy(pathFile, path, 255);
      nFiles = 1;
    }
    else {
      // convert exts to ExtList
      fileExtLists =
          (GenericFSA::ExtList *)calloc(*numFiles, sizeof(GenericFSA::ExtList));

      for (size_t i = 0, ie = 0; i < *numFiles; ++i, ++ie) {
        fileExtLists[i].bytes = fileSizes[i];

        // exts is separated by 'block == UINT64_MAX'
        fileExtLists[i].exts = &exts[ie];
        // exts is separated by 'block == UINT64_MAX'
        fileExtLists[i].exts = &exts[ie];
        fileExtLists[i].len = 0;
        for (; exts[ie].block != UINT64_MAX; ++ie)
          fileExtLists[i].len++;
      }
      nFiles = *numFiles;
      if (!isdir)
        strncpy(pathFile, path, 255);
    }

    if (!isdir)
      goto readfile;

    pr("Num files: %lu %s", nFiles, isdir ? "(dir)" : "");
    for (oBufDir = 0; iFile < nFiles; ++iFile) {
      // find next file
      if (nofsa) {
        // we don't know the exact filename, give it a symbolic name
        snprintf(pathFile, 255, "%s[%lu]", path, iFile);
        goto readfile;
      }

      for (char name[256]; oBufDir < szBufDir; oBufDir += dirent->rec_len) {
        dirent = (Ext4::fake_dirent *)&bufDir[oBufDir];

        if (dirent->file_type != 1 /* reg file */)
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
      auto bufFile = (char *)calloc(1, szBufFile);
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

      // do task on this file
      result_t res;
      sts = grep(bufFile, fileExtList.bytes, pattern, &res _add_sim_params);
      pr("Find target line: (%lu) '%s'", res.len, res.line);

      {
        // the put the all the result and their size together,
        int szResEntry = sizeof(size_t) + ALIGN_UP(res.len, sizeof(size_t));

        oBufOut = *bufOutSz;
        *bufOutSz += szResEntry;
        pr("Update output size to %lu", *bufOutSz);

        char *bufOutNew = (char *)realloc(bufOut, *bufOutSz);
        if (!bufOutNew) {
          pr("Failed to realloc bufOut size");
          sts = ISC_STS_FAIL;
          goto out_free_fileExt;
        }

        // switch to new buffer
        bufOut = bufOutNew;
        memcpy(&bufOut[oBufOut], &res.len, sizeof(size_t));
        oBufOut += sizeof(size_t);
        memcpy(&bufOut[oBufOut], res.line, res.len);
      }
      free(res.line);

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
  simApplyLatency(CPU::ISC__SLET__GREP, CPU::ISC__START_SLET);
  return res;
}

}  // namespace ISC
}  // namespace SimpleSSD
