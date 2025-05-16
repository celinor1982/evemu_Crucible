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
: m_updateTimer(20 * 60 * 1000) { // default 20 minutes
    m_initalized = false;
}

int MarketBotMgr::Initialize() {
    if (!sMBotConf.ParseFile(BOT_CONFIG_FILE)) {
        sLog.Error("       ServerInit", "Loading Market Bot Config file '%s' failed.", BOT_CONFIG_FILE);
        return 0;
    }

    m_initalized = true;
    sMktBotDataMgr.Initialize();
    sLog.Blue("     MarketBotMgr", "Market Bot Manager Initialized.");
    return 1;
}

void MarketBotMgr::ForceRun() {
    _log(MARKET__TRACE, "Admin-triggered MarketBot run.");

    if (!m_initalized) {
        _log(MARKET__ERROR, "MarketBotMgr is not initialized — skipping.");
        return;
    }

    this->Process();  // run main logic
    _log(MARKET__TRACE, "MarketBot Process() completed.");
    m_updateTimer.Start(sMBotConf.main.DataRefreshTime * 60 * 1000);
}

// Called on minute tick from EntityList
void MarketBotMgr::Process() {
    if (!m_initalized || !m_updateTimer.Check())
        return;

    ExpireOldOrders();

    std::vector<uint32> eligibleSystems = GetEligibleSystems();
    for (uint32 systemID : eligibleSystems) {
        std::vector<uint32> availableStations;
        if (!sDataMgr.GetStationListForSystem(systemID, availableStations)) {
            _log(MARKET__TRACE, "Skipping system %u: no stations available.", systemID);
            continue;
        }
        PlaceBuyOrders(systemID);
        PlaceSellOrders(systemID);
    }

    m_updateTimer.Start(sMBotConf.main.DataRefreshTime * 60 * 1000);
}

void MarketBotMgr::AddSystem() { /* To be implemented if needed */ }
void MarketBotMgr::RemoveSystem() { /* To be implemented if needed */ }

void MarketBotMgr::ExpireOldOrders() {
    uint64_t now = GetFileTimeNow();

    DBQueryResult res;
    DBResultRow row;

    if (!sDatabase.RunQuery(res,
        "SELECT orderID FROM market_orders WHERE issued + (duration * 86400000000) < %" PRIu64 " AND ownerID = 1",
        now)) {
        _log(MARKET__DB_ERROR, "Failed to query expired bot orders.");
        return;
    }

    while (res.GetRow(row)) {
        uint32 orderID = row.GetUInt(0);
        MarketDB::DeleteOrder(orderID);
        _log(MARKET__TRACE, "Expired MarketBot order %u", orderID);
    }
}

void MarketBotMgr::PlaceBuyOrders(uint32 systemID) {
    SystemData sysData;
    if (!sDataMgr.GetSystemData(systemID, sysData)) {
        _log(MARKET__ERROR, "Failed to get system data for system %u", systemID);
        return;
    }

    std::vector<uint32> availableStations;
    if (!sDataMgr.GetStationListForSystem(systemID, availableStations)) {
        _log(MARKET__ERROR, "No stations found for system %u", systemID);
        return;
    }

    size_t stationCount = availableStations.size();
    size_t stationLimit = std::max<size_t>(1, stationCount / 2);
    std::shuffle(availableStations.begin(), availableStations.end(), std::mt19937{std::random_device{}()});

    for (size_t i = 0; i < std::min<size_t>(stationLimit, sMBotConf.main.OrdersPerRefresh); ++i) {
        uint32 stationID = availableStations[i];
        uint32 itemID = SelectRandomItemID();
        const ItemType* type = sItemFactory.GetType(itemID);
        if (!type) continue;

        uint32 quantity = GetRandomQuantity(type->groupID());
        double price = CalculateBuyPrice(itemID);

        if (price * quantity > sMBotConf.main.MaxISKPerOrder)
            continue;

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
        order.ownerID = 1000125; // NPC corp owner

        MarketDB::StoreOrder(order);
    }
}

void MarketBotMgr::PlaceSellOrders(uint32 systemID) {
    SystemData sysData;
    if (!sDataMgr.GetSystemData(systemID, sysData)) {
        _log(MARKET__ERROR, "Failed to get system data for system %u", systemID);
        return;
    }

    std::vector<uint32> availableStations;
    if (!sDataMgr.GetStationListForSystem(systemID, availableStations)) {
        _log(MARKET__ERROR, "No stations found for system %u", systemID);
        return;
    }

    size_t stationCount = availableStations.size();
    size_t stationLimit = std::max<size_t>(1, stationCount / 2);
    std::shuffle(availableStations.begin(), availableStations.end(), std::mt19937{std::random_device{}()});

    for (size_t i = 0; i < std::min<size_t>(stationLimit, sMBotConf.main.OrdersPerRefresh); ++i) {
        uint32 stationID = availableStations[i];
        uint32 itemID = SelectRandomItemID();
        const ItemType* type = sItemFactory.GetType(itemID);
        if (!type) continue;

        uint32 quantity = GetRandomQuantity(type->groupID());
        double price = CalculateSellPrice(itemID);

        if (price * quantity > sMBotConf.main.MaxISKPerOrder)
            continue;

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
        order.ownerID = 1; // NPC corp owner

        MarketDB::StoreOrder(order);
    }
}

std::vector<uint32> MarketBotMgr::GetEligibleSystems() {
    std::vector<uint32> systemIDs;
    sDataMgr.GetRandomSystemIDs(5, systemIDs); // pulls a randomized list of systems
    return systemIDs;
}

uint32 MarketBotMgr::SelectRandomItemID() {
    uint32 itemID = 0;
    const ItemType* type = nullptr;
    do {
        itemID = GetRandomInt(10, MARKETBOT_MAX_ITEM_ID);
        type = sItemFactory.GetType(itemID);
    } while (!type || std::find(VALID_GROUPS.begin(), VALID_GROUPS.end(), type->groupID()) == VALID_GROUPS.end());
    return itemID;
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
