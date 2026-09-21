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
#include <assert.h>
#include "RuleRegion.h"
#include "Mod.h"
#include "City.h"
#include "../Engine/Logger.h"
#include "../Engine/RNG.h"
#include "../Engine/Exception.h"
#include "../Geoscape/Globe.h"
#include <algorithm>
#include <memory>

namespace OpenXcom
{

/**
 * Creates a blank ruleset for a certain type of region.
 * @param type String defining the type.
 */
RuleRegion::RuleRegion(const std::string &type): _type(type), _cost(0), _regionWeight(0)
{
}

/**
 * Deletes the cities from memory.
 */
RuleRegion::~RuleRegion()
{
	for (auto* city : _cities)
	{
		delete city;
	}
}

/**
 * Loads the region type from a YAML file.
 * @param node YAML node.
 */
void RuleRegion::load(const YAML::YamlNodeReader& reader, Mod* mod)
{
	if (const auto& parent = reader["refNode"])
	{
		load(parent, mod);
	}

	reader.tryRead("cost", _cost);

	if (reader["deleteOldAreas"].readVal(false))
	{
		_lonMin.clear();
		_lonMax.clear();
		_latMin.clear();
		_latMax.clear();
	}
	for (const auto& area : reader["areas"].children())
	{
		_lonMin.push_back(Deg2Rad(area[0].readVal<double>()));
		_lonMax.push_back(Deg2Rad(area[1].readVal<double>()));
		_latMin.push_back(Deg2Rad(area[2].readVal<double>()));
		_latMax.push_back(Deg2Rad(area[3].readVal<double>()));

		if (_latMin.back() > _latMax.back())
			std::swap(_latMin.back(), _latMax.back());
	}

	reader.tryRead("cityMissionZones", _cityMissionZones);
	reader.tryRead("missionZones", _missionZones);
	{
		int zn = 0;
		for (auto& z : _missionZones)
		{
			if (z.areas.size() < 1)
			{
				if (!isCityMissionZone(zn))
					Log(LOG_WARNING) << "Empty zone, region: " << _type << ", zone: " << zn;
				++zn;
				continue;
			}
			int an = 0;
			bool firstAreaType = z.areas.at(0).isPoint();
			for (auto& a : z.areas)
			{
				if (a.isPoint() != firstAreaType)
				{
					Log(LOG_WARNING) << "Mixed area types (point vs non-point), region: " << _type << ", zone: " << zn << ", area: " << an;
				}
				if (a.lonMin > a.lonMax)
				{
					Log(LOG_ERROR) << "Crossing the prime meridian in mission zones requires a different syntax, region: " << _type << ", zone: " << zn << ", area: " << an << ", lonMin: " << Rad2Deg(a.lonMin) << ", lonMax: " << Rad2Deg(a.lonMax);
					Log(LOG_INFO) << "  Wrong example: [350,   8, 20, 30]";
					Log(LOG_INFO) << "Correct example: [350, 368, 20, 30]";
				}
				++an;
			}
			++zn;
		}
	}
	if (const auto& weights = reader["missionWeights"])
	{
		_missionWeights.load(weights);
	}
	reader.tryRead("regionWeight", _regionWeight);
	reader.tryRead("missionRegion", _missionRegion);
	reader.tryRead("baseRegionTemplate", _baseRegionTemplate);
	reader.tryRead("baseRegionId", _baseRegionId);
	reader.tryRead("landOnly", _landOnly);
	reader.tryRead("maxDistanceKm", _maxDistanceKm);
	reader.tryRead("originLon", _originLon);
	reader.tryRead("originLat", _originLat);

	mod->loadBaseFunction(_type, _provideBaseFunc, reader["provideBaseFunc"]);
	mod->loadBaseFunction(_type, _forbiddenBaseFunc, reader["forbiddenBaseFunc"]);
}

/**
 * Gets the language string that names
 * this region. Each region type
 * has a unique name.
 * @return The region type.
 */
const std::string& RuleRegion::getType() const
{
	return _type;
}

/**
 * Gets the cost of building a base inside this region.
 * @return The construction cost.
 */
int RuleRegion::getBaseCost() const
{
	return _cost;
}

/**
 * Checks if a point is inside this region.
 * @param lon Longitude in radians.
 * @param lat Latitude in radians.
 * @param ignoreTechnicalRegion If true, empty technical regions (i.e. regions with no areas, just having mission zones) will return true.
 * @return True if it's inside, false if it's outside.
 */
bool RuleRegion::insideRegion(double lon, double lat, bool ignoreTechnicalRegion) const
{
	if (isBaseRegion())
	{
		lon = std::fmod(lon, 2 * M_PI);
		if (lon < 0) lon += 2 * M_PI;
	}
	if (ignoreTechnicalRegion && _lonMin.empty())
		return true;

	for (size_t i = 0; i < _lonMin.size(); ++i)
	{
		bool inLon, inLat;

		if (_lonMin[i] <= _lonMax[i])
			inLon = (lon >= _lonMin[i] && lon < _lonMax[i]);
		else
			inLon = ((lon >= _lonMin[i] && lon < M_PI*2.0) || (lon >= 0 && lon < _lonMax[i]));

		if (lat > 0) // make that both poles could be in some regions, this means `M_PI == _latMax[i]` or `-M_PI == _latMin[i]`
			inLat = (lat > _latMin[i] && lat <= _latMax[i]);
		else
			inLat = (lat >= _latMin[i] && lat < _latMax[i]);

		if (inLon && inLat)
			return true;
	}
	return false;
}

void RuleRegion::validateBaseTemplate() const
{
	for (size_t i = 0; i < _cityMissionZones.size(); ++i)
	{
		int zone = _cityMissionZones[i];
		if (zone < 0 || (size_t)zone >= _missionZones.size()
			|| std::find(_cityMissionZones.begin(), _cityMissionZones.begin() + i, zone) != _cityMissionZones.begin() + i)
			throw Exception("Invalid or duplicate cityMissionZones index in base region template: " + _type);
	}
	auto valid = [](double lo, double hi, double south, double north)
	{
		return std::isfinite(lo) && std::isfinite(hi) && std::isfinite(south) && std::isfinite(north)
			&& lo <= hi && hi - lo <= 2 * M_PI && south >= -M_PI / 2 && north <= M_PI / 2;
	};
	if (_lonMin.empty() || _missionZones.empty() || !_missionRegion.empty()
		|| !std::isfinite(_maxDistanceKm) || _maxDistanceKm < 0)
		throw Exception("Invalid base region template: " + _type + " (requires areas and missionZones, no missionRegion, nonnegative maxDistanceKm)");
	for (size_t i = 0; i < _lonMin.size(); ++i)
		if (!valid(_lonMin[i], _lonMax[i], _latMin[i], _latMax[i])
			|| _lonMin[i] == _lonMax[i] || _latMin[i] == _latMax[i])
			throw Exception("Invalid relative area in base region template: " + _type);
	for (const auto& zone : _missionZones)
	{
		if (zone.areas.empty()) throw Exception("Empty mission zone in base region template: " + _type);
		for (const auto& area : zone.areas)
			if (!valid(area.lonMin, area.lonMax, area.latMin, area.latMax))
				throw Exception("Invalid relative mission area in base region template: " + _type);
	}
}

bool RuleRegion::isCityMissionZone(size_t zone) const
{
	return std::find(_cityMissionZones.begin(), _cityMissionZones.end(), (int)zone) != _cityMissionZones.end();
}

RuleRegion* RuleRegion::instantiate(const std::string& id, int baseId, double lon, double lat,
	const std::vector<MissionArea>& cities) const
{
	validateBaseTemplate();
	std::unique_ptr<RuleRegion> result(new RuleRegion(id));
	result->_baseRegionTemplate = _type;
	result->_baseRegionId = baseId;
	result->_landOnly = _landOnly;
	result->_maxDistanceKm = _maxDistanceKm;
	result->_originLon = lon;
	result->_originLat = lat;
	auto normalize = [](double value)
	{
		value = std::fmod(value, 2 * M_PI);
		return value < 0 ? value + 2 * M_PI : value;
	};
	auto latitude = [lat](double offset) { return std::max(-M_PI / 2, std::min(M_PI / 2, lat + offset)); };
	for (size_t i = 0; i < _lonMin.size(); ++i)
	{
		double width = _lonMax[i] - _lonMin[i];
		double lo = normalize(lon + _lonMin[i]);
		result->_lonMin.push_back(width >= 2 * M_PI ? 0 : lo);
		result->_lonMax.push_back(width >= 2 * M_PI ? 2 * M_PI : normalize(lo + width));
		result->_latMin.push_back(latitude(_latMin[i]));
		result->_latMax.push_back(latitude(_latMax[i]));
	}
	result->_missionZones = _missionZones;
	result->_cityMissionZones = _cityMissionZones;
	for (auto& zone : result->_missionZones)
		for (auto& area : zone.areas)
		{
			double width = area.lonMax - area.lonMin;
			area.lonMin = normalize(lon + area.lonMin);
			area.lonMax = area.lonMin + width; // mission zones use an unwrapped interval
			area.latMin = latitude(area.latMin);
			area.latMax = latitude(area.latMax);
		}
	for (int zoneIndex : _cityMissionZones)
	{
		auto& zone = result->_missionZones[zoneIndex];
		const auto searchAreas = zone.areas;
		zone.areas.clear();
		for (const auto& city : cities)
		{
			if (!city.isPoint() || city.name.empty()) continue;
			double cityLon = normalize(city.lonMin);
			if (!result->allowsBaseRegionPoint(cityLon, city.latMin)) continue;
			// Repeated city entries and overlapping search rectangles must not multiply a city's weight.
			if (std::any_of(zone.areas.begin(), zone.areas.end(), [&](const MissionArea& existing)
				{ return AreSame(existing.lonMin, cityLon) && AreSame(existing.latMin, city.latMin); })) continue;
			for (const auto& search : searchAreas)
			{
				double offset = normalize(cityLon - search.lonMin);
				if (AreSame(offset, 2 * M_PI)) offset = 0;
				double width = search.lonMax - search.lonMin;
				if ((offset > width && !AreSame(offset, width))
					|| city.latMin < search.latMin || city.latMin > search.latMax) continue;
				MissionArea selected = city;
				selected.lonMin = selected.lonMax = cityLon;
				selected.latMin = selected.latMax = city.latMin;
				selected.texture = search.texture;
				zone.areas.push_back(selected);
				break; // first matching rectangle supplies the texture
			}
		}
	}
	return result.release();
}

bool RuleRegion::allowsBaseRegionPoint(double lon, double lat) const
{
	if (!insideRegion(lon, lat)) return false;
	if (_maxDistanceKm <= 0) return true;
	double cosine = std::sin(lat) * std::sin(_originLat)
		+ std::cos(lat) * std::cos(_originLat) * std::cos(lon - _originLon);
	return 6371.0 * std::acos(std::max(-1.0, std::min(1.0, cosine))) <= _maxDistanceKm;
}

void RuleRegion::saveBaseRegion(YAML::YamlNodeWriter writer) const
{
	writer.setAsMap();
	writer.write("type", _type);
	writer.write("baseRegionTemplate", _baseRegionTemplate);
	writer.write("baseRegionId", _baseRegionId);
	writer.write("landOnly", _landOnly);
	writer.write("maxDistanceKm", _maxDistanceKm);
	writer.write("originLon", _originLon);
	writer.write("originLat", _originLat);
	if (!_cityMissionZones.empty()) writer.write("cityMissionZones", _cityMissionZones);
	auto areas = writer["areas"];
	areas.setAsSeq();
	for (size_t i = 0; i < _lonMin.size(); ++i)
		areas.write(std::vector<double>{Rad2Deg(_lonMin[i]), Rad2Deg(_lonMax[i]), Rad2Deg(_latMin[i]), Rad2Deg(_latMax[i])});
	auto zones = writer["missionZones"];
	zones.setAsSeq();
	for (const auto& zone : _missionZones)
	{
		auto zw = zones.write();
		zw.setAsSeq();
		for (const auto& area : zone.areas)
		{
			auto aw = zw.write();
			aw.setAsSeq();
			aw.write(Rad2Deg(area.lonMin)); aw.write(Rad2Deg(area.lonMax));
			aw.write(Rad2Deg(area.latMin)); aw.write(Rad2Deg(area.latMax));
			aw.write(area.texture); aw.write(area.name);
		}
	}
}

bool RuleRegion::sampleBaseRegionPoint(const Globe& globe, size_t zone, int area,
	std::pair<double, double>& point, bool requireLand, int fakeWater) const
{
	return sampleBaseRegionPoint(zone, area, point, [&](double lon, double lat)
	{
		return (!requireLand || globe.insideLand(lon, lat))
			&& (fakeWater < 0 || globe.insideFakeUnderwaterTexture(lon, lat) == (fakeWater != 0));
	});
}

bool RuleRegion::sampleBaseRegionPoint(size_t zone, int area, std::pair<double, double>& point,
	const std::function<bool(double, double)>& acceptsSurface) const
{
	if (zone < _missionZones.size() && _missionZones[zone].areas.empty() && isCityMissionZone(zone) && area == -1)
		return false;
	if (zone >= _missionZones.size() || _missionZones[zone].areas.empty()
		|| area < -1 || (area >= 0 && (size_t)area >= _missionZones[zone].areas.size()))
		throw Exception("Invalid zone/area in base region: " + _type);
	for (int attempt = 0; attempt < 1000; ++attempt)
	{
		point = getRandomPoint(zone, area);
		point.first = std::fmod(point.first, 2 * M_PI);
		if (point.first < 0) point.first += 2 * M_PI;
		if (!allowsBaseRegionPoint(point.first, point.second)) continue;
		if (!acceptsSurface(point.first, point.second)) continue;
		return true;
	}
	return false;
}

/**
 * Gets the list of cities contained in this region.
 * @return Pointer to a list.
 */
std::vector<City*> *RuleRegion::getCities()
{
	// Build a cached list of all mission zones that are cities
	// Saves us from constantly searching for them
	if (_cities.empty())
	{
		for (const auto& mz : _missionZones)
		{
			for (const auto& ma : mz.areas)
			{
				if (ma.isPoint() && !ma.name.empty())
				{
					_cities.push_back(new City(ma.name, ma.lonMin, ma.latMin));
				}
			}
		}
	}
	return &_cities;
}

/**
 * Gets the weight of this region for mission selection.
 * This is only used when creating a new game, since these weights change in the course of the game.
 * @return The initial weight of this region.
 */
size_t RuleRegion::getWeight() const
{
	return _regionWeight;
}

/**
 * Gets a list of all the missionZones in the region.
 * @return A list of missionZones.
 */
const std::vector<MissionZone> &RuleRegion::getMissionZones() const
{
	return _missionZones;
}

/**
 * Gets a random point that is guaranteed to be inside the given zone.
 * @param zone The target zone.
 * @return A pair of longitude and latitude.
 */
std::pair<double, double> RuleRegion::getRandomPoint(size_t zone, int area) const
{
	if (zone < _missionZones.size())
	{
		size_t a = area != -1 ? area : RNG::generate(0, _missionZones[zone].areas.size() - 1);
		double lonMin = _missionZones[zone].areas[a].lonMin;
		double lonMax = _missionZones[zone].areas[a].lonMax;
		double latMin = _missionZones[zone].areas[a].latMin;
		double latMax = _missionZones[zone].areas[a].latMax;
		if (lonMin > lonMax)
		{
			lonMin = _missionZones[zone].areas[a].lonMax;
			lonMax = _missionZones[zone].areas[a].lonMin;
		}
		if (latMin > latMax)
		{
			latMin = _missionZones[zone].areas[a].latMax;
			latMax = _missionZones[zone].areas[a].latMin;
		}
		double lon = RNG::generate(lonMin, lonMax);
		double lat = RNG::generate(latMin, latMax);
		return std::make_pair(lon, lat);
	}
	assert(0 && "Invalid zone number");
	return std::make_pair(0.0, 0.0);
}

// helper overloads for deserialization-only
bool read(ryml::ConstNodeRef const& n, MissionArea* val)
{
	YAML::YamlNodeReader reader(n);
	val->lonMin = Deg2Rad(reader[0].readVal<double>());
	val->lonMax = Deg2Rad(reader[1].readVal<double>());
	val->latMin = Deg2Rad(reader[2].readVal<double>());
	val->latMax = Deg2Rad(reader[3].readVal<double>());
	if (val->latMin > val->latMax)
		std::swap(val->latMin, val->latMax);
	size_t count = reader.childrenCount();
	if (count >= 5)
		val->texture = reader[4].readVal<int>();
	if (count >= 6)
		val->name = reader[5].readVal<std::string>();
	return true;
}

bool read(ryml::ConstNodeRef const& n, MissionZone* val)
{
	YAML::YamlNodeReader reader(n);
	reader.tryReadVal(val->areas);
	return true;
}

}
