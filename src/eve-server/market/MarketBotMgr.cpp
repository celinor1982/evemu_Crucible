/**
  * @name MarketBotMgr.h
  *   system for automating/emulating buy and sell orders on the market.
  * idea and some code taken from AuctionHouseBot - Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
  * @Author:         Allan
  * @date:   10 August 2016
  * @version:  0.15 (config version)
  */

#include "eve-server.h"
#include "EVEServerConfig.h"
#include "market/MarketBotConf.h"
#include "market/MarketBotMgr.h"
#include "market/MarketMgr.h"
#include "market/MarketProxyService.h"
#include "market/MarketDB.h"
#include "inventory/ItemType.h"
#include "inventory/ItemFactory.h"
#include "inventory/InventoryItem.h"
#include "station/StationDataMgr.h"
#include "system/SystemManager.h"
#include "system/SystemEntity.h"
#include <random>
#include <cstdint>

extern SystemManager* sSystemMgr;

static const uint32 MARKETBOT_MAX_ITEM_ID = 30000;
static const std::vector<uint32> VALID_GROUPS = {
    18, 19, 20, 53, 55, 63, 70, 83, 85, 104, 108, 255
};

static const char* const BOT_CONFIG_FILE = "/src/utils/config/MarketBot.xml";

static constexpr uint32 BOT_OWNER_ID = 1000125; // NPC corp owner, default CONCORD

// helper random generators
int GetRandomInt(int min, int max) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

float GetRandomFloat(float min, float max) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng);
}

MarketBotDataMgr::MarketBotDataMgr() {
    m_initalized = false;
}

int MarketBotDataMgr::Initialize() {
    m_initalized = true;
    sLog.Blue(" MarketBotDataMgr", "Market Bot Data Manager Initialized."); // load current data
    return 1;
}

MarketBotMgr::MarketBotMgr()
/*: m_updateTimer(20 * 60 * 1000)*/ { // default 20 minutes
    m_initalized = false;
}

int MarketBotMgr::Initialize() {
    if (!sMBotConf.ParseFile(BOT_CONFIG_FILE)) {
        sLog.Error("       ServerInit", "Loading Market Bot Config file '%s' failed.", BOT_CONFIG_FILE);
        return 0;
    }

    m_initalized = true;
    sMktBotDataMgr.Initialize();

    // START automation timer immediately
    /*uint32_t delay_ms = sMBotConf.main.DataRefreshTime * 60 * 1000;*/
    m_updateTimer.Disable();
    m_updateTimer.Start(0); // () set to delay_ms

    std::printf("[MarketBot] Initialized — automation will trigger on next tick (Start(0) used)\n");
    std::fflush(stdout);

    /*std::printf("[MarketBot] Initialized with timer set to %u ms (%d min)\n", delay_ms, sMBotConf.main.DataRefreshTime);
    std::fflush(stdout);*/

    sLog.Blue("     MarketBotMgr", "Market Bot Manager Initialized.");
    return 1;
}

void MarketBotMgr::ForceRun() {
    std::printf("[MarketBot] ForceRun entered\n");
    std::fflush(stdout);

    if (!m_initalized) {
        std::printf("[MarketBot] MarketBotMgr not initialized — skipping run\n");
        std::fflush(stdout);
        return;
    }

    std::printf("[MarketBot] Running Process() now...\n");
    std::fflush(stdout);

    this->Process(true);  // force override

    std::printf("[MarketBot] Finished Process()\n");
    std::fflush(stdout);

    // Reset the timer for the next normal cycle
    m_updateTimer.Start(sMBotConf.main.DataRefreshTime * 60 * 1000);
}

// Called on minute tick from EntityList
void MarketBotMgr::Process(bool overrideTimer) {
    bool timerReady = m_updateTimer.Check();

    std::printf("[MarketBot] MarketBot Process() invoked on tick. Timer ready = %s\n", m_updateTimer.Check() ? "true" : "false");
    std::fflush(stdout);
    _log(MARKET__TRACE, "MarketBot Process() invoked on tick. Timer ready = %s", m_updateTimer.Check() ? "true" : "false");

    std::printf("[MarketBot] Entered MarketBotMgr::Process()\n");
    std::fflush(stdout);
    _log(MARKET__TRACE, ">> Entered MarketBotMgr::Process()");

    if (!m_initalized) {
        std::printf("[MarketBot] MarketBotMgr not initialized — skipping run\n");
        std::fflush(stdout);
        _log(MARKET__ERROR, "Process() called but MarketBotMgr is not initialized.");
        return;
    }

    if (!overrideTimer && !m_updateTimer.Check()) {
        std::printf("[MarketBot] Update timer not ready yet.\n");
        std::fflush(stdout);
        _log(MARKET__TRACE, "Update timer not ready yet.");
        return;
    }
    
    std::printf("[MarketBot] Processing old orders...\n");
    std::fflush(stdout);
    _log(MARKET__TRACE, "Processing old orders...");
    ExpireOldOrders();

    std::vector<uint32> eligibleSystems = GetEligibleSystems();
    std::printf("[MarketBot] Found %zu eligible systems for order placement.\n", eligibleSystems.size());
    std::fflush(stdout);
    _log(MARKET__TRACE, "Found %zu eligible systems for order placement.", eligibleSystems.size());

    int totalBuyOrders = 0;
    int totalSellOrders = 0;
    int expiredOrders = ExpireOldOrders();

    for (uint32 systemID : eligibleSystems) {
        std::printf("[MarketBot] Placing orders in systemID: %u\n", systemID);
        std::fflush(stdout);
        _log(MARKET__TRACE, "Placing orders in systemID: %u", systemID);

        totalBuyOrders += PlaceBuyOrders(systemID);
        totalSellOrders += PlaceSellOrders(systemID);
    }

    std::printf("[MarketBot] Master Summary: Created %d buy orders and %d sell orders across %zu systems. Removed %d old orders.\n",
        totalBuyOrders, totalSellOrders, eligibleSystems.size(), expiredOrders);
    std::fflush(stdout);

    _log(MARKET__TRACE, "MarketBot Master Summary: Created %d buy orders and %d sell orders across %zu systems. Removed %d old orders.",
        totalBuyOrders, totalSellOrders, eligibleSystems.size(), expiredOrders);

    std::printf("[MarketBot] Cycle complete. Resetting timer.\n");
    std::fflush(stdout);
    _log(MARKET__TRACE, "MarketBot cycle complete. Resetting timer.");
    m_updateTimer.Start(sMBotConf.main.DataRefreshTime * 60 * 1000);
}

void MarketBotMgr::AddSystem() { /* To be implemented if needed */ }
void MarketBotMgr::RemoveSystem() { /* To be implemented if needed */ }

int MarketBotMgr::ExpireOldOrders() {
    uint64_t now = GetFileTimeNow();

    DBQueryResult res;
    DBResultRow row;

    int expiredCount = 0;

    if (!sDatabase.RunQuery(res,
        "SELECT orderID FROM mktOrders WHERE issued + (duration * 86400000000) < %" PRIu64 " AND ownerID = %u",
        now, BOT_OWNER_ID)) {
        std::printf("[MarketBot] Failed to query expired bot orders.\n");
        std::fflush(stdout);
        _log(MARKET__DB_ERROR, "Failed to query expired bot orders.");
        return 0;
    }

    while (res.GetRow(row)) {
        uint32 orderID = row.GetUInt(0);
        MarketDB::DeleteOrder(orderID);
        ++expiredCount;
        std::printf("[MarketBot] Expired MarketBot order %u\n", orderID);
        std::fflush(stdout);
        _log(MARKET__TRACE, "Expired MarketBot order %u", orderID);
    }

    return expiredCount;
}

int MarketBotMgr::PlaceBuyOrders(uint32 systemID) {
    SystemData sysData;
    if (!sDataMgr.GetSystemData(systemID, sysData)) {
        std::printf("[MarketBot] Failed to get system data for system %u\n", systemID);
        std::fflush(stdout);
        _log(MARKET__ERROR, "Failed to get system data for system %u", systemID);
        return 0;
    }

    std::vector<uint32> availableStations;
    if (!sDataMgr.GetStationListForSystem(systemID, availableStations)) {
        std::printf("[MarketBot] No stations found for system %u\n", systemID);
        std::fflush(stdout);
        _log(MARKET__ERROR, "No stations found for system %u", systemID);
        return 0;
    }

    size_t stationCount = availableStations.size();
    size_t stationLimit = stationCount;
    std::shuffle(availableStations.begin(), availableStations.end(), std::mt19937{std::random_device{}()});

    int orderCount = 0;

    for (size_t i = 0; i < std::min<size_t>(stationLimit, sMBotConf.main.OrdersPerRefresh); ++i) {
        uint32 stationID = availableStations[i];
        uint32 itemID = SelectRandomItemID();
        const ItemType* type = sItemFactory.GetType(itemID);
        if (!type) continue;

        uint32 quantity = GetRandomQuantity(type->groupID());
        double price = CalculateBuyPrice(itemID);

        if (price * quantity > sMBotConf.main.MaxISKPerOrder) {
            if (quantity > 1) {
                quantity = 1;
                if (price > sMBotConf.main.MaxISKPerOrder) {
                    std::printf("[MarketBot] Skipping itemID %u due to price %.2f ISK exceeding MaxISKPerOrder.\n", itemID, price);
                    std::fflush(stdout);
                    _log(MARKET__TRACE, "Skipping itemID %u due to price %.2f ISK exceeding MaxISKPerOrder.", itemID, price);
                    continue;
                }
                std::printf("[MarketBot] Price too high for bulk, retrying with quantity = 1 for itemID %u.\n", itemID);
                std::fflush(stdout);
                _log(MARKET__TRACE, "Price too high for bulk, retrying with quantity = 1 for itemID %u", itemID);
            } else {
                std::printf("[MarketBot] Skipping itemID %u even at quantity = 1 due to price %.2f ISK.\n", itemID, price);
                std::fflush(stdout);
                _log(MARKET__TRACE, "Skipping itemID %u even at quantity = 1 due to price %.2f ISK", itemID, price);
                continue;
            }
        }

        Market::SaveData order;
        order.typeID = itemID;
        order.regionID = sysData.regionID;
        order.stationID = stationID;
        order.solarSystemID = systemID;
        order.volEntered = quantity;
        order.volRemaining = quantity;
        order.price = price;
        order.duration = sMBotConf.main.OrderLifetime;
        order.bid = true;
        order.issued = GetFileTimeNow();
        order.isCorp = false;
        order.ownerID = BOT_OWNER_ID;

        bool success = MarketDB::StoreOrder(order);
        if (success) {
            ++orderCount;
            std::printf("[MarketBot] %s order created for typeID %u, qty %u, price %.2f ISK, station %u\n",
                (order.bid ? "BUY" : "SELL"), order.typeID, order.volEntered, order.price, order.stationID);
            std::fflush(stdout);
            _log(MARKET__TRACE, "%s order created for typeID %u, qty %u, price %.2f ISK, station %u",
                (order.bid ? "BUY" : "SELL"), order.typeID, order.volEntered, order.price, order.stationID);
        } else {
            std::printf("[MarketBot] Failed to store %s order for typeID %u at station %u\n",
                (order.bid ? "BUY" : "SELL"), order.typeID, order.stationID);
            std::fflush(stdout);
            _log(MARKET__ERROR, "Failed to store %s order for typeID %u at station %u",
                (order.bid ? "BUY" : "SELL"), order.typeID, order.stationID);
        }
    }
    std::printf("[MarketBot] Created %d buy orders for system %u\n", orderCount, systemID);
    std::fflush(stdout);
    return orderCount;
}

int MarketBotMgr::PlaceSellOrders(uint32 systemID) {
    SystemData sysData;
    if (!sDataMgr.GetSystemData(systemID, sysData)) {
        std::printf("[MarketBot] Failed to get system data for system %u\n", systemID);
        std::fflush(stdout);
        _log(MARKET__ERROR, "MarketBot: Failed to get system data for system %u", systemID);
        return 0;
    }

    std::vector<uint32> availableStations;

    if (!sDataMgr.GetStationListForSystem(systemID, availableStations)) {
        std::printf("[MarketBot] No stations found for system %u — skipping order creation.\n", systemID);
        std::fflush(stdout);
        _log(MARKET__ERROR, "MarketBot: No stations found for system %u — skipping order creation.", systemID);
        return 0;
    } else {
        std::printf("[MarketBot] Found %zu stations in system %u\n", availableStations.size(), systemID);
        std::fflush(stdout);
        _log(MARKET__TRACE, "MarketBot: Found %zu stations in system %u", availableStations.size(), systemID);
    }

    size_t stationCount = availableStations.size();
    size_t stationLimit = stationCount;
    std::shuffle(availableStations.begin(), availableStations.end(), std::mt19937{std::random_device{}()});

    int orderCount = 0;

    for (size_t i = 0; i < std::min<size_t>(stationLimit, sMBotConf.main.OrdersPerRefresh); ++i) {
        uint32 stationID = availableStations[i];
        uint32 itemID = SelectRandomItemID();
        const ItemType* type = sItemFactory.GetType(itemID);
        if (!type) continue;

        uint32 quantity = GetRandomQuantity(type->groupID());
        double price = CalculateSellPrice(itemID);

        if (price * quantity > sMBotConf.main.MaxISKPerOrder) {
            if (quantity > 1) {
                quantity = 1;
                if (price > sMBotConf.main.MaxISKPerOrder) {
                    std::printf("[MarketBot] Skipping itemID %u due to price %.2f ISK exceeding MaxISKPerOrder.\n", itemID, price);
                    std::fflush(stdout);
                    _log(MARKET__TRACE, "Skipping itemID %u due to price %.2f ISK exceeding MaxISKPerOrder.", itemID, price);
                    continue;
                }
                std::printf("[MarketBot] Price too high for bulk, retrying with quantity = 1 for itemID %u.\n", itemID);
                std::fflush(stdout);
                _log(MARKET__TRACE, "Price too high for bulk, retrying with quantity = 1 for itemID %u", itemID);
            } else {
                std::printf("[MarketBot] Skipping itemID %u even at quantity = 1 due to price %.2f ISK.\n", itemID, price);
                std::fflush(stdout);
                _log(MARKET__TRACE, "Skipping itemID %u even at quantity = 1 due to price %.2f ISK", itemID, price);
                continue;
            }
        }

        Market::SaveData order;
        order.typeID = itemID;
        order.regionID = sysData.regionID;
        order.stationID = stationID;
        order.solarSystemID = systemID;
        order.volEntered = quantity;
        order.volRemaining = quantity;
        order.price = price;
        order.duration = sMBotConf.main.OrderLifetime;
        order.bid = false;
        order.issued = GetFileTimeNow();
        order.isCorp = false;
        order.ownerID = BOT_OWNER_ID;
        order.orderRange = -1; // -1 station, 0 solarsystem, 1-5 10 20 30 40 jumps, 32767 region

        std::printf("[MarketBot] System %u maps to region %u via GetSystemData.\n", systemID, sysData.regionID);
        std::fflush(stdout);
        _log(MARKET__TRACE, "System %u maps to region %u via GetSystemData", systemID, sysData.regionID);

        bool success = MarketDB::StoreOrder(order);
        if (success) {
            ++orderCount;
            std::printf("[MarketBot] %s order created for typeID %u, qty %u, price %.2f ISK, station %u\n",
                (order.bid ? "BUY" : "SELL"), order.typeID, order.volEntered, order.price, order.stationID);
            std::fflush(stdout);
            _log(MARKET__TRACE, "MarketBot: Creating %s order for typeID %u, qty %u, price %.2f, station %u, region %u",
                (order.bid ? "BUY" : "SELL"), order.typeID, order.volEntered, order.price, order.stationID, order.regionID);
        } else {
            std::printf("[MarketBot] Failed to store %s order for typeID %u at station %u\n",
                (order.bid ? "BUY" : "SELL"), order.typeID, order.stationID);
            std::fflush(stdout);
            _log(MARKET__ERROR, "MarketBot: Failed to store %s order for typeID %u at station %u",
                (order.bid ? "BUY" : "SELL"), order.typeID, order.stationID);
        }
    }
    std::printf("[MarketBot] Created %d sell orders for system %u\n", orderCount, systemID);
    std::fflush(stdout);
    return orderCount;
}

std::vector<uint32> MarketBotMgr::GetEligibleSystems() {
    bool useStaticSystems = false;
    if (useStaticSystems) {
        return { 30000142 };  // Jita
    }

    std::vector<uint32> systemIDs;
    sDataMgr.GetRandomSystemIDs(5, systemIDs); // pulls a randomized list of systems
    std::printf("[MarketBot] GetEligibleSystems(): Pulled %zu systems from StaticDataMgr\n", systemIDs.size());
    std::fflush(stdout);
    _log(MARKET__TRACE, "GetEligibleSystems(): Pulled %zu systems from StaticDataMgr", systemIDs.size());
    return systemIDs;
}

uint32 MarketBotMgr::SelectRandomItemID() {
    uint32 itemID = 0;
    const ItemType* type = nullptr;
    uint32 tries = 0;

    do {
        ++tries;
        itemID = GetRandomInt(10, MARKETBOT_MAX_ITEM_ID);
        type = sItemFactory.GetType(itemID);

        std::printf("[MarketBot] Selected itemID %u after %u attempts.\n", itemID, tries);
        std::fflush(stdout);
        if (type && std::find(VALID_GROUPS.begin(), VALID_GROUPS.end(), type->groupID()) != VALID_GROUPS.end()) {
            _log(MARKET__TRACE, "Selected itemID %u after %u attempts", itemID, tries);
            return itemID;
        }
    } while (tries < 50);

    // If we fail after 50 attempts, log a warning and return fallback value
    std::printf("[MarketBot] Failed to select valid itemID after %u attempts. Returning fallback itemID = 34 (Tritanium)\n", tries);
    std::fflush(stdout);
    _log(MARKET__WARNING, "Failed to select valid itemID after %u attempts. Returning fallback itemID = 34 (Tritanium)", tries);
    return 34;  // Tritanium, as a safe default
}

uint32 MarketBotMgr::GetRandomQuantity(uint32 groupID) {
    if (groupID == 18 || groupID == 20 || groupID == 53 || groupID == 104) {
        return GetRandomInt(1000, 1000000);
    }
    if (groupID == 55 || groupID == 63 || groupID == 70) {
        return GetRandomInt(10, 100);
    }
    return GetRandomInt(1, 5);
}

double MarketBotMgr::CalculateBuyPrice(uint32 itemID) {
    const ItemType* type = sItemFactory.GetType(itemID);
    return type ? type->basePrice() * GetRandomFloat(0.8f, 1.1f) : 1000.0;
}

double MarketBotMgr::CalculateSellPrice(uint32 itemID) {
    const ItemType* type = sItemFactory.GetType(itemID);
    return type ? type->basePrice() * GetRandomFloat(1.0f, 1.3f) : 1000.0;
}
