#include "NavMeshNormalizer.h"
#include <algorithm>
#include <cmath>

bool CNavMeshNormalizer::Normalize(CNavFile* pNavFile)
{
	if (!pNavFile)
		return false;

	return ConnectAdjacentAreas(pNavFile);
}

bool CNavMeshNormalizer::ConnectAdjacentAreas(CNavFile* pNavFile)
{
	bool bChanged = false;
	for (auto& area1 : pNavFile->m_vAreas)
	{
		for (auto& area2 : pNavFile->m_vAreas)
		{
			if (area1.m_uId == area2.m_uId)
				continue;

			int iDir = -1;
			if (IsAdjacent(area1, area2, iDir))
			{
				if (!ConnectionExists(area1, area2.m_uId))
				{
					AddBidirectionalConnection(area1, area2, iDir);
					bChanged = true;
				}
			}
		}
	}
	return bChanged;
}

bool CNavMeshNormalizer::IsAdjacent(const CNavArea& area1, const CNavArea& area2, int& iDir)
{
	float minZ1 = area1.m_flMinZ;
	float maxZ1 = area1.m_flMaxZ;
	float minZ2 = area2.m_flMinZ;
	float maxZ2 = area2.m_flMaxZ;

	bool bZOverlap = (minZ1 <= maxZ2 + 80.0f) && (minZ2 <= maxZ1 + 80.0f);
	if (!bZOverlap)
		return false;

	if (std::abs(area1.m_vNwCorner.y - area2.m_vSeCorner.y) < 2.0f)
	{
		float overlapMinX = std::max(area1.m_vNwCorner.x, area2.m_vNwCorner.x);
		float overlapMaxX = std::min(area1.m_vSeCorner.x, area2.m_vSeCorner.x);
		if (overlapMaxX > overlapMinX)
		{
			iDir = 0;
			return true;
		}
	}

	if (std::abs(area1.m_vSeCorner.y - area2.m_vNwCorner.y) < 2.0f)
	{
		float overlapMinX = std::max(area1.m_vNwCorner.x, area2.m_vNwCorner.x);
		float overlapMaxX = std::min(area1.m_vSeCorner.x, area2.m_vSeCorner.x);
		if (overlapMaxX > overlapMinX)
		{
			iDir = 2;
			return true;
		}
	}

	if (std::abs(area1.m_vSeCorner.x - area2.m_vNwCorner.x) < 2.0f)
	{
		float overlapMinY = std::max(area1.m_vNwCorner.y, area2.m_vNwCorner.y);
		float overlapMaxY = std::min(area1.m_vSeCorner.y, area2.m_vSeCorner.y);
		if (overlapMaxY > overlapMinY)
		{
			iDir = 1;
			return true;
		}
	}

	if (std::abs(area1.m_vNwCorner.x - area2.m_vSeCorner.x) < 2.0f)
	{
		float overlapMinY = std::max(area1.m_vNwCorner.y, area2.m_vNwCorner.y);
		float overlapMaxY = std::min(area1.m_vSeCorner.y, area2.m_vSeCorner.y);
		if (overlapMaxY > overlapMinY)
		{
			iDir = 3;
			return true;
		}
	}

	return false;
}

void CNavMeshNormalizer::AddBidirectionalConnection(CNavArea& area1, CNavArea& area2, int iDir1to2)
{
	// Add 1 -> 2
	NavConnect_t conn1;
	conn1.m_uId = area2.m_uId;
	conn1.m_pArea = &area2;
	conn1.m_flLength = area1.m_vCenter.DistTo(area2.m_vCenter);

	area1.m_vConnections.push_back(conn1);
	area1.m_uConnectionCount++;
	if (iDir1to2 >= 0 && iDir1to2 < 4)
		area1.m_vConnectionsDir[iDir1to2].push_back(conn1);

	// Add 2 -> 1
	int iDir2to1 = -1;
	switch (iDir1to2) {
	case 0: iDir2to1 = 2; break; // N -> S
	case 1: iDir2to1 = 3; break; // E -> W
	case 2: iDir2to1 = 0; break; // S -> N
	case 3: iDir2to1 = 1; break; // W -> E
	}

	NavConnect_t conn2;
	conn2.m_uId = area1.m_uId;
	conn2.m_pArea = &area1;
	conn2.m_flLength = conn1.m_flLength;

	area2.m_vConnections.push_back(conn2);
	area2.m_uConnectionCount++;
	if (iDir2to1 >= 0 && iDir2to1 < 4)
		area2.m_vConnectionsDir[iDir2to1].push_back(conn2);
}

bool CNavMeshNormalizer::ConnectionExists(const CNavArea& area, uint32_t uTargetId)
{
	for (const auto& conn : area.m_vConnections)
	{
		if (conn.m_uId == uTargetId)
			return true;
	}
	return false;
}
