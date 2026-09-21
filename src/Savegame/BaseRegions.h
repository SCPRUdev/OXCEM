#pragma once
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
#include <map>
#include <string>
#include <vector>
#include "../Engine/Yaml.h"

namespace OpenXcom
{
class SavedGame;
class Base;
class Mod;
class RuleEvent;
class RuleRegion;

/// Persistent context for one unique escaped object.
struct EscapeContext
{
	std::string region, status = "searching", lostEvent;
	std::vector<std::string> recoveryItems;
	int baseId = 0, month = 0, searchMonths = 3;
};

/// Campaign-owned dynamic regions and escape contexts, separate from geographical regions.
class BaseRegions
{
	SavedGame& _game;
	std::map<std::string, RuleRegion*> _baseRegions;
	std::map<std::string, EscapeContext> _escapes;
public:
	explicit BaseRegions(SavedGame& game);
	~BaseRegions();
	BaseRegions(const BaseRegions&) = delete;
	BaseRegions& operator=(const BaseRegions&) = delete;
	bool hasRegions() const { return !_baseRegions.empty(); }
	RuleRegion* getMissionRegion(const std::string& id, const Mod& mod, bool error = false) const;
	void syncBaseRegions(const Mod& mod);
	void loadBaseRegions(const YAML::YamlNodeReader& reader, Mod& mod);
	void saveBaseRegions(YAML::YamlNodeWriter writer) const;
	std::vector<RuleRegion*> getDebugRegions() const;
	std::vector<std::string> getBaseRegions(const std::string& templateId, const Mod& mod);
	void registerEscape(const RuleEvent& rule, Base& base, const Mod& mod);
	void updateEscapes(const Mod& mod);
	bool hasEscapeMission(const std::string& id) const;
	std::string getEscapeRegion(const std::string& id, const std::string& templateId, int minMonths, const Mod& mod);
	const std::map<std::string, EscapeContext>& getEscapes() const { return _escapes; }
	void loadEscapes(const YAML::YamlNodeReader& reader);
	void saveEscapes(YAML::YamlNodeWriter writer) const;
};
}
