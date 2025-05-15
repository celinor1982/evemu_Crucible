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
#include "inventory/ItemType.h"
#include "inventory/ItemFactory.h"
#include "inventory/InventoryItem.h"
#include "system/SystemManager.h"
#include "system/SystemEntity.h"
#include "utils/Random.h"
#include <random>

extern ItemFactory* sItemFactory;
extern SystemManager* sSystemMgr;
extern MarketMgr sMarketMgr;

static const uint32 MARKETBOT_MAX_ITEM_ID = 30000;
static const std::vector<uint32> VALID_GROUPS = {
    18, 19, 20, 53, 55, 63, 70, 83, 85, 104, 108, 255
};

static const char* const BOT_CONFIG_FILE = "/src/utils/config/MarketBot.xml";

MarketBotDataMgr::MarketBotDataMgr() {
    m_initalized = false;
}

int MarketBotDataMgr::Initialize() {
    m_initalized = true;

    /* load current data */
    sLog.Blue(" MarketBotDataMgr", "Market Bot Data Manager Initialized.");
    return 1;
}

MarketBotMgr::MarketBotMgr()
: m_updateTimer(20 * 60 * 1000)  // default 20 minutes
{
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

// Called on minute tick from EntityList
void MarketBotMgr::Process() {
    if (!m_initalized || !m_updateTimer.Check())
        return;

    ExpireOldOrders();

    std::vector<uint32> eligibleSystems = GetEligibleSystems();
    for (uint32 systemID : eligibleSystems) {
        PlaceBuyOrders(systemID);
        PlaceSellOrders(systemID);
    }

    m_updateTimer.Start(sMBotConf.main.DataRefreshTime * 60 * 1000);
}

void MarketBotMgr::AddSystem() {
    // To be implemented if needed
}

void MarketBotMgr::RemoveSystem() {
    // To be implemented if needed
}

void MarketBotMgr::ExpireOldOrders() {
    sMarketMgr.ExpireBotOrders();
}

void MarketBotMgr::PlaceBuyOrders(uint32 systemID) {
    for (int i = 0; i < sMBotConf.main.OrdersPerRefresh; ++i) {
        uint32 itemID = SelectRandomItemID();
        const ItemType* type = sItemFactory->GetType(itemID);
        if (!type) continue;

        uint32 quantity = GetRandomQuantity(type->groupID());
        double price = CalculateBuyPrice(itemID);

        if (price * quantity > sMBotConf.main.MaxISKPerOrder)
            continue;

        sMarketMgr.CreateMarketOrder(systemID, itemID, quantity, price, true, sMBotConf.main.OrderLifetime, true);
    }
}

void MarketBotMgr::PlaceSellOrders(uint32 systemID) {
    for (int i = 0; i < sMBotConf.main.OrdersPerRefresh; ++i) {
        uint32 itemID = SelectRandomItemID();
        const ItemType* type = sItemFactory->GetType(itemID);
        if (!type) continue;

        uint32 quantity = GetRandomQuantity(type->groupID());
        double price = CalculateSellPrice(itemID);

        if (price * quantity > sMBotConf.main.MaxISKPerOrder)
            continue;

        sMarketMgr.CreateMarketOrder(systemID, itemID, quantity, price, false, sMBotConf.main.OrderLifetime, true);
    }
}

std::vector<uint32> MarketBotMgr::GetEligibleSystems() {
    std::vector<uint32> systemIDs;
    for (const auto& [id, sys] : sSystemMgr->GetSystems()) {
        systemIDs.push_back(id);
    }
    std::shuffle(systemIDs.begin(), systemIDs.end(), std::mt19937{ std::random_device{}() });
    return std::vector<uint32>(systemIDs.begin(), systemIDs.begin() + std::min<size_t>(5, systemIDs.size()));
}

uint32 MarketBotMgr::SelectRandomItemID() {
    uint32 itemID = 0;
    const ItemType* type = nullptr;
    do {
        itemID = MakeRandomInt(10, MARKETBOT_MAX_ITEM_ID);
        type = sItemFactory->GetType(itemID);
    } while (!type || std::find(VALID_GROUPS.begin(), VALID_GROUPS.end(), type->groupID()) == VALID_GROUPS.end());
    return itemID;
}

uint32 MarketBotMgr::GetRandomQuantity(uint32 groupID) {
    // Large quantities for ores, ammo, charges
    if (groupID == 18 || groupID == 20 || groupID == 53 || groupID == 104) {
        return MakeRandomInt(1000, 1000000);
    }
    // Medium quantities for modules, rigs, etc
    if (groupID == 55 || groupID == 63 || groupID == 70) {
        return MakeRandomInt(10, 100);
    }
    // Default to small quantity
    return MakeRandomInt(1, 5);
}

double MarketBotMgr::CalculateBuyPrice(uint32 itemID) {
    const ItemType* type = sItemFactory->GetType(itemID);
    return type ? type->GetBasePrice() * MakeRandomFloat(0.8f, 1.1f) : 1000.0;
}

double MarketBotMgr::CalculateSellPrice(uint32 itemID) {
    const ItemType* type = sItemFactory->GetType(itemID);
    return type ? type->GetBasePrice() * MakeRandomFloat(1.0f, 1.3f) : 1000.0;
}