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

// ---marketbot update; everything past this point has been completely changed.
#include "market/MarketDB.h"
#include "inventory/ItemType.h"
#include "inventory/ItemFactory.h"
#include "inventory/InventoryItem.h"
#include "station/StationDataMgr.h"
#include "system/SystemManager.h"
#include "system/SystemEntity.h"
#include <random>
#include <cstdint>
#include <chrono>

extern SystemManager* sSystemMgr;

static const uint32 MARKETBOT_MAX_ITEM_ID = 30000;
static const std::vector<uint32> VALID_GROUPS = {
    // Ores & Mining
    18,                                    // Minerals
    450, 451, 452, 453, 454, 455, 456,     // Raw ores (part 1)
    457, 458, 459, 460, 461, 462,
    465, 466, 467, 468, 469,              // Raw ores (part 2)
    479,                                  // Scanner Probe
    482,                                  // Mining Crystals
    492,                                  // Survey Probe
    538,                                  // Data Miners
    548,                                  // Interdiction Probe
    663,                                  // Mercoxit Mining Crystals

    // Ammo / Charges
    83,                                   // Projectile Ammo
    84,                                   // Missiles
    85,                                   // Hybrid Charges
    86,                                   // Frequency Crystals
    87,                                   // Cap Booster Charges
    88,                                   // Defender Missiles
    89,                                   // Torpedoes
    90,                                   // Bombs
    92,                                   // Mines
    372, 373, 374, 375, 376, 377,         // Advanced Ammo
    384, 385, 386, 387, 388, 389,         // Extended Missiles (part 1)
    390, 391, 392, 393, 394, 395, 396,    // Extended Missiles (part 2)
    648,                                  // Advanced Rocket
    653, 654, 655, 656, 657,              // Advanced Missiles
    772,                                  // Assault Missiles

    // Boosters for Implants
    303
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
    m_nextRunTime = Clock::now();  // immediate run on first tick

    sLog.Cyan("     MarketBotMgr", "Initialized — automation will trigger on next tick.");

    sLog.Blue("     MarketBotMgr", "Market Bot Manager Initialized.");
    return 1;
}

// Called on minute tick from EntityList
void MarketBotMgr::Process(bool overrideTimer) {
    TimePoint now = Clock::now();

    std::printf("[MarketBot] MarketBot Process() invoked on tick.\n");
    std::fflush(stdout);
    _log(MARKET__TRACE, "MarketBot Process() invoked on tick.");

    std::printf("[MarketBot] Entered MarketBotMgr::Process()\n");
    std::fflush(stdout);
    _log(MARKET__TRACE, ">> Entered MarketBotMgr::Process()");

    if (!m_initalized) {
        std::printf("[MarketBot] MarketBotMgr not initialized — skipping run\n");
        std::fflush(stdout);
        _log(MARKET__ERROR, "Process() called but MarketBotMgr is not initialized.");
        return;
    }

    if (!overrideTimer && now < m_nextRunTime) {
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
    m_nextRunTime = Clock::now() + std::chrono::minutes(sMBotConf.main.DataRefreshTime);
    std::printf("[MarketBot] Timer reset. Next run in %d minutes.\n", sMBotConf.main.DataRefreshTime);
    std::fflush(stdout);
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
    
    m_nextRunTime = Clock::now() + std::chrono::minutes(sMBotConf.main.DataRefreshTime);
    std::printf("[MarketBot] Timer reset. Next run in %d minutes.\n", sMBotConf.main.DataRefreshTime);
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

        double escrow = price * quantity;

        Market::SaveData order;
        order.typeID = itemID;
        order.regionID = sysData.regionID;
        order.stationID = stationID;
        order.solarSystemID = systemID;
        order.minVolume = 1;
        order.volEntered = quantity;
        order.volRemaining = quantity;
        order.price = price;
        order.escrow = escrow; // required, without this no one can sell items to bot orders.
        order.duration = sMBotConf.main.OrderLifetime;
        order.bid = true;
        order.issued = GetFileTimeNow();
        order.isCorp = false;
        order.ownerID = BOT_OWNER_ID;
        order.orderRange = 32767; // -1 station, 0 solarsystem, 1-5 10 20 30 40 jumps, 32767 region (default)
        // marketbot update; unsure if these are really needed, but aligns with how orders are created manually in-game and through seed-market.
        order.memberID = 0;       // default value for who placed the order (0 for char order)
        order.accountKey = 1000;  // default value for corp account key

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
        order.minVolume = 1;
        order.volEntered = quantity;
        order.volRemaining = quantity;
        order.price = price;
        /* order.escrow = escrow; */ // not required for sell orders; place here for possible future use.
        order.duration = sMBotConf.main.OrderLifetime;
        order.bid = false;
        order.issued = GetFileTimeNow();
        order.isCorp = false;
        order.ownerID = BOT_OWNER_ID;
        order.orderRange = -1; // -1 station (default), 0 solarsystem, 1-5 10 20 30 40 jumps, 32767 region
        // marketbot update; unsure if these are really needed, but aligns with how orders are created manually in-game and through seed-market.
        order.memberID = 0;       // default value for who placed the order (0 for char order)
        order.accountKey = 1000;  // default value for corp account key

        std::printf("[MarketBot] System %u maps to region %u via GetSystemData.\n", systemID, sysData.regionID);
        std::fflush(stdout);
        _log(MARKET__TRACE, "System %u maps to region %u via GetSystemData", systemID, sysData.regionID);

        std::printf("[MarketBot] Placing sell order with orderRange: %u\n", order.orderRange);
        std::fflush(stdout);
        _log(MARKET__TRACE, "MarketBot: Storing sell order with orderRange = %u", order.orderRange);

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
    bool useStaticSystems = false; // ---marketbot update; only turn to true, for testing changes with markbot or want to have it only populate orders in one system
    if (useStaticSystems) {
        return { 30000142 };  // Jita (default system for testing)
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
    // Large-quantity bulk groups: minerals, ammo, ores, charges, etc.
    if (
        groupID == 18 ||                      // Minerals
        (groupID >= 83 && groupID <= 92) ||   // Basic ammo/charges
        (groupID >= 372 && groupID <= 377) || // Advanced ammo
        (groupID >= 384 && groupID <= 396) || // More missiles
        groupID == 479 ||                     // Scanner Probes
        groupID == 482 ||                     // Mining Crystals
        groupID == 492 ||                     // Survey Probes
        groupID == 538 ||                     // Data Miners
        groupID == 548 ||                     // Interdiction Probe
        groupID == 648 ||                     // Advanced Rocket
        (groupID >= 653 && groupID <= 657) || // Advanced Missiles
        groupID == 663 ||                     // Mercoxit Mining Crystals
        groupID == 772 ||                     // Assault Missiles
        (groupID >= 450 && groupID <= 462) || // Raw ores (part 1)
        (groupID >= 465 && groupID <= 469)    // Raw ores (part 2)
    ) {
        return GetRandomInt(1000, 1000000);  // Large stack sizes
    }

    // Medium-volume: modules, rigs, etc.; way to many to list... disabled for the time being
    // leaving below as an example.
    /*if (
        groupID == 62 ||  // Armor Repairers
        groupID == 63 ||  // Hull Repair
        groupID == 205    // Heat Sink
    ) {
        return GetRandomInt(10, 100);
    }*/

    // Fallback for anything else
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
