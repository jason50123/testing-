#include "hil/scheduler/credit_scheduler.hh"
#include "util/def.hh"
#include "util/algorithm.hh"

namespace SimpleSSD {
namespace HIL {

#ifndef CS_DBG_ON
#define CS_DBG_ON 0  // 關閉詳細調試輸出，避免log過大
#endif
#define CS_DBG(fmt, ...) \
        do{ if (CS_DBG_ON) debugprint(LOG_HIL, fmt, ##__VA_ARGS__);}while(0)

// 關鍵事件的debug (較少輸出) - 現在也可以關閉  
#define CS_DBG_KEY(fmt, ...) \
        do{ if (CS_DBG_ON) debugprint(LOG_HIL, fmt, ##__VA_ARGS__);}while(0)

// 最小化輸出：只保留極重要的事件
#ifndef CS_MINIMAL_LOG
#define CS_MINIMAL_LOG 1  // 僅輸出最重要事件
#endif
#define CS_DBG_MINIMAL(fmt, ...) \
        do{ if (CS_MINIMAL_LOG) debugprint(LOG_HIL, fmt, ##__VA_ARGS__);}while(0)

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096ULL
#endif

// 新設計的系統參數 - 基於實際SSD bandwidth
static const uint64_t DEFAULT_OVERDRAFT_RATIO = 2;  // 透支額度為credit cap的2倍
static const uint64_t INACTIVITY_PENALTY_THRESHOLD = 1000000000ULL;  // 10億ticks沒活動開始懲罰

// SSD bandwidth相關參數 - 基於實際FIO測試結果
static const uint64_t SSD_BANDWIDTH_MBS = 80;  // 80 MB/s (實測結果)
static const uint64_t SSD_IOPS = 20000;        // 20,000 IOPS (實測結果)  
static const uint64_t PAGE_SIZE_BYTES = 4096;  // 4KB per page
static const uint64_t PAGES_PER_SECOND = SSD_IOPS;  // 使用實測IOPS值

CreditScheduler::CreditScheduler(ICL::ICL* iclPtr,
                                 uint64_t baseCreditRate,
                                 uint64_t baseCreditCap,
                                 uint64_t refillIntervalTicks)
    : pICL(iclPtr),
      baseCreditRate(baseCreditRate),
      baseCreditCap(baseCreditCap), 
      refillInterval(refillIntervalTicks),
      nextRefillTick(0),
      maxOverdraft(baseCreditCap * DEFAULT_OVERDRAFT_RATIO),
      lastRefillTimerTick(0),        // 初始化定時中斷時間
      refillTimerInterval(1000000000ULL),  // 1秒 = 1G ticks
      currentTick(0),
      lastLoadCheckTick(0),
      currentLoadFactor(0.0),
      pendingThresholdLow(100),      // 低於100個pending為輕負載
      pendingThresholdHigh(1000),    // 高於1000個pending為重負載
      // 早期重分配初始化
      lastEarlyRedistribution(0),
      earlyRedistributionCount(0),
      emergencyReserve(PAGES_PER_SECOND / 4),      // 初始reserve: 5,000 pages (0.25秒)
      maxEmergencyReserve(PAGES_PER_SECOND),       // 最大reserve: 20,000 pages (1秒)
      lastReserveRefill(0) {
  
  // 初始化耗盡狀態
  depletionState = {0, 0, 0, 0.0};
      
  CS_DBG_KEY("CreditScheduler | DYNAMIC DESIGN | baseCreditRate=%lu pages, baseCreditCap=%lu pages, interval=%lu ticks",
             baseCreditRate, baseCreditCap, refillInterval);
  CS_DBG_KEY("CreditScheduler | Load thresholds: low=%lu, high=%lu", pendingThresholdLow, pendingThresholdHigh);
  CS_DBG_KEY("CreditScheduler | Early redistribution enabled: reserve=%lu, max=%lu",
             emergencyReserve, maxEmergencyReserve);
             
  statsUsers = { 1001, 1002 };
}

CreditScheduler::~CreditScheduler() {
  CS_DBG("CreditScheduler | destroyed");
}

CreditScheduler::UserAccount &
CreditScheduler::getOrCreateUser(uint32_t uid)
{
    auto it = users.find(uid);
    if (it == users.end()) {
        UserAccount account;
        
        // 設定用戶權重（付費等級）
        account.weight = 1;  // 默認權重
        if (uid == 1001) account.weight = 2;  // 中等付費用戶
        if (uid == 1002) account.weight = 8;  // 高級付費用戶
        
        // 修正：降低creditCap基準值，避免credit用不完的問題
        // 設為用戶在0.5秒內應得的pages數，更符合實際使用需求
        uint64_t totalSystemWeight = 10;  // uid 1001(weight=2) + uid 1002(weight=8)
        uint64_t pagesPerHalfSecond = (PAGES_PER_SECOND / 2 * account.weight) / totalSystemWeight;  
        account.creditCap = std::max(pagesPerHalfSecond, static_cast<uint64_t>(50));  // 最少50 pages，大幅降低基準
        account.overdraftCredit = account.creditCap * DEFAULT_OVERDRAFT_RATIO;
        
        // 初始化狀態 - 不給初始credit，等到第一次I/O請求才開始分配
        account.credit = 0;  // 新用戶不給初始credit，避免浪費在未活動用戶上
        account.totalConsumed = 0;
        account.lastActiveTime = currentTick;
        account.isActive = false;  // 等到第一次I/O才啟動
        
        CS_DBG_KEY("CreditScheduler | Created user %u: weight=%u, creditCap=%lu, overdraft=%lu, initial_credit=%lu",
                   uid, account.weight, account.creditCap, account.overdraftCredit, account.credit);
        
        return users.emplace(uid, account).first->second;
    }
    return it->second;
}

void CreditScheduler::refillCredits()
{
    CS_DBG("CreditScheduler | Dynamic credit refill based on load");
    
    // 更新負載係數
    updateLoadFactor();
    
    // 計算總權重 - 只計算有I/O活動的用戶
    uint64_t totalWeight = 0;
    for (auto &kv : users) {
        if (kv.first != 0 && kv.second.totalConsumed > 0) {  // 跳過admin用戶和無I/O活動的用戶
            totalWeight += kv.second.weight;
        }
    }
    
    if (totalWeight == 0) {
        CS_DBG("CreditScheduler | No active users, skipping refill");
        return;
    }
    
    // 修正：降低補充節奏，按需分配而非大量預先分配
    double intervalSeconds = static_cast<double>(refillInterval) / 1000000000.0;
    uint64_t conservativeRate = 200;  // 保守的200 pages/sec基準
    uint64_t basePagesThisPeriod = static_cast<uint64_t>(conservativeRate * intervalSeconds);
    
    // 更保守的動態調整：只在需要時適度增加
    uint64_t totalPagesThisPeriod = static_cast<uint64_t>(basePagesThisPeriod * (0.5 + currentLoadFactor * 0.8));
    
    CS_DBG_MINIMAL(
               "CreditScheduler | SSD: %lu IOPS, load=%.2f, interval=%.3fs, base=%lu, allocated=%lu pages",
               PAGES_PER_SECOND, currentLoadFactor, intervalSeconds, basePagesThisPeriod, totalPagesThisPeriod);
    
    for (auto &kv : users) {
        uint32_t uid = kv.first;
        auto &account = kv.second;
        if (uid == 0) continue;  // 跳過admin用戶
        
        // 關鍵優化：如果用戶還沒有發送任何I/O請求，不分配credit
        if (account.totalConsumed == 0) {
            CS_DBG("CreditScheduler | User %u has no I/O activity (totalConsumed=0), skipping credit allocation", uid);
            continue;
        }
        
        // 保持 creditCap 固定，不進行動態調整避免混亂
        // uint64_t newCreditCap = calculateDynamicCreditCap(account.weight);
        // account.creditCap = newCreditCap;
        
        // 新增：基於使用率的智能補充機制
        double currentUsageRate = static_cast<double>(account.credit) / account.creditCap;
        
        // 基礎分配：按權重比例
        uint64_t baseCreditToAdd = (totalPagesThisPeriod * account.weight) / totalWeight;
        
        // 智能調整：credit使用率高時增加補充，使用率低時減少補充
        double demandFactor = 1.0;
        if (currentUsageRate < 0.2) {        // credit充足時減少補充
            demandFactor = 0.3;
        } else if (currentUsageRate > 0.7) { // credit不足時增加補充
            demandFactor = 1.5;
        }
        
        uint64_t creditToAdd = static_cast<uint64_t>(baseCreditToAdd * demandFactor);
        
        // 檢查是否長期未活動
        uint64_t inactivityTime = currentTick - account.lastActiveTime;
        if (inactivityTime > INACTIVITY_PENALTY_THRESHOLD) {
            creditToAdd /= 3;  // 長期未活動用戶補充大幅減少
            CS_DBG("CreditScheduler | User %u inactive for %lu ticks, reduced refill to %lu",
                   uid, inactivityTime, creditToAdd);
        }
        
        CS_DBG("CreditScheduler | User %u: usage=%.1f%%, demand=%.1fx, base=%lu, final=%lu",
               uid, currentUsageRate*100, demandFactor, baseCreditToAdd, creditToAdd);
        
        // 補充credit，但不超過動態上限
        uint64_t oldCredit = account.credit;
        account.credit = std::min(account.credit + creditToAdd, account.creditCap);
        uint64_t actualAdded = (account.credit >= oldCredit) ? (account.credit - oldCredit) : 0;
        
        if (account.credit < oldCredit) {
            CS_DBG("CreditScheduler | WARNING: Credit decreased during refill! uid=%u, old=%lu, new=%lu, creditToAdd=%lu",
                   uid, oldCredit, account.credit, creditToAdd);
        }
        
        CS_DBG_MINIMAL(
                   "CreditScheduler | User %u: weight=%u (%lu/%lu), credit=%lu/%lu (+%lu), consumed=%lu",
                   uid, account.weight, account.weight, totalWeight, account.credit, 
                   account.creditCap, actualAdded, account.totalConsumed);
    }
}

// 新增：定時中斷補充credit (1秒週期)
void CreditScheduler::refillCreditsTimer()
{
    CS_DBG_MINIMAL("CreditScheduler | Timer refill (1s tick) - weight-based delta_credit calculation");
    
    // 計算總權重 - 只計算有I/O活動的用戶
    uint64_t totalWeight = 0;
    for (auto &kv : users) {
        if (kv.first != 0 && kv.second.totalConsumed > 0) {  // 跳過admin用戶和無I/O活動的用戶
            totalWeight += kv.second.weight;
        }
    }
    
    if (totalWeight == 0) {
        CS_DBG("CreditScheduler | Timer refill: No active users, skipping");
        return;
    }
    
    // 修正：大幅降低補充速率，基於實際需求而非理論峰值
    // 從20,000 pages/sec降到更合理的500 pages/sec基準
    uint64_t basePagesPerSecond = 500;  // 大幅降低基準補充率
    
    // 根據當前負載調整每秒分配量
    updateLoadFactor();
    uint64_t adjustedPagesPerSecond = static_cast<uint64_t>(
        basePagesPerSecond * (0.3 + currentLoadFactor * 1.0)  // 30%-130%範圍，更保守
    );
    
    CS_DBG_MINIMAL("CreditScheduler | Timer refill: totalWeight=%lu, adjustedPages=%lu/sec, load=%.2f",
                   totalWeight, adjustedPagesPerSecond, currentLoadFactor);
    
    // 為每個用戶計算並累加 delta_credit
    for (auto &kv : users) {
        uint32_t uid = kv.first;
        auto &account = kv.second;
        if (uid == 0) continue;  // 跳過admin用戶
        
        // 關鍵優化：如果用戶還沒有發送任何I/O請求，不分配credit
        if (account.totalConsumed == 0) {
            CS_DBG("CreditScheduler | Timer refill: User %u has no I/O activity (totalConsumed=0), skipping credit allocation", uid);
            continue;
        }
        
        // 同步優化：基於使用率的智能補充
        double currentUsageRate = static_cast<double>(account.credit) / account.creditCap;
        uint64_t base_delta = (adjustedPagesPerSecond * account.weight) / totalWeight;
        
        // 智能調整因子
        double demandFactor = 1.0;
        if (currentUsageRate < 0.3) {        // credit充足
            demandFactor = 0.2;  // 大幅減少補充
        } else if (currentUsageRate > 0.8) { // credit不足
            demandFactor = 1.8;  // 增加補充
        }
        
        uint64_t delta_credit = static_cast<uint64_t>(base_delta * demandFactor);
        
        // 檢查長期未活動
        uint64_t inactivityTime = currentTick - account.lastActiveTime;
        if (inactivityTime > INACTIVITY_PENALTY_THRESHOLD) {
            delta_credit /= 4;  // 長期未活動用戶大幅減少
            CS_DBG("CreditScheduler | Timer refill: User %u inactive for %lu ticks, reduced delta_credit to %lu",
                   uid, inactivityTime, delta_credit);
        }
        
        // 累加到 user.credit (不超過creditCap)
        uint64_t oldCredit = account.credit;
        account.credit = std::min(account.credit + delta_credit, account.creditCap);
        uint64_t actualAdded = (account.credit >= oldCredit) ? (account.credit - oldCredit) : 0;
        
        if (account.credit < oldCredit) {
            CS_DBG("CreditScheduler | WARNING: Credit decreased during timer refill! uid=%u, old=%lu, new=%lu, delta_credit=%lu",
                   uid, oldCredit, account.credit, delta_credit);
        }
        
        CS_DBG_MINIMAL("CreditScheduler | Timer refill: User %u: weight=%u, delta_credit=%lu, credit=%lu/%lu (+%lu)",
                       uid, account.weight, delta_credit, account.credit, account.creditCap, actualAdded);
    }
}

bool CreditScheduler::canServeRequest(const Request& req, UserAccount& account) {
    uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // 特殊情況：如果用戶還沒有任何I/O活動，允許第一次請求（使用透支）
    if (account.totalConsumed == 0 && account.overdraftCredit >= pages) {
        CS_DBG("CreditScheduler | User %u first I/O request: allowing with overdraft, need=%lu, overdraft=%lu",
               req.userID, pages, account.overdraftCredit);
        return true;
    }
    
    // 優先使用正常credit
    if (account.credit >= pages) {
        return true;
    }
    
    // 如果正常credit不夠，檢查是否可以透支
    uint64_t totalAvailable = account.credit + account.overdraftCredit;
    if (totalAvailable >= pages) {
        CS_DBG(
                   "CreditScheduler | User %u using overdraft: need=%lu, credit=%lu, overdraft=%lu",
                   req.userID, pages, account.credit, account.overdraftCredit);
        return true;
    }
    
    return false;
}

void CreditScheduler::chargeCredit(uint32_t uid, uint64_t pages) {
    auto &account = getOrCreateUser(uid);
    
    if (account.credit >= pages) {
        account.credit -= pages;
    } else {
        // 使用透支
        uint64_t overdraftUsed = pages - account.credit;
        account.credit = 0;
        account.overdraftCredit = (account.overdraftCredit >= overdraftUsed) ? 
                                  account.overdraftCredit - overdraftUsed : 0;
        
        CS_DBG(
                   "CreditScheduler | User %u used overdraft: pages=%lu, overdraft_remaining=%lu",
                   uid, overdraftUsed, account.overdraftCredit);
    }
    
    // 如果這是用戶的第一次I/O請求，激活該用戶並給予初始credit
    if (account.totalConsumed == 0) {
        account.isActive = true;
        // 修正：大幅減少初始credit，只給最小可用量
        uint64_t initialCredit = std::min(static_cast<uint64_t>(10), account.creditCap / 10);  // 最多10 pages或10%
        account.credit += initialCredit;
        CS_DBG_KEY("CreditScheduler | User %u activated on first I/O: granted %lu initial credit, total=%lu", 
                   uid, initialCredit, account.credit);
    }
    
    account.totalConsumed += pages;
    account.lastActiveTime = currentTick;
}

uint32_t CreditScheduler::selectNextUser() {
    uint32_t bestUser = 0;
    double bestScore = -1.0;
    
    for (auto &kv : users) {
        uint32_t uid = kv.first;
        auto &account = kv.second;
        if (uid == 0 || account.readyQueue.empty()) continue;
        
        // 計算用戶服務優先級：權重 × 隊列長度 × 活躍度
        double queueFactor = static_cast<double>(account.readyQueue.size());
        double weightFactor = static_cast<double>(account.weight);
        double activityFactor = 1.0;
        
        // 長期未活動的用戶優先級降低
        uint64_t inactiveTime = currentTick - account.lastActiveTime;
        if (inactiveTime > INACTIVITY_PENALTY_THRESHOLD) {
            activityFactor = 0.5;
        }
        
        double score = weightFactor * queueFactor * activityFactor;
        
        if (score > bestScore) {
            bestScore = score;
            bestUser = uid;
        }
    }
    
    return bestUser;
}

void CreditScheduler::processOverdrafts() {
    for (auto &kv : users) {
        uint32_t uid = kv.first;
        auto &account = kv.second;
        if (uid == 0) continue;
        
        // 恢復透支額度（如果用戶有正常credit）
        uint64_t maxOverdraft = account.creditCap * DEFAULT_OVERDRAFT_RATIO;
        if (account.overdraftCredit < maxOverdraft && account.credit > 0) {
            uint64_t toRestore = std::min(account.credit / 4, maxOverdraft - account.overdraftCredit);
            account.overdraftCredit += toRestore;
            CS_DBG(
                       "CreditScheduler | Restored %lu overdraft credit for user %u",
                       toRestore, uid);
        }
    }
}

void CreditScheduler::drainPending() {
    CS_DBG("CreditScheduler | drainPending: pendingQueue size=%zu", pendingQueue.size());
    
    size_t moved = 0;
    size_t initialSize = pendingQueue.size();
    
    for (size_t i = 0; i < initialSize; ++i) {
        Request req = pendingQueue.front();
        pendingQueue.pop();
        
        // Admin請求直接移到admin queue
        if (req.userID == 0) {
            adminQueue.push(req);
            CS_DBG("CreditScheduler | Admin req %lu moved to admin queue", req.reqID);
            moved++;
            continue;
        }

        auto &account = getOrCreateUser(req.userID);
        
        if (canServeRequest(req, account)) {
            // 可以服務此請求 - 不在這裡扣除credit，在實際dispatch時才扣除
            uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
            account.readyQueue.push(req);
            moved++;
            
            CS_DBG(
                       "CreditScheduler | Moved req %lu (uid=%u, %lu pages) to ready queue, credit=%lu",
                       req.reqID, req.userID, pages, account.credit);
        } else {
            // 無法服務，放回pending queue
            pendingQueue.push(req);
            CS_DBG(
                       "CreditScheduler | Req %lu (uid=%u) still pending, insufficient credit",
                       req.reqID, req.userID);
        }
    }
    
    if (moved > 0) {
        CS_DBG("CreditScheduler | Moved %zu requests from pending to ready", moved);
    }
}

void CreditScheduler::submitRequest(Request &req) {
    // Admin請求直接進入admin queue，不受credit限制
    if (req.userID == 0) {
        adminQueue.push(req);
        CS_DBG("CreditScheduler | Admin req %lu submitted to admin queue", req.reqID);
        return;
    }

    auto &account = getOrCreateUser(req.userID);
    uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
    
    CS_DBG(
               "CreditScheduler | Submit req %lu: uid=%u, pages=%lu, current credit=%lu/%lu, consumed=%lu",
               req.reqID, req.userID, pages, account.credit, account.creditCap, account.totalConsumed);

    if (canServeRequest(req, account)) {
        // 立即服務請求 - 不在這裡扣除credit，在實際dispatch時才扣除
        account.readyQueue.push(req);
        
        CS_DBG(
                   "CreditScheduler | Accepted req %lu (uid=%u) to ready queue, credit=%lu",
                   req.reqID, req.userID, account.credit);
    } else {
        // 加入pending queue等待credit補充
        pendingQueue.push(req);
        CS_DBG(
                   "CreditScheduler | Queued req %lu (uid=%u) to pending, need=%lu have=%lu",
                   req.reqID, req.userID, pages, account.credit);
    }
}

void CreditScheduler::schedule() {
    // 0. 新增：早期credit重分配檢查 (在timer之前)
    checkEarlyRedistribution();
    
    // 1. 檢查定時中斷 (1秒週期) - weight-based delta_credit accumulation
    if (currentTick >= lastRefillTimerTick + refillTimerInterval) {
        refillCreditsTimer();
        lastRefillTimerTick = currentTick;
        CS_DBG_MINIMAL("CreditScheduler | Timer interrupt triggered at tick=%lu (next at %lu)", 
                       currentTick, lastRefillTimerTick + refillTimerInterval);
    }
    
    // 1. 優先處理admin請求
    if (!adminQueue.empty()) {
        Request req = adminQueue.front();
        adminQueue.pop();
        
        currentTick += applyLatency(CPU::CREDIT_SCHEDULER, CPU::SCHEDULE);
        ICL::Request iclReq(req);
        
        switch (req.op) {
            case OpType::READ:
                pICL->read(iclReq, currentTick);
                break;
            case OpType::WRITE:
                pICL->write(iclReq, currentTick);
                break;
            case OpType::CREDIT_ONLY:
                CS_DBG("CreditScheduler | Admin credit-only req granted at tick=%lu", currentTick);
                break;
            case OpType::ISC_RESULT:
                break;
            default:
                panic("Unknown OpType in admin request");
        }
        
        CS_DBG("CreditScheduler | Dispatched admin req %lu, op=%d, len=%lu",
                   req.reqID, static_cast<int>(req.op), req.length);
        return;
    }
    
    // 2. 選擇下一個要服務的用戶
    uint32_t selectedUser = selectNextUser();
    if (selectedUser == 0) {
        CS_DBG("CreditScheduler | No user requests available");
        return;
    }
    
    auto &account = getOrCreateUser(selectedUser);
    if (account.readyQueue.empty()) {
        CS_DBG("CreditScheduler | Selected user %u has empty queue", selectedUser);
        return;
    }
    
    // 3. 處理選中用戶的請求
    Request req = account.readyQueue.front();
    account.readyQueue.pop();
    
    // 關鍵修復：在實際派發請求時消耗credit
    uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
    chargeCredit(req.userID, pages);
    
    currentTick += applyLatency(CPU::CREDIT_SCHEDULER, CPU::SCHEDULE);
    ICL::Request iclReq(req);
    
    switch (req.op) {
        case OpType::READ:
            pICL->read(iclReq, currentTick);
            break;
        case OpType::WRITE:
            pICL->write(iclReq, currentTick);
            break;
        case OpType::CREDIT_ONLY:
            CS_DBG("CreditScheduler | Credit-only req for uid=%u granted", req.userID);
            break;
        case OpType::ISC_RESULT:
            break;
        default:
            panic("Unknown OpType in user request");
    }
    
    CS_DBG(
               "CreditScheduler | Dispatched req %lu: uid=%u (weight=%u), op=%d, len=%lu, pages=%lu, remaining_credit=%lu, remaining_queue=%zu",
               req.reqID, req.userID, account.weight, static_cast<int>(req.op), 
               req.length, pages, account.credit, account.readyQueue.size());
}

void CreditScheduler::tick(uint64_t now) {
    currentTick = now;
    
    CS_DBG("CreditScheduler | tick: now=%lu nextRefillTick=%lu", now, nextRefillTick);
    
    // 0. 新增：每100ms補充emergency reserve
    if (currentTick >= lastReserveRefill + 100000000ULL) {  // 100ms
        refillEmergencyReserve();
        lastReserveRefill = currentTick;
    }
    
    // 1. 檢查是否需要補充credit
    if (now >= nextRefillTick) {
        CS_DBG("CreditScheduler | time to refill credits");
        refillCredits();
        processOverdrafts();
        nextRefillTick = now + refillInterval;
        CS_DBG("CreditScheduler | next refill at %lu", nextRefillTick);
    }
    
    // 2. 嘗試將pending requests移到ready queue
    drainPending();
    
    // 3. 調度一個請求
    schedule();
    
    // 4. 顯示系統狀態 (只在有pending或低credit時顯示)
    bool shouldShowStatus = !pendingQueue.empty() || !adminQueue.empty();
    for (const auto &kv : users) {
        if (kv.first != 0 && (kv.second.credit < kv.second.creditCap / 4 || !kv.second.readyQueue.empty())) {
            shouldShowStatus = true;
            break;
        }
    }
    
    if (shouldShowStatus) {
        CS_DBG_MINIMAL("CreditScheduler | System status:");
        for (const auto &kv : users) {
            uint32_t uid = kv.first;
            const auto &account = kv.second;
            if (uid != 0) {
                double usageRate = (double)account.credit / account.creditCap * 100.0;
                CS_DBG_MINIMAL("  uid=%u weight=%u credit=%lu/%lu (%.1f%%) readyQueue=%zu consumed=%lu", 
                           uid, account.weight, account.credit, account.creditCap, usageRate,
                           account.readyQueue.size(), account.totalConsumed);
            }
        }
        CS_DBG_MINIMAL("  adminQueue=%zu pendingQueue=%zu", adminQueue.size(), pendingQueue.size());
    }
}

// 統計相關方法
void CreditScheduler::getStatList(std::vector<Stats> &list, std::string prefix) {
    for (auto &uid : statsUsers) {
        getOrCreateUser(uid);
        Stats s;
        s.name = prefix + "credit.user" + std::to_string(uid) + ".consumed";
        s.desc = "Pages consumed by uid " + std::to_string(uid);
        list.push_back(s);
        
        s.name = prefix + "credit.user" + std::to_string(uid) + ".credit";
        s.desc = "Current credit for uid " + std::to_string(uid);
        list.push_back(s);
    }
    
    Stats t;
    t.name = prefix + "credit.total_consumed";
    t.desc = "Total pages consumed by all users";
    list.push_back(t);
    
    t.name = prefix + "credit.pending_requests";
    t.desc = "Number of pending requests";
    list.push_back(t);
    
    t.name = prefix + "credit.ready_requests";
    t.desc = "Number of ready requests";
    list.push_back(t);
    
    // 早期重分配統計
    t.name = prefix + "credit.early_redistributions";
    t.desc = "Total number of early credit redistributions";
    list.push_back(t);
    
    t.name = prefix + "credit.emergency_reserve";
    t.desc = "Current emergency reserve size (pages)";
    list.push_back(t);
}

void CreditScheduler::getStatValues(std::vector<double> &val) {
    uint64_t totalConsumed = 0;
    uint64_t totalReady = 0;
    
    for (uint32_t uid : statsUsers) {
        auto it = users.find(uid);
        if (it != users.end()) {
            val.push_back(static_cast<double>(it->second.totalConsumed));
            val.push_back(static_cast<double>(it->second.credit));
            totalConsumed += it->second.totalConsumed;
            totalReady += it->second.readyQueue.size();
        } else {
            val.push_back(0.0);
            val.push_back(0.0);
        }
    }
    
    val.push_back(static_cast<double>(totalConsumed));
    val.push_back(static_cast<double>(pendingQueue.size()));
    val.push_back(static_cast<double>(totalReady));
    
    // 早期重分配統計值
    val.push_back(static_cast<double>(earlyRedistributionCount));
    val.push_back(static_cast<double>(emergencyReserve));
}

void CreditScheduler::resetStatValues() {
    for (auto &kv : users) {
        auto &account = kv.second;
        account.credit = 0;  // 修復：新用戶不給初始credit
        account.totalConsumed = 0;
        account.lastActiveTime = currentTick;
        account.overdraftCredit = account.creditCap * DEFAULT_OVERDRAFT_RATIO;
        
        // 清空queues
        while (!account.readyQueue.empty()) {
            account.readyQueue.pop();
        }
    }
    
    while (!pendingQueue.empty()) pendingQueue.pop();
    while (!adminQueue.empty()) adminQueue.pop();
    
    // 重置早期重分配統計
    earlyRedistributionCount = 0;
    lastEarlyRedistribution = 0;
    emergencyReserve = PAGES_PER_SECOND / 4;  // 重置為初始值
    
    CS_DBG("CreditScheduler | Statistics reset");
}

bool CreditScheduler::pendingForUser(uint32_t uid) const {
    std::queue<Request> q = pendingQueue;
    while (!q.empty()) {
        if (q.front().userID == uid) return true;
        q.pop();
    }
    return false;
}

bool CreditScheduler::checkCredit(uint32_t uid, size_t need) const {
    auto it = users.find(uid);
    if (it == users.end()) {
        return baseCreditCap >= need;  // 新用戶用基礎cap檢查
    }
    
    uint64_t totalAvailable = it->second.credit + it->second.overdraftCredit;
    return totalAvailable >= need;
}

void CreditScheduler::useCredit(uint32_t uid, size_t used) {
    chargeCredit(uid, used);
}

// 新增的ISC相關接口
void CreditScheduler::chargeUserCredit(uint32_t uid, uint64_t pages) {
    chargeCredit(uid, pages);
    CS_DBG("CreditScheduler | ISC charged %lu pages to user %u", pages, uid);
}

uint64_t CreditScheduler::getUserCredit(uint32_t uid) const {
    auto it = users.find(uid);
    return (it != users.end()) ? it->second.credit : baseCreditCap;
}

uint64_t CreditScheduler::getUserWeight(uint32_t uid) const {
    auto it = users.find(uid);
    return (it != users.end()) ? it->second.weight : 1;
}

void CreditScheduler::updateLoadFactor() {
    // 計算當前總 pending requests
    uint64_t totalPending = pendingQueue.size();
    for (auto &kv : users) {
        if (kv.first != 0) {  // 跳過admin
            totalPending += kv.second.readyQueue.size();
        }
    }
    
    // 根據 pending 數量計算負載係數
    if (totalPending <= pendingThresholdLow) {
        currentLoadFactor = 0.1;  // 輕負載
    } else if (totalPending >= pendingThresholdHigh) {
        currentLoadFactor = 1.0;  // 重負載
    } else {
        // 線性插值：從 0.1 到 1.0
        double ratio = static_cast<double>(totalPending - pendingThresholdLow) / 
                      (pendingThresholdHigh - pendingThresholdLow);
        currentLoadFactor = 0.1 + ratio * 0.9;
    }
    
    CS_DBG_MINIMAL("CreditScheduler | Load factor: %.2f (pending=%lu)", currentLoadFactor, totalPending);
}

uint64_t CreditScheduler::calculateDynamicCreditCap(uint32_t weight) {
    // 基礎 cap
    uint64_t baseCap = baseCreditCap * weight;
    
    // 根據負載係數動態調整
    // 輕負載時：使用較小的 cap 防止過度累積
    // 重負載時：使用較大的 cap 確保充足資源
    uint64_t dynamicCap = static_cast<uint64_t>(baseCap * (0.5 + currentLoadFactor * 1.5));
    
    // 確保最小值不低於基礎值的一半
    dynamicCap = std::max(dynamicCap, baseCap / 2);
    
    CS_DBG("CreditScheduler | Dynamic cap for weight %u: base=%lu, factor=%.2f, result=%lu",
           weight, baseCap, currentLoadFactor, dynamicCap);
    
    return dynamicCap;
}

// 早期重分配核心實現方法
void CreditScheduler::checkEarlyRedistribution() {
    // 防止過於頻繁的重分配 (最少250ms間隔)
    uint64_t minInterval = 250000000ULL;  // 250ms
    if (currentTick - lastEarlyRedistribution < minInterval) {
        return;
    }
    
    updateDepletionState();
    
    if (shouldTriggerEarlyRedistribution()) {
        double urgency = calculateRedistributionUrgency();
        
        CS_DBG_MINIMAL("CreditScheduler | Early redistribution triggered: urgency=%.2f, depleted=%u/%u",
                       urgency, depletionState.completelyDepletedUsers, depletionState.totalActiveUsers);
        
        distributeEmergencyCredits();
        lastEarlyRedistribution = currentTick;
        earlyRedistributionCount++;
    }
}

void CreditScheduler::updateDepletionState() {
    depletionState.totalActiveUsers = 0;
    depletionState.completelyDepletedUsers = 0;
    depletionState.totalPendingPages = 0;
    
    // 計算活躍用戶和耗盡用戶數
    for (const auto &kv : users) {
        if (kv.first == 0) continue;  // 跳過admin
        
        const auto &account = kv.second;
        depletionState.totalActiveUsers++;
        
        // 檢查是否完全耗盡 (credit + overdraft 都沒有)
        if (account.credit == 0 && account.overdraftCredit == 0) {
            depletionState.completelyDepletedUsers++;
        }
    }
    
    // 計算總待處理頁數
    auto temp = pendingQueue;
    while (!temp.empty()) {
        Request req = temp.front();
        temp.pop();
        if (req.userID != 0) {  // 跳過admin請求
            uint64_t pages = (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
            depletionState.totalPendingPages += pages;
        }
    }
    
    // 計算耗盡比例
    if (depletionState.totalActiveUsers > 0) {
        depletionState.depletionRatio = static_cast<double>(depletionState.completelyDepletedUsers) / 
                                       depletionState.totalActiveUsers;
    } else {
        depletionState.depletionRatio = 0.0;
    }
}

bool CreditScheduler::shouldTriggerEarlyRedistribution() {
    // 主要條件：80%以上用戶完全耗盡
    if (depletionState.depletionRatio < 0.8) {
        return false;
    }
    
    // 次要條件：有足夠的待處理請求
    if (depletionState.totalPendingPages < 10) {
        return false;
    }
    
    // 時機條件：距離上次timer refill至少250ms
    uint64_t timeSinceRefill = currentTick - lastRefillTimerTick;
    if (timeSinceRefill < 250000000ULL) {  // 250ms
        return false;
    }
    
    // 緊急條件：待處理頁數過多或高負載
    if (depletionState.totalPendingPages >= 100 || currentLoadFactor >= 0.9) {
        return true;  // 立即重分配
    }
    
    // 標準條件：所有條件都滿足
    return true;
}

double CreditScheduler::calculateRedistributionUrgency() {
    // 基於四個因素計算緊急程度
    double depletionUrgency = depletionState.depletionRatio;  // 0.0-1.0
    double loadUrgency = currentLoadFactor;                   // 0.0-1.0
    
    // 隊列深度緊急度 (標準化到20個請求)
    double queueUrgency = std::min(static_cast<double>(depletionState.totalPendingPages) / 100.0, 1.0);
    
    // 時間緊急度 (距離上次refill的時間)
    uint64_t timeSinceRefill = currentTick - lastRefillTimerTick;
    double timeUrgency = std::min(static_cast<double>(timeSinceRefill) / 500000000.0, 1.0);  // 500ms max
    
    // 加權計算總緊急度
    return (depletionUrgency * 0.4) + (loadUrgency * 0.3) + (queueUrgency * 0.2) + (timeUrgency * 0.1);
}

void CreditScheduler::distributeEmergencyCredits() {
    if (emergencyReserve < 10) {  // 最少需要10 pages才能分配
        CS_DBG("CreditScheduler | Emergency reserve too low: %lu pages", emergencyReserve);
        return;
    }
    
    double urgency = calculateRedistributionUrgency();
    
    // 根據緊急程度決定分配比例
    double distributionRatio;
    if (urgency >= 0.8) {
        distributionRatio = 1.0;  // 分配100%的reserve
    } else if (urgency >= 0.6) {
        distributionRatio = 0.75; // 分配75%的reserve
    } else if (urgency >= 0.3) {
        distributionRatio = 0.5;  // 分配50%的reserve
    } else {
        distributionRatio = 0.25; // 分配25%的reserve
    }
    
    uint64_t totalToDistribute = static_cast<uint64_t>(emergencyReserve * distributionRatio);
    
    // 確保不超過reserve的80%
    totalToDistribute = std::min(totalToDistribute, static_cast<uint64_t>(emergencyReserve * 0.8));
    
    if (totalToDistribute < 10) {  // 最少10 pages
        return;
    }
    
    CS_DBG_MINIMAL("CreditScheduler | Emergency distribution: urgency=%.2f, distributing=%lu/%lu pages",
                   urgency, totalToDistribute, emergencyReserve);
    
    // 計算總權重
    uint64_t totalWeight = 0;
    for (const auto &kv : users) {
        if (kv.first != 0) {  // 跳過admin
            totalWeight += kv.second.weight;
        }
    }
    
    if (totalWeight == 0) return;
    
    // 分配給每個用戶
    uint64_t totalDistributed = 0;
    for (auto &kv : users) {
        uint32_t uid = kv.first;
        auto &account = kv.second;
        if (uid == 0) continue;  // 跳過admin
        
        uint64_t allocation = calculateEmergencyAllocation(uid, totalToDistribute);
        
        if (allocation > 0) {
            account.credit += allocation;
            totalDistributed += allocation;
            
            CS_DBG("CreditScheduler | Emergency allocation: uid=%u, allocated=%lu, new_credit=%lu",
                   uid, allocation, account.credit);
        }
    }
    
    // 從reserve中扣除
    emergencyReserve -= totalDistributed;
    
    CS_DBG_MINIMAL("CreditScheduler | Emergency distribution complete: distributed=%lu, reserve_remaining=%lu",
                   totalDistributed, emergencyReserve);
}

uint64_t CreditScheduler::calculateEmergencyAllocation(uint32_t uid, uint64_t totalPool) {
    auto &account = getOrCreateUser(uid);
    
    // 基礎分配：基於權重比例
    uint64_t totalWeight = 0;
    for (const auto &kv : users) {
        if (kv.first != 0) totalWeight += kv.second.weight;
    }
    
    if (totalWeight == 0) return 0;
    
    double baseRatio = static_cast<double>(account.weight) / totalWeight;
    
    // 需求因子：基於該用戶的待處理請求
    double needFactor = 1.0;
    uint64_t userPendingPages = 0;
    auto temp = pendingQueue;
    while (!temp.empty()) {
        Request req = temp.front();
        temp.pop();
        if (req.userID == uid) {
            userPendingPages += (req.length + PAGE_SIZE - 1) / PAGE_SIZE;
        }
    }
    
    if (userPendingPages > 0) {
        needFactor = 1.0 + std::min(static_cast<double>(userPendingPages) / 50.0, 1.0);  // 最多增加100%
    }
    
    // 計算最終分配
    uint64_t allocation = static_cast<uint64_t>(totalPool * baseRatio * needFactor);
    
    // 應用限制
    uint64_t minAllocation = 10;  // 最少10 pages
    uint64_t maxAllocation = totalPool / 2;  // 最多50%
    
    return std::min<uint64_t>( maxAllocation,
           std::max<uint64_t>( allocation, minAllocation ) );
}

void CreditScheduler::refillEmergencyReserve() {
    if (emergencyReserve >= maxEmergencyReserve) {
        return;  // 已滿
    }
    
    // 每100ms補充reserve容量的10%
    uint64_t refillAmount = maxEmergencyReserve / 10;  // 10%
    
    emergencyReserve = std::min(emergencyReserve + refillAmount, maxEmergencyReserve);
    
    CS_DBG("CreditScheduler | Emergency reserve refilled: +%lu, total=%lu/%lu",
           refillAmount, emergencyReserve, maxEmergencyReserve);
}

}  // namespace HIL
}  // namespace SimpleSSD