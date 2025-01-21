#include "gtest/gtest.h"
#include "runtime.hh"
#include "types.hh"

#include "sims/dram.hh"

#include <algorithm>

using namespace SimpleSSD::ISC;
using namespace SimpleSSD::ISC::SIM;
using namespace SimpleSSD::Utils;

class TestSlet : public GenericAPP {
 public:
  TestSlet() : GenericAPP() {}
  ISC_STS builtin_startup(_SIM_PARAMS) override { return ISC_STS_OK; }
};

template ISC_STS_SLET_ID Runtime::addSlet<TestSlet>(_SIM_PARAMS);

TEST(RuntimeTest, Basic) {
  int count = 0;
  auto id = Runtime::addSlet<TestSlet>();
  ASSERT_GE(id, ISC_STS_OK);
  ASSERT_EQ(id, ++count);

  auto key = "test key";
  auto val = strdup("test val");
  ASSERT_EQ(Runtime::setOpt(id, key, val), ISC_STS_OK);
  ASSERT_EQ(Runtime::getOpt(id, key), val);
  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  ASSERT_EQ(Runtime::delSlet(id), ISC_STS_OK);

  auto id2 = Runtime::addSlet<TestSlet>();
  ASSERT_GE(id2, ISC_STS_OK);
  ASSERT_EQ(id2, ++count);
  Runtime::destory();
}

#undef PR_SECTION
#define PR_SECTION NormalRegion
TEST(DramTest, PR_SECTION) {
  const size_t nmem = 100;
  const size_t unit = sizeof(size_t);
  auto mem = DRAM::alloc(nmem, unit);
  ASSERT_NE(mem, nullptr);

  size_t buffer[nmem];
  memset(buffer, 1, nmem * unit);

  // write data unit by unit
  for (size_t i = 0; i < nmem; ++i)
    ASSERT_EQ(0, mem->write(i * unit, unit, &i));

  // read large data at once
  ASSERT_EQ(0, mem->read(0, nmem * unit, &buffer));
  for (size_t i = 0; i < nmem; ++i)
    ASSERT_EQ(i, buffer[i]);

  // write large data at once
  for (size_t i = 0; i < nmem; ++i)
    buffer[i] = i;
  ASSERT_EQ(0, mem->write(0, unit * nmem, buffer));

  // read data unit by unit
  for (size_t i = 0, out; i < nmem; ++i) {
    ASSERT_EQ(0, mem->read(i * unit, unit, &out));
    ASSERT_EQ(buffer[i], out);
  }

  // offset doesn't need to align unit
  uint32_t out;
  size_t ofs = 4;
  ASSERT_EQ(0, mem->read(ofs, sizeof(out), &out));
  ASSERT_EQ(out, *(uint32_t *)((char *)buffer + ofs));

  DRAM::dealloc(mem);
  DRAM::destroy();
}

template <typename T>
void LRU_BASIC_TESTS(DRAM::Region *mem, size_t nmem, T *buffer, T tmp) {
  // read range and size is forced align the unit specified during init
  ASSERT_EQ(-ENOENT, mem->read(0, 0, &buffer[0]));
  ASSERT_EQ(-ENOENT, mem->read(UINT64_MAX, 0, &buffer[0]));
  ASSERT_EQ(-ENOENT, mem->read(0, UINT64_MAX, &buffer[0]));
  ASSERT_EQ(-ENOENT, mem->read(UINT64_MAX, UINT64_MAX, &buffer[0]));

  // reads should fail before the first write, buffer will keep
  for (size_t i = 0; i < nmem; ++i) {
    auto val = rand();
    buffer[i] = val;
    ASSERT_EQ(-ENOENT, mem->read(0, 0, &buffer[i]));
    ASSERT_EQ(true, buffer[i] == (uint64_t)val);
  }

  // reads of invalid data should also fail, and buffer should be not changed
  for (size_t i = 0; i < nmem; ++i) {
    ASSERT_EQ(0, mem->write(0, 0, &buffer[i]));
    for (size_t j = i + 1; j < nmem; ++j) {
      const auto original = buffer[j];
      ASSERT_EQ(-ENOENT, mem->read(0, 0, &buffer[j]));
      ASSERT_EQ(true, buffer[j] == original);
    }
  }

  // evict all old data sequentially
  for (size_t i = 0; i < nmem; ++i) {
    buffer[i] = nmem + i;
    ASSERT_EQ(0, mem->write(0, 0, &buffer[i]));
    buffer[i] = i;
    ASSERT_EQ(-ENOENT, mem->read(0, 0, &buffer[i]));
  }

  // randomize the lru list
  std::random_shuffle(&buffer[0], &buffer[nmem - 1]);
  for (size_t i = 0; i < nmem; ++i) {
    // randomly access valid data
    buffer[i] += nmem;
    ASSERT_EQ(0, mem->read(0, 0, &buffer[i]));
  }

  // check the evict order
  for (size_t i = 0; i < nmem; ++i) {
    tmp = i;
    ASSERT_EQ(0, mem->write(0, 0, &tmp));
    ASSERT_EQ(-ENOENT, mem->read(0, 0, &buffer[i]));
  }
}

#undef PR_SECTION
#define PR_SECTION LRURegion_SimpleTest
TEST(DramTest, PR_SECTION) {
  const size_t nmem = 100;
  const size_t unit = sizeof(size_t);
  auto mem = DRAM::alloc(nmem, unit, DRAM::TYPES::LRU_CACHE);
  ASSERT_NE(mem, nullptr);

  size_t buffer[nmem], tmp = 0;
  memset(buffer, 1, nmem * unit);

  LRU_BASIC_TESTS(mem, nmem, buffer, tmp);

  DRAM::dealloc(mem);
  DRAM::destroy();
}

#undef PR_SECTION
#define PR_SECTION LRURegion_KeyValTest
TEST(DramTest, PR_SECTION) {
  struct KV_t {
    uint64_t key;
    uint32_t vals[4];

    static int keyCmp(const void *dst, const void *src, size_t) {
      uint64_t d = ((KV_t *)dst)->key, s = ((KV_t *)src)->key;
      return (d == s) ? 0 : (d < s ? -1 : 1);
    }

    KV_t() {}
    KV_t(uint64_t k) : key(k) {}

    bool operator==(KV_t d) { return !memcmp(this, &d, sizeof(KV_t)); }
    bool operator==(uint64_t k) { return this->key == k; }

    KV_t &operator=(uint64_t k) {
      this->key = k;
      return *this;
    }
    KV_t &operator+=(uint64_t k) {
      this->key += k;
      return *this;
    }
  };

  const size_t nmem = 100;
  const size_t unit = sizeof(KV_t);
  auto mem = DRAM::alloc(nmem, unit, DRAM::TYPES::LRU_CACHE, KV_t::keyCmp);
  ASSERT_NE(mem, nullptr);

  KV_t buffer[nmem], tmp = 0;
  memset(buffer, 1, nmem * unit);

  LRU_BASIC_TESTS(mem, nmem, buffer, tmp);

  DRAM::dealloc(mem);
  DRAM::destroy();
}

#undef PR_SECTION
#define PR_SECTION LRURegion_KeyValPtrTest
TEST(DramTest, PR_SECTION) {
  struct Val_t {
    uint64_t a;
    uint64_t b;
  };

  struct KVp_t {
    uint64_t key;
    Val_t *val;

    static constexpr size_t keySize() { return sizeof(key); }
    static constexpr size_t valSize() { return sizeof(*val); }
    static constexpr size_t size() { return keySize() + valSize(); }

    static int keyCmp(const void *dst, const void *src, size_t) {
      uint64_t d = ((KVp_t *)dst)->key, s = ((KVp_t *)src)->key;
      return (d == s) ? 0 : (d < s ? -1 : 1);
    }

    KVp_t() {}
    KVp_t(uint64_t k) : key(k) {}

    bool operator==(KVp_t d) {
      return !memcmp(&this->key, &d.key, KVp_t::keySize()) &&
             !memcmp(this->val, d.val, KVp_t::valSize());
    }
    bool operator==(uint64_t k) { return this->key == k; }

    KVp_t &operator=(uint64_t k) {
      this->key = k;
      return *this;
    }
    KVp_t &operator+=(uint64_t k) {
      this->key += k;
      return *this;
    }
  };

  const size_t nmem = 100;
  const size_t unit = KVp_t::size();
  auto mem = DRAM::alloc(
      nmem, unit, DRAM::TYPES::LRU_CACHE, KVp_t::keyCmp,
      [](void *mem, const void *buf, size_t) -> void * {
        auto b = (const KVp_t *)buf;
        memcpy(mem, &b->key, KVp_t::keySize());
        memcpy((char *)mem + KVp_t::keySize(), b->val, KVp_t::valSize());
        return mem;
      },
      [](void *buf, const void *mem, size_t) -> void * {
        auto b = (KVp_t *)buf;
        memcpy(&b->key, mem, KVp_t::keySize());
        memcpy(b->val, (char *)mem + KVp_t::keySize(), KVp_t::valSize());
        return buf;
      });
  ASSERT_NE(mem, nullptr);

  Val_t vals[nmem], tmpv;
  KVp_t buffer[nmem], tmp = 0;

  memset(buffer, 1, nmem * sizeof(KVp_t));
  memset(vals, 1, nmem * KVp_t::valSize());

  for (size_t i = 0; i < nmem; ++i)
    buffer[i].val = &vals[i];
  tmp.val = &tmpv;

  LRU_BASIC_TESTS(mem, nmem, buffer, tmp);

  DRAM::dealloc(mem);
  DRAM::destroy();
}