#pragma once
#include "nav.h"
#include <fstream>
#include <filesystem>

class CNavFile
{
public:
	// Intended to use with engine->GetLevelName() or mapname from server_spawn
	// GameEvent Change it if you get the nav file from elsewhere
	explicit CNavFile(const char* szLevelname)
	{
		if (!szLevelname)
			return;

		//        m_mapName = std::string("tf/");
		//        std::string map(szLevelname);

		//        if (map.find("maps/") == std::string::npos)
		//            m_mapName.append("maps/");

		//        m_mapName.append(szLevelname);
		//        int dotpos = m_mapName.find('.');
		//        m_mapName  = m_mapName.substr(0, dotpos);
		//        m_mapName.append(".nav");
		m_mapName.append(szLevelname);

		std::ifstream fs(m_mapName, std::ios::binary);

		if (!fs.is_open())
		{
			//.nav file does not exist
			return;
		}

		uint32_t magic;
		fs.read((char*)&magic, sizeof(uint32_t));

		if (magic != 0xFEEDFACE)
		{
			// Wrong magic number
			return;
		}

		uint32_t version;
		fs.read((char*)&version, sizeof(uint32_t));

		if (version < 16) // 16 is latest for TF2
		{
			// Version is too old
			return;
		}

		uint32_t subVersion;
		fs.read((char*)&subVersion, sizeof(uint32_t));

		if (subVersion != 2) // 2 for TF2
		{
			// Not TF2 nav file
			return;
		}

		// We do not really need to check the size
		fs.read((char*)&m_bspSize, sizeof(uint32_t));
		fs.read((char*)&m_isAnalized, sizeof(unsigned char));

		unsigned short placesCount;
		fs.read((char*)&placesCount, sizeof(uint16_t));

		// TF2 does not use places, but in case they exist
		for (int i = 0; i < placesCount; ++i)
		{
			CNavPlace place{};
			fs.read((char*)&place.m_len, sizeof(uint16_t));
			fs.read((char*)&place.m_name, place.m_len);

			m_places.push_back(place);
		}

		fs.read((char*)&m_hasUnnamedAreas, sizeof(unsigned char));
		unsigned int areaCount;
		fs.read((char*)&areaCount, sizeof(uint32_t));

		for (size_t i = 0; i < areaCount; ++i)
		{
			CNavArea area;
			fs.read((char*)&area.m_id, sizeof(uint32_t));
			fs.read((char*)&area.m_attributeFlags, sizeof(uint32_t));
			fs.read((char*)&area.m_nwCorner, sizeof(Vector));
			fs.read((char*)&area.m_seCorner, sizeof(Vector));
			fs.read((char*)&area.m_neZ, sizeof(float));
			fs.read((char*)&area.m_swZ, sizeof(float));

			area.m_center[0] = (area.m_nwCorner[0] + area.m_seCorner[0]) / 2.0f;
			area.m_center[1] = (area.m_nwCorner[1] + area.m_seCorner[1]) / 2.0f;
			area.m_center[2] = (area.m_nwCorner[2] + area.m_seCorner[2]) / 2.0f;

			if ((area.m_seCorner.x - area.m_nwCorner.x) > 0.0f && (area.m_seCorner.y - area.m_nwCorner.y) > 0.0f)
			{
				area.m_invDxCorners = 1.0f / (area.m_seCorner.x - area.m_nwCorner.x);
				area.m_invDyCorners = 1.0f / (area.m_seCorner.y - area.m_nwCorner.y);
			}
			else
				area.m_invDxCorners = area.m_invDyCorners = 0.0f;

			// Change the tolerance if you wish
			area.m_minZ = std::min(area.m_seCorner.z, area.m_nwCorner.z) - 18.f;
			area.m_maxZ = std::max(area.m_seCorner.z, area.m_nwCorner.z) + 18.f;

			for (int dir = 0; dir < 4; dir++)
			{
				fs.read((char*)&area.m_connectionCount, sizeof(uint32_t));

				for (size_t j = 0; j < area.m_connectionCount; j++)
				{
					NavConnect connect;

					fs.read((char*)&connect.id, sizeof(uint32_t));


					// Connection to the same area?
					if (connect.id == area.m_id)
					{
						area.m_connectionCount--;
						continue;
					}

					// Note: If connection directions matter to you, uncomment
					// this
					area.m_connections /*[dir]*/.push_back(connect);
					area.m_connectionsDir[dir].push_back(connect);
				}
			}

			fs.read((char*)&area.m_hidingSpotCount, sizeof(uint8_t));

			for (size_t j = 0; j < area.m_hidingSpotCount; j++)
			{
				HidingSpot spot;
				fs.read((char*)&spot.m_id, sizeof(uint32_t));
				fs.read((char*)&spot.m_pos, sizeof(Vector));
				fs.read((char*)&spot.m_flags, sizeof(unsigned char));

				area.m_hidingSpots.push_back(spot);
			}

			fs.read((char*)&area.m_encounterSpotCount, sizeof(uint32_t));

			for (size_t j = 0; j < area.m_encounterSpotCount; j++)
			{
				SpotEncounter spot;
				fs.read((char*)&spot.from.id, sizeof(uint32_t));
				fs.read((char*)&spot.fromDir, sizeof(unsigned char));
				fs.read((char*)&spot.to.id, sizeof(uint32_t));
				fs.read((char*)&spot.toDir, sizeof(unsigned char));
				fs.read((char*)&spot.spotCount, sizeof(unsigned char));

				for (int s = 0; s < spot.spotCount; ++s)
				{
					SpotOrder order{};
					fs.read((char*)&order.id, sizeof(uint32_t));
					fs.read((char*)&order.t, sizeof(unsigned char));
					spot.spots.push_back(order);
				}

				area.m_spotEncounters.push_back(spot);
			}

			fs.read((char*)&area.m_indexType, sizeof(uint16_t));

			// TF2 does not use ladders either
			for (int dir = 0; dir < 2; dir++)
			{
				fs.read((char*)&area.m_ladderCount, sizeof(uint32_t));

				for (size_t j = 0; j < area.m_ladderCount; j++)
				{
					int temp;
					fs.read((char*)&temp, sizeof(uint32_t));
					area.m_ladders[dir].push_back(temp);
				}
			}

			for (float& j : area.m_earliestOccupyTime)
				fs.read((char*)&j, sizeof(float));

			for (float& j : area.m_lightIntensity)
				fs.read((char*)&j, sizeof(float));

			fs.read((char*)&area.m_visibleAreaCount, sizeof(uint32_t));

			for (size_t j = 0; j < area.m_visibleAreaCount; ++j)
			{
				AreaBindInfo info;
				fs.read((char*)&info.id, sizeof(uint32_t));
				fs.read((char*)&info.attributes, sizeof(unsigned char));

				area.m_potentiallyVisibleAreas.push_back(info);
			}

			fs.read((char*)&area.m_inheritVisibilityFrom, sizeof(uint32_t));

			// TF2 Specific area flags
			fs.read((char*)&area.m_TFattributeFlags, sizeof(uint32_t));

			m_areas.push_back(area);
		}

		fs.close();

		// Fill connection for every area with their area ptrs instead of IDs
		// This will come in handy in path finding

		for (auto& area : m_areas)
		{
			for (auto& connection : area.m_connections)
				for (auto& connected_area : m_areas)
					if (connection.id == connected_area.m_id)
						connection.area = &connected_area;

			// Fill potentially visible areas as well
			for (auto& bindinfo : area.m_potentiallyVisibleAreas)
				for (auto& boundarea : m_areas)
					if (bindinfo.id == boundarea.m_id)
						bindinfo.area = &boundarea;

		}
		m_isOK = true;
	}


	// Im not sure why but it takes away last 4 bytes of the nav file
	// Might be related to the fact that im not using CUtlBuffer for saving this
	void Write()
	{
		std::string sFilePath{ std::filesystem::current_path().string() + "\\Amalgam\\Nav\\" + SDK::GetLevelName() + ".nav" };
		std::ofstream file(sFilePath, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			SDK::Output("CNavFile::Write", std::format("Couldn't open file {}", sFilePath).c_str(), { 200, 150, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return;
		}

		uint32_t magic = 0xFEEDFACE;
		uint32_t version = 16;
		uint32_t subVersion = 2;
		file.write((char*)&magic, sizeof(uint32_t));
		file.write((char*)&version, sizeof(uint32_t));
		file.write((char*)&subVersion, sizeof(uint32_t));
		file.write((char*)&m_bspSize, sizeof(uint32_t));
		file.write((char*)&m_isAnalized, sizeof(unsigned char));
		size_t placesCount = m_places.size();
		file.write((char*)&placesCount, sizeof(uint16_t));

		for (auto& place : m_places)
		{
			file.write((char*)&place.m_len, sizeof(uint16_t));
			file.write((char*)&place.m_name, place.m_len);
		}

		file.write((char*)&m_hasUnnamedAreas, sizeof(unsigned char));
		size_t areaCount = m_areas.size();
		file.write((char*)&areaCount, sizeof(uint32_t));

		for (auto& area : m_areas)
		{
			file.write((char*)&area.m_id, sizeof(uint32_t));
			file.write((char*)&area.m_attributeFlags, sizeof(uint32_t));
			file.write((char*)&area.m_nwCorner, sizeof(Vector));
			file.write((char*)&area.m_seCorner, sizeof(Vector));
			file.write((char*)&area.m_neZ, sizeof(float));
			file.write((char*)&area.m_swZ, sizeof(float));

			for (int dir = 0; dir < 4; dir++)
			{
				size_t connectionCount = area.m_connectionsDir[dir].size();
				file.write((char*)&connectionCount, sizeof(uint32_t));

				for (auto& connect : area.m_connectionsDir[dir])
				{
					file.write((char*)&connect.id, sizeof(uint32_t));
				}
			}

			size_t hidingSpotCount = area.m_hidingSpots.size();
			file.write((char*)&hidingSpotCount, sizeof(uint8_t));

			for (auto& hidingSpot : area.m_hidingSpots)
			{
				file.write((char*)&hidingSpot.m_id, sizeof(uint32_t));
				file.write((char*)&hidingSpot.m_pos, sizeof(Vector));
				file.write((char*)&hidingSpot.m_flags, sizeof(unsigned char));
			}

			size_t encounterSpotCount = area.m_spotEncounters.size();
			file.write((char*)&encounterSpotCount, sizeof(uint32_t));

			for (auto& encounterSpot : area.m_spotEncounters)
			{
				file.write((char*)&encounterSpot.from.id, sizeof(uint32_t));
				file.write((char*)&encounterSpot.fromDir, sizeof(unsigned char));
				file.write((char*)&encounterSpot.to.id, sizeof(uint32_t));
				file.write((char*)&encounterSpot.toDir, sizeof(unsigned char));
				size_t spotCount = encounterSpot.spots.size();
				file.write((char*)&spotCount, sizeof(unsigned char));

				for (auto& order : encounterSpot.spots)
				{
					file.write((char*)&order.id, sizeof(uint32_t));
					file.write((char*)&order.t, sizeof(unsigned char));
				}
			}

			file.write((char*)&area.m_indexType, sizeof(uint16_t));

			for (int dir = 0; dir < 2; dir++)
			{
				size_t ladderCount = area.m_ladders[dir].size();
				file.write((char*)&ladderCount, sizeof(uint32_t));

				for (auto& ladder : area.m_ladders[dir])
				{
					file.write((char*)&ladder, sizeof(uint32_t));
				}
			}

			for (float& j : area.m_earliestOccupyTime)
				file.write((char*)&j, sizeof(float));

			for (float& j : area.m_lightIntensity)
				file.write((char*)&j, sizeof(float));

			size_t potentiallyVisibleCount = area.m_potentiallyVisibleAreas.size();
			file.write((char*)&potentiallyVisibleCount, sizeof(uint32_t));

			for (auto& visibleArea : area.m_potentiallyVisibleAreas)
			{
				file.write((char*)&visibleArea.id, sizeof(uint32_t));
				file.write((char*)&visibleArea.attributes, sizeof(unsigned char));
			}

			file.write((char*)&area.m_inheritVisibilityFrom, sizeof(uint32_t));
			file.write((char*)&area.m_TFattributeFlags, sizeof(uint32_t));
		}

		file.close();
	}

	unsigned int m_bspSize;
	std::string m_mapName;
	bool m_isAnalized{};
	std::vector<CNavPlace> m_places;
	bool m_hasUnnamedAreas{};
	std::vector<CNavArea> m_areas;
	bool m_isOK = false;
};