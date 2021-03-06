#include "pathPlaning.h"
#include "Util.h"
#include "CCBot.h"

node::node() :m_pos(sc2::Point2D()), m_status(-1), m_travelCost(-1.0f), m_heuristicToGoal(-1.0f), m_threatLvl(-1.0f), m_totalCost(-1.0), m_parent(nullptr)
{

}

node::node(sc2::Point2D pos,int status, float travelCost,float heuristicToGoal,float threatLvl, std::shared_ptr<node> parent) : m_pos(pos), m_status(status), m_travelCost(travelCost), m_heuristicToGoal(heuristicToGoal), m_threatLvl(threatLvl), m_totalCost(travelCost+heuristicToGoal), m_parent(parent)
{

}

pathPlaning::pathPlaning(CCBot & bot, sc2::Point2D startPos, sc2::Point2D endPos, int mapWidth, int mapHeight, float stepSize):m_bot(bot),m_endPos(endPos),m_stepSize(stepSize),
	m_map(std::vector<std::vector<std::shared_ptr<node>>>(static_cast<int>(static_cast<float>(mapWidth +1) / stepSize), std::vector<std::shared_ptr<node>>(static_cast<int>(static_cast<float>(mapHeight+1) / stepSize), nullptr)))
{
	int status = 0;
	float travelCost = 0.0f;
	float heuristicToGoal = calcHeuristic(startPos);
	float threatLvl = m_bot.Map().calcThreatLvl(startPos, sc2::Weapon::TargetType::Air);
	setMap(startPos,std::make_shared<node>(startPos, status, travelCost, heuristicToGoal, threatLvl));
	m_openList.push_front(map(startPos));
}

const float pathPlaning::calcHeuristic(sc2::Point2D pos) const
{
	float xDist = std::abs(pos.x - m_endPos.x);
	float yDist = std::abs(pos.y - m_endPos.y);
	//Octile heuristic
	return std::max(xDist, yDist) + (std::sqrt(2.0f) - 1.0f)*std::min(xDist, yDist);
}

std::vector<sc2::Point2D> pathPlaning::planPath()
{
	while (!m_openList.empty())
	{
		std::shared_ptr<node> frontNode = getBestNextNodeAndPop();
		if (reachedEndPos(frontNode->m_pos) || m_openList.size()>500)
		{
			return constructPath(frontNode);
		}
		expandFrontNode(frontNode);
	}
	return std::vector<sc2::Point2D>();
}

std::shared_ptr<node> pathPlaning::getBestNextNodeAndPop()
{
	
	std::list<std::shared_ptr<node>>::iterator it = m_openList.begin();

	std::list<std::shared_ptr<node>>::iterator bestIt = it;
	++it;
	while (it != m_openList.end())
	{
		if (isBiggerNode(*bestIt,*it))
		{
			bestIt = it;
		}
		++it;
	}
	sc2::Point2D bestNodePos = (*bestIt)->m_pos;
	m_openList.erase(bestIt);
	return map(bestNodePos);
}

const bool pathPlaning::isBiggerNode(std::shared_ptr<node> nodeA, std::shared_ptr<node> nodeB)
{
	if (nodeA->m_totalCost > nodeB->m_totalCost)
	{
		return true;
	}
	else if (nodeA->m_totalCost < nodeB->m_totalCost)
	{
		return false;
	}
	//Tie breaking
	return nodeA->m_travelCost > nodeB->m_travelCost;
}

std::shared_ptr<node> pathPlaning::map(sc2::Point2D pos) const
{
	return m_map[int(pos.x / m_stepSize)][int(pos.y / m_stepSize)];
}

void pathPlaning::setMap(sc2::Point2D pos, std::shared_ptr<node> node)
{
	m_map[int(pos.x / m_stepSize)][int(pos.y / m_stepSize)]=node;
}

void pathPlaning::activateNode(sc2::Point2D pos, std::shared_ptr<node> parent)
{
	int status = 0;
	float heuristicToGoal=calcHeuristic(pos);
	float threatLvl=m_bot.Map().calcThreatLvl(pos,sc2::Weapon::TargetType::Air);
	float travelCost = parent->m_travelCost + threatLvl + Util::Dist(pos, parent->m_pos);
	setMap(pos,std::make_shared<node>(pos, status, travelCost, heuristicToGoal, threatLvl,parent));
	m_openList.push_front(map(pos));
}

void pathPlaning::updateNode(std::shared_ptr<node> updateNode, std::shared_ptr<node> parent)
{
	float travelCost = parent->m_totalCost + Util::Dist(updateNode->m_pos, parent->m_pos);
	if (updateNode->m_travelCost > travelCost)
	{
		updateNode->m_travelCost = travelCost;
	}
}

const bool pathPlaning::reachedEndPos(sc2::Point2D currentPos) const
{
	return Util::DistSq(currentPos, m_endPos) <= 0.5*m_stepSize*m_stepSize;
}

void pathPlaning::expandFrontNode(std::shared_ptr<node> frontNode)
{
	frontNode->m_status = 1;
	sc2::Point2D currentPos(frontNode->m_pos);
	sc2::Point2D xMove(m_stepSize, 0.0f);
	sc2::Point2D yMove(0.0f, m_stepSize);
	for (float i = -1.0f; i <= 1.0f; ++i)
	{
		for (float j = -1.0f; j <= 1.0f; ++j)
		{
			if (i != 0.0f || j != 0.0f)
			{
				sc2::Point2D newPos = currentPos + i*xMove + j*yMove;
				if (newPos.x < m_bot.Observation()->GetGameInfo().playable_min.x || newPos.x > m_bot.Observation()->GetGameInfo().playable_max.x || newPos.y < m_bot.Observation()->GetGameInfo().playable_min.y || newPos.y > m_bot.Observation()->GetGameInfo().playable_max.y)
				{
					continue;
				}
				std::shared_ptr<node> newNode = map(newPos);
				if (!newNode || newNode->m_status == -1)
				{
					activateNode(newPos, frontNode);
				}
				else if (newNode->m_status == 0)
				{
					updateNode(newNode, frontNode);
				}
			}
		}
	}
}

std::vector<sc2::Point2D> pathPlaning::constructPath(std::shared_ptr<const node> frontNode) const
{
	std::vector < sc2::Point2D> path;
	path.push_back(m_endPos);
	std::shared_ptr<const node> currentNode = frontNode;
	while (currentNode->m_parent)
	{
		if (currentNode->m_parent->m_parent)
		{
			const sc2::Point2D directionNext = currentNode->m_parent->m_pos - path.back();
			const sc2::Point2D directionNextNext = currentNode->m_parent->m_parent->m_pos - path.back();
			if (directionNext.x*directionNextNext.y != directionNext.y*directionNextNext.x)
			{
				path.push_back(currentNode->m_pos);
			}
		}
		else
		{
			path.push_back(currentNode->m_pos);
		}
		currentNode = currentNode->m_parent;
	}
	std::reverse(path.begin(), path.end());
	return path;
}
