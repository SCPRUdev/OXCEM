/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "BaseRegions.h"
#include "SavedGame.h"
#include "Base.h"
#include "Region.h"
#include "AlienMission.h"
#include "MissionSite.h"
#include "Transfer.h"
#include "ItemContainer.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleRegion.h"
#include "../Mod/RuleEvent.h"
#include "../Engine/Exception.h"
#include "../Engine/Logger.h"
#include <algorithm>
#include <set>

namespace OpenXcom
{

BaseRegions::BaseRegions(SavedGame& game) : _game(game) {}

BaseRegions::~BaseRegions()
{
	for (auto& entry : _baseRegions) delete entry.second;
}

RuleRegion* BaseRegions::getMissionRegion(const std::string& id, const Mod& mod, bool error) const
{
	auto it = _baseRegions.find(id);
	return it == _baseRegions.end() ? mod.getRegion(id, error) : it->second;
}

void BaseRegions::loadBaseRegions(const YAML::YamlNodeReader& reader, Mod& mod)
{
	for (const auto& entry : reader.children())
	{
		std::string id = entry["type"].readVal<std::string>();
		if (_baseRegions.count(id) || mod.getRegion(id)) throw Exception("Duplicate saved base region: " + id);
		RuleRegion* region = new RuleRegion(id);
		_baseRegions[id] = region;
		region->load(entry, &mod);
		if (!region->isBaseRegion() || region->getBaseRegionId() <= 0 || region->getMissionZones().empty())
			throw Exception("Invalid saved base region: " + id);
	}
}

void BaseRegions::saveBaseRegions(YAML::YamlNodeWriter writer) const
{
	writer.setAsSeq();
	for (const auto& entry : _baseRegions) entry.second->saveBaseRegion(writer.write());
}

std::vector<RuleRegion*> BaseRegions::getDebugRegions() const
{
	std::vector<RuleRegion*> result;
	for (auto* region : *_game.getRegions()) result.push_back(region->getRules());
	for (const auto& entry : _baseRegions) result.push_back(entry.second);
	return result;
}

void BaseRegions::loadEscapes(const YAML::YamlNodeReader& reader)
{
	_escapes.clear();
	for (const auto& node : reader.children())
	{
		std::string id = node["id"].readVal<std::string>();
		EscapeContext e;
		node.tryRead("region", e.region); node.tryRead("baseId", e.baseId);
		node.tryRead("month", e.month); node.tryRead("searchMonths", e.searchMonths);
		node.tryRead("status", e.status); node.tryRead("recoveryItems", e.recoveryItems);
		node.tryRead("lostEvent", e.lostEvent);
		if (id.empty() || _escapes.count(id) || e.region.empty() || e.baseId <= 0 || e.searchMonths < 0
			|| (e.status != "searching" && e.status != "recovered" && e.status != "lost"))
			throw Exception("Invalid escape context: " + id);
		_escapes[id] = e;
	}
}

void BaseRegions::saveEscapes(YAML::YamlNodeWriter writer) const
{
	writer.setAsSeq();
	for (const auto& entry : _escapes)
	{
		auto node = writer.write(); node.setAsMap();
		const auto& e = entry.second;
		node.write("id", entry.first); node.write("region", e.region); node.write("baseId", e.baseId);
		node.write("month", e.month); node.write("searchMonths", e.searchMonths);
		node.write("status", e.status); node.write("recoveryItems", e.recoveryItems);
		node.write("lostEvent", e.lostEvent);
	}
}

bool BaseRegions::hasEscapeMission(const std::string& id) const
{
	for (const auto* mission : _game.getAlienMissions())
		if (mission->getEscapeId() == id && !mission->isOver()) return true;
	for (const auto* site : *_game.getMissionSites())
		if (site->getEscapeId() == id) return true;
	return false;
}

void BaseRegions::registerEscape(const RuleEvent& rule, Base& base, const Mod& mod)
{
	const std::string& id = rule.getEscapeId();
	if (id.empty()) return;
	// IDs describe unique objects. Never move an unresolved escape to another base.
	auto existing = _escapes.find(id);
	if (existing != _escapes.end() && (existing->second.status == "searching" || hasEscapeMission(id)))
	{
		Log(LOG_WARNING) << "Escape already active: " << id;
		return;
	}
	syncBaseRegions(mod);
	EscapeContext e;
	e.baseId = base.getBaseRegionId(); e.month = _game.getMonthsPassed();
	e.region = "__BASE_REGION_" + std::to_string(e.baseId) + ":" + rule.getEscapeRegionTemplate();
	getMissionRegion(e.region, mod, true);
	e.searchMonths = rule.getEscapeSearchMonths(); e.lostEvent = rule.getEscapeLostEvent();
	e.recoveryItems = rule.getEscapeRecoveryItems();
	if (e.recoveryItems.empty()) e.recoveryItems.push_back(rule.getEscapeItem());
	_escapes[id] = e;
}

void BaseRegions::updateEscapes(const Mod& mod)
{
	for (auto& entry : _escapes)
	{
		auto& e = entry.second;
		if (e.status == "recovered") continue;
		bool recovered = false;
		for (const auto& itemName : e.recoveryItems)
		{
			if (_game.isItemObtained(itemName, &mod)) recovered = true;
			const auto* item = mod.getItem(itemName);
			for (const auto* base : *_game.getBases())
				for (const auto* transfer : *base->getTransfers())
					if (item && transfer->getItems() == item && transfer->getQuantity() > 0) recovered = true;
		}
		if (recovered) e.status = "recovered";
		else if (e.status == "searching" && _game.getMonthsPassed() - e.month > e.searchMonths)
		{
			// Stop new searches; an already scheduled/visible mission remains playable.
			e.status = "lost";
			Log(LOG_INFO) << "Escape search expired: " << entry.first;
			if (!e.lostEvent.empty()) _game.spawnEvent(mod.getEvent(e.lostEvent, true));
		}
	}
}

std::string BaseRegions::getEscapeRegion(const std::string& id, const std::string& templateId, int minMonths, const Mod& mod)
{
	updateEscapes(mod);
	auto it = _escapes.find(id);
	if (it == _escapes.end() || it->second.status != "searching"
		|| _game.getMonthsPassed() - it->second.month < minMonths || hasEscapeMission(id)) return {};
	auto* region = getMissionRegion(it->second.region, mod);
	if (!region || region->getBaseRegionTemplate() != templateId) return {};
	return it->second.region;
}

void BaseRegions::syncBaseRegions(const Mod& mod)
{
	if (mod.getBaseRegionTemplates().empty() && _baseRegions.empty()) return;
	std::vector<MissionArea> cities;
	if (std::any_of(mod.getBaseRegionTemplates().begin(), mod.getBaseRegionTemplates().end(),
		[](const std::pair<const std::string, RuleRegion*>& entry) { return entry.second->hasCityMissionZones(); }))
	{
		// Use geographical regions only, excluding the technical copies used by special missions.
		for (const auto* region : *_game.getRegions())
		{
			const auto* rule = region->getRules();
			if (rule->getLonMin().empty()) continue;
			for (const auto& zone : rule->getMissionZones())
				for (const auto& area : zone.areas)
					if (area.isPoint() && !area.name.empty()) cities.push_back(area);
		}
	}
	std::set<int> liveBases;
	int largestId = 0;
	for (auto* base : *_game.getBases()) largestId = std::max(largestId, base->getBaseRegionId());
	for (const auto& entry : _baseRegions) largestId = std::max(largestId, entry.second->getBaseRegionId());
	for (auto* base : *_game.getBases())
	{
		// The first base may be positioned after the initial geoscape was opened.
		// A relocated base gets a fresh identity; active missions retain the old snapshot.
		for (const auto& entry : _baseRegions)
			if (entry.second->getBaseRegionId() == base->getBaseRegionId()
				&& !entry.second->isCenteredAt(base->getLongitude(), base->getLatitude()))
			{
				base->setBaseRegionId(0);
				break;
			}
		if (!base->getBaseRegionId())
		{
			int id;
			do { id = _game.getId("BASE_REGION_BASES"); } while (id <= largestId);
			base->setBaseRegionId(id);
			largestId = id;
		}
		if (!liveBases.insert(base->getBaseRegionId()).second)
			throw Exception("Duplicate baseRegionId in saved bases");
		for (const auto& entry : mod.getBaseRegionTemplates())
		{
			std::string id = "__BASE_REGION_" + std::to_string(base->getBaseRegionId()) + ":" + entry.first;
			if (mod.getRegion(id)) throw Exception("Reserved dynamic region ID: " + id);
			if (!_baseRegions.count(id))
				_baseRegions[id] = entry.second->instantiate(id, base->getBaseRegionId(), base->getLongitude(), base->getLatitude(), cities);
		}
	}
	std::set<std::string> used;
	for (auto* mission : _game.getAlienMissions()) used.insert(mission->getRegion());
	for (const auto& entry : _escapes)
		if (entry.second.status == "searching" || hasEscapeMission(entry.first)) used.insert(entry.second.region);
	for (auto it = _baseRegions.begin(); it != _baseRegions.end();)
	{
		if ((!liveBases.count(it->second->getBaseRegionId())
			|| !mod.getBaseRegionTemplates().count(it->second->getBaseRegionTemplate())) && !used.count(it->first))
		{
			delete it->second;
			if (_game.debugRegion == it->first)
			{
				_game.debugRegion.clear();
				_game.debugZone = _game.debugArea = 0;
			}
			it = _baseRegions.erase(it);
		}
		else ++it;
	}
}

std::vector<std::string> BaseRegions::getBaseRegions(const std::string& templateId, const Mod& mod)
{
	if (!mod.getBaseRegionTemplates().count(templateId))
		throw Exception("Unknown baseRegionTemplate: " + templateId);
	syncBaseRegions(mod);
	std::vector<std::string> result;
	for (const auto& entry : _baseRegions)
		if (entry.second->getBaseRegionTemplate() == templateId)
			for (auto* base : *_game.getBases())
				if (base->getBaseRegionId() == entry.second->getBaseRegionId()) result.push_back(entry.first);
	return result;
}


}
