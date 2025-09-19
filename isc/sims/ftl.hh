#ifndef __SIMPLESSD_ISC_SIM_FTL_HH__
#define __SIMPLESSD_ISC_SIM_FTL_HH__

#include <unistd.h>

#include <cstdint>
#include <cstdlib>

#ifdef ISC_TEST
#include "sims/cpu.hh"
#else
#include "isc/sims/cpu.hh"
#endif

#ifdef ISC_TEST
#endif

namespace SimpleSSD {
namespace ISC {
namespace SIM {

// ★ Forward declaration to avoid circular includes
struct ISCRequestContext;

// ★ Simple callback function type - 避免std::function的複雜性
typedef void (*ISCCallbackFunc)(uint64_t completionTick, void* userData);

// ★ ISC請求Context結構 - 用於非阻塞執行
struct ISCRequestContext {
  void *buffer;              // 目標buffer
  size_t offset;             // 檔案偏移
  size_t size;               // 讀取大小
  uint64_t requestStartTick; // 請求開始時間
  void *originalSimCtx;      // 原始模擬器context
  ISCCallbackFunc callback;  // 簡單的function pointer
  void *callbackUserData;    // callback的用戶數據

  // ★ 構造函數確保初始化
  ISCRequestContext() : buffer(nullptr), offset(0), size(0),
                       requestStartTick(0), originalSimCtx(nullptr),
                       callback(nullptr), callbackUserData(nullptr) {}
};

class FTL {
 protected:
  static size_t lbaSize;
  static char *pathFilesystemImg;
  static void *cache;

 public:
  FTL() = delete;

  static void setImage(const char *p, size_t = 512);
  static void setCache(void *pICL) { FTL::cache = pICL; }

  static void destory() {
    free(pathFilesystemImg);
    pathFilesystemImg = nullptr;
  }
void  setCurrentUid(uint32_t uid);
#ifndef ISC_TEST
  static void read(void *, size_t, size_t);
#endif
  static void read(void *, size_t, size_t _ADD_SIM_PARAMS);
};
  void setCurrentUid(uint32_t uid);
  
}  // namespace SIM
}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SIM_FTL_HH__ */