#include "RangedManager.h"
#include "Util.h"
#include "CCBot.h"

RangedManager::RangedManager(CCBot & bot)
    : MicroManager(bot)
{

}

void RangedManager::executeMicro(const CUnits & targets)
{
    assignTargets(targets);
}

void RangedManager::assignTargets(const CUnits & targets)
{
    const CUnits & rangedUnits = getUnits();

    // figure out targets
    CUnits rangedUnitTargets;
    for (const auto & target : targets)
    {
        if (!target) { continue; }
        if (target->getUnitType() == sc2::UNIT_TYPEID::ZERG_EGG) { continue; }
        if (target->getUnitType() == sc2::UNIT_TYPEID::ZERG_LARVA) { continue; }

        rangedUnitTargets.push_back(target);
    }
	//The idea is now to group the targets/targetPos
	std::unordered_map<CUnit_ptr, CUnits> targetsAttackedBy;
	CUnits moveToPosition;
	//For the medivac we need either
	//Either the most injured
	std::map<float, CUnit_ptr> injuredUnits;
	//Or the soldier in the front
	CUnit_ptr frontSoldier = nullptr;
	sc2::Point2D orderPos = order.getPosition();
	float minDist = std::numeric_limits<float>::max();
	//Just checking if only medivacs available
	bool justMedivacs = true;
	for (const auto & injured : rangedUnits)
	{
		if (!m_bot.GetUnit(injured->getTag()) || !injured->isAlive())
		{
			//its too late
			continue;
		}
		if (injured->getUnitType().ToType() != sc2::UNIT_TYPEID::TERRAN_MEDIVAC)
		{
			justMedivacs = false;
		}
		else
		{
			continue;
		}
		//We can only heal biological units
		if (injured->hasAttribute(sc2::Attribute::Biological))
		{
			float healthMissing = injured->getHealthMax() - injured->getHealth();
			if (healthMissing>0)
			{
				injuredUnits[healthMissing] = injured;
			}
			float dist = Util::DistSq(injured->getPos(), orderPos);
			if (!frontSoldier || minDist > dist)
			{
				frontSoldier = injured;
				minDist = dist;
			}
		}
	}
	// In case it were really only medivacs
	if (justMedivacs)
	{
		Micro::SmartMove(rangedUnits, m_bot.Bases().getRallyPoint(),m_bot);
		return;
	}

	//Get effects like storm
	const std::vector<sc2::Effect> effects = m_bot.Observation()->GetEffects();

    // for each Unit
    for (auto & rangedUnit : rangedUnits)
    {
		//Don't stand in a storm etc
		bool fleeYouFools = false;
		for (const auto & effect : effects)
		{
			if (Util::isBadEffect(effect.effect_id))
			{
				const float radius = m_bot.Observation()->GetEffectData()[effect.effect_id].radius;
				for (const auto & pos : effect.positions)
				{
					Drawing::drawSphere(m_bot, pos, radius, sc2::Colors::Purple);
					if (Util::Dist(rangedUnit->getPos(), pos)<radius + 2.0f)
					{
						sc2::Point2D fleeingPos;
						if (effect.positions.size() == 1)
						{
							fleeingPos = pos + Util::normalizeVector(rangedUnit->getPos() - pos, radius + 2.0f);
						}
						else
						{
							const sc2::Point2D attackDirection = effect.positions.back() - effect.positions.front();
							//"Randomly" go right and left
							if (rangedUnit->getTag() % 2)
							{
								fleeingPos = rangedUnit->getPos() + Util::normalizeVector(sc2::Point2D(-attackDirection.x, attackDirection.y), radius + 2.0f);
							}
							else
							{
								fleeingPos = rangedUnit->getPos() - Util::normalizeVector(sc2::Point2D(-attackDirection.x, attackDirection.y), radius + 2.0f);
							}
						}
						Micro::SmartMove(rangedUnit, fleeingPos, m_bot);
						fleeYouFools = true;
						break;
					}
				}
			}
			
			
			if (fleeYouFools)
			{
				break;
			}
		}
		if (fleeYouFools)
		{
			continue;
		}
        BOT_ASSERT(rangedUnit, "ranged unit is null");
        // if the order is to attack or defend
		if (order.getType() == SquadOrderTypes::Attack || order.getType() == SquadOrderTypes::Defend || order.getType() == SquadOrderTypes::GuardDuty)
		{
			// find the best target for this rangedUnit
			//medivacs have the other ranged units as target.
			if (rangedUnit->getUnitType() == sc2::UNIT_TYPEID::TERRAN_MEDIVAC)
			{
				//find the nearest enemy
				CUnit_ptr nearestEnemy = nullptr;
				float minDistTarget = std::numeric_limits<float>::max();
				for (const auto & target : rangedUnitTargets)
				{
					if (target->isAlive() && rangedUnit->canHitMe(target))
					{
						float dist = Util::Dist(rangedUnit->getPos(), target->getPos());
						if (!nearestEnemy || minDistTarget > dist)
						{
							nearestEnemy = target;
							minDistTarget = dist;
						}
					}
				}
				
				if (injuredUnits.size()>0)
				{
					CUnit_ptr mostInjured = (injuredUnits.rbegin())->second;
					if (nearestEnemy && Util::Dist(rangedUnit->getPos(), nearestEnemy->getPos()) < Util::Dist(mostInjured->getPos(), nearestEnemy->getPos()))
					{
						Micro::SmartCDAbility(rangedUnit, sc2::ABILITY_ID::EFFECT_MEDIVACIGNITEAFTERBURNERS,m_bot);
						sc2::Point2D targetPos = rangedUnit->getPos();
						sc2::Point2D runningVector = Util::normalizeVector(rangedUnit->getPos() - nearestEnemy->getPos(), nearestEnemy->getAttackRange(rangedUnit) + 1);
						targetPos += runningVector;
						Micro::SmartMove(rangedUnit, targetPos, m_bot);
					}
					else if (Util::Dist(rangedUnit->getPos(), mostInjured->getPos()) > 4.0f)
					{
						Micro::SmartCDAbility(rangedUnit, sc2::ABILITY_ID::EFFECT_MEDIVACIGNITEAFTERBURNERS,m_bot);
						if (rangedUnit->getOrders().empty() || rangedUnit->getOrders().front().target_unit_tag != mostInjured->getTag())
						{
							Micro::SmartMove(rangedUnit, mostInjured,m_bot);
						}
					}
					else
					{
						if (rangedUnit->getOrders().empty() || rangedUnit->getOrders()[0].ability_id != sc2::ABILITY_ID::EFFECT_HEAL)
						{
							Micro::SmartAbility(rangedUnit, sc2::ABILITY_ID::EFFECT_HEAL, mostInjured,m_bot);
							injuredUnits.erase(std::prev(injuredUnits.end())); //no idea why rbegin is not working
						}
					}
				}
				else
				{
					if (rangedUnit->getOrders().empty() || frontSoldier && rangedUnit->getOrders()[0].target_unit_tag && rangedUnit->getOrders()[0].target_unit_tag != frontSoldier->getTag())
					{
						Micro::SmartMove(rangedUnit, frontSoldier,m_bot);
					}
				}
			}
			else
			{
				if (!rangedUnitTargets.empty() || (order.getType() == SquadOrderTypes::Defend && Util::Dist(rangedUnit->getPos(), order.getPosition()) > 7))
				{
					CUnit_ptr target = getTarget(rangedUnit, rangedUnitTargets);
					//if something goes wrong
					if (!target)
					{
						//This can happen with vikings
						if (rangedUnit->getOrders().empty() || frontSoldier && rangedUnit->getOrders().front().target_unit_tag && rangedUnit->getOrders().front().target_unit_tag != frontSoldier->getTag())
						{
							Micro::SmartMove(rangedUnit, frontSoldier,m_bot);
						}
						continue;
					}
					if (order.getType() == SquadOrderTypes::Defend)
					{
						CUnits Bunker = m_bot.UnitInfo().getUnits(Players::Self, sc2::UNIT_TYPEID::TERRAN_BUNKER);
						if (Bunker.size() > 0 && Bunker.front()->getCargoSpaceTaken() != Bunker.front()->getCargoSpaceMax())
						{
							if (Util::Dist(rangedUnit->getPos(), Bunker.front()->getPos()) < Util::Dist(rangedUnit->getPos(), target->getPos()))
							{
								Micro::SmartRightClick(rangedUnit, Bunker.front(), m_bot);
								Micro::SmartAbility(Bunker.front(), sc2::ABILITY_ID::LOAD, rangedUnit,m_bot);
								continue;
							}
						}
					}
					//We only need fancy micro if we are in range and its not a building
					if (!target->isBuilding() && Util::Dist(rangedUnit->getPos(), target->getPos()) <= rangedUnit->getAttackRange(target))
					{
						Micro::SmartKiteTarget(rangedUnit, target, m_bot);
					}
					//else we batch the attack comand first
					else
					{
						targetsAttackedBy[target].push_back(rangedUnit);
					}
				}
				// if there are no targets
				else
				{
					// if we're not near the order position
					if (Util::Dist(rangedUnit->getPos(), order.getPosition()) > 4.0f)
					{
						// move to it
						Drawing::drawLine(m_bot,rangedUnit->getPos(), order.getPosition(), sc2::Colors::White);
						moveToPosition.push_back(rangedUnit);
					}
				}
			}
		}
    }
	//Grouped by target attack command
	for (auto & t : targetsAttackedBy)
	{
		Micro::SmartAttackUnit(t.second, t.first, m_bot);
	}
	//Grouped by  position Move command
	if (moveToPosition.size() > 0)
	{
		Micro::SmartAttackMove(moveToPosition, order.getPosition(), m_bot);
	}
}

// get a target for the ranged unit to attack
const CUnit_ptr RangedManager::getTarget(const CUnit_ptr rangedUnit, const CUnits & targets)
{
    BOT_ASSERT(rangedUnit, "null melee unit in getTarget");
    int highPriorityFar = 0;
	int highPriorityNear = 0;
    double closestDist = std::numeric_limits<double>::max();
	double lowestHealth = std::numeric_limits<double>::max();
    CUnit_ptr closestTargetOutsideRange = nullptr;
	CUnit_ptr weakestTargetInsideRange = nullptr;
    // for each target possiblity
	// We have three levels: in range, in sight, somewhere.
	// We want to attack the weakest/highest prio target in range
	// If there is no in range, we want to attack one in sight,
	// else the one with highest prio.
	for (const auto & targetUnit : targets)
	{
		BOT_ASSERT(targetUnit, "null target unit in getTarget");
		//Ignore dead units or ones we can not hit
		if (!targetUnit->isAlive() || !targetUnit->canHitMe(rangedUnit))
		{
			continue;
		}
		const float range = rangedUnit->getAttackRange(targetUnit);
		int priority = getAttackPriority(rangedUnit, targetUnit);
		const float distance = Util::Dist(rangedUnit->getPos(), targetUnit->getPos());
		if (distance > range)
		{
			// If in sight we just add 20 to prio. This should make sure that a unit in sight has higher priority than any unit outside of range
			if (distance <= rangedUnit->getSightRange())
			{
				priority += 20;
			}
			// if it's a higher priority, or it's closer, set it
			if (!closestTargetOutsideRange || (priority > highPriorityFar) || (priority == highPriorityFar && distance < closestDist))
			{
				closestDist = distance;
				highPriorityFar = priority;
				closestTargetOutsideRange = targetUnit;
			}
		}
		else
		{
			if (!weakestTargetInsideRange || (priority > highPriorityNear) || (priority == highPriorityNear && targetUnit->getHealth() < lowestHealth))
			{
				lowestHealth = targetUnit->getHealth();
				highPriorityNear = priority;
				weakestTargetInsideRange = targetUnit;
			}
		}

	}
    return weakestTargetInsideRange&&highPriorityNear>1 ? weakestTargetInsideRange: closestTargetOutsideRange;
}

// get the attack priority of a type in relation to a zergling
int RangedManager::getAttackPriority(const CUnit_ptr attacker, const CUnit_ptr unit)
{
    BOT_ASSERT(unit, "null unit in getAttackPriority");

    if (unit->isCombatUnit())
    {
		if (unit->getUnitType() == sc2::UNIT_TYPEID::ZERG_BANELING)
		{
			return 11;
		}
        return 10;
    }
	if (unit->getUnitType() == sc2::UNIT_TYPEID::PROTOSS_PHOTONCANNON || unit->getUnitType() == sc2::UNIT_TYPEID::ZERG_SPINECRAWLER)
	{
		return 10;
	}
    if (unit->isWorker())
    {
        return 10;
    }
	if (unit->getUnitType() == sc2::UNIT_TYPEID::PROTOSS_PYLON || unit->getUnitType() == sc2::UNIT_TYPEID::ZERG_SPORECRAWLER || unit->getUnitType() == sc2::UNIT_TYPEID::TERRAN_MISSILETURRET)
	{
		return 5;
	}
	if (unit->isTownHall())
	{
		return 4;
	}
    return 1;
}

