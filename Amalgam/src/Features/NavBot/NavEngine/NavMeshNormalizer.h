#pragma once
#include "FileReader/CNavFile.h"

class CNavMeshNormalizer
{
public:
	bool Normalize(CNavFile* pNavFile);

private:
	bool ConnectAdjacentAreas(CNavFile* pNavFile);
	bool IsAdjacent(const CNavArea& area1, const CNavArea& area2, int& iDir);
	void AddBidirectionalConnection(CNavArea& area1, CNavArea& area2, int iDir1to2);
	bool ConnectionExists(const CNavArea& area, uint32_t uTargetId);
};
