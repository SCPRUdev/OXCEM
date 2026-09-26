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
#include "ActionMenuState.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/LocalizedText.h"
#include "../Engine/Action.h"
#include "../Engine/Unicode.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleInterface.h"
#include "ActionMenuItem.h"
#include "PrimeGrenadeState.h"
#include "MedikitState.h"
#include "ScannerState.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Savegame/HitLog.h"
#include "Pathfinding.h"
#include "TileEngine.h"
#include "../Interface/Text.h"

namespace OpenXcom
{

/**
 * Default constructor, used by SkillMenuState.
 */
ActionMenuState::ActionMenuState(BattleAction *action) : _action(action)
{
}

/**
 * Initializes all the elements in the Action Menu window.
 * @param game Pointer to the core game.
 * @param action Pointer to the action.
 * @param x Position on the x-axis.
 * @param y position on the y-axis.
 */
ActionMenuState::ActionMenuState(BattleAction *action, int x, int y) : _action(action)
{
	_screen = false;

	// Set palette
	_game->getSavedGame()->getSavedBattle()->setPaletteByDepth(this);

	// Mirror the left-hand position around the centre of the battlescape panel.
	const int panelWidth = _game->getMod()->getInterface("battlescape")->getElement("icons")->w;
	const auto* slot = _action->weapon->getSlot();
	_menuX = x + (slot && slot->isRightHand() ? panelWidth - 24 - ActionMenuItem::WIDTH : 24);
	_menuY = y + 20;

	// Build up the popup menu
	int id = 0;
	const RuleItem *weapon = _action->weapon->getRules();
	_action->itemAction = nullptr;
	if (weapon->hasActions())
	{
		if (weapon->isPsiRequired() && _action->actor->getBaseStats()->psiSkill <= 0) return;
		// Assign shortcuts in YAML order before reversing the upward-growing menu.
		std::vector<const RuleItemAction*> actions;
		std::vector<SDLKey> keys;
		std::vector<bool> extra;
		for (const auto& action : weapon->getActions())
		{
			if (getDefault(action.cost).Time <= 0 || (action.type == BA_THROW && weapon->isFixed())) continue;
			SDLKey key = SDLK_UNKNOWN;
			const bool additional = std::any_of(actions.begin(), actions.end(), [&](const RuleItemAction* a) { return a->type == action.type; });
			if (!additional)
			{
				switch (action.type)
				{
				case BA_AIMEDSHOT: key = Options::keyBattleActionItem1; break;
				case BA_SNAPSHOT: key = Options::keyBattleActionItem2; break;
				case BA_AUTOSHOT: key = Options::keyBattleActionItem3; break;
				case BA_HIT: key = Options::keyBattleActionItem4; break;
				case BA_THROW: key = Options::keyBattleActionItem5; break;
				default: break;
				}
			}
			actions.push_back(&action);
			keys.push_back(key);
			extra.push_back(additional);
		}
		const SDLKey extraKeys[] = { SDLK_6, SDLK_7, SDLK_8, SDLK_9, SDLK_0 };
		size_t nextKey = 0;
		for (size_t i = 0; i < actions.size(); ++i)
		{
			if (!extra[i]) continue;
			// Respect remapped classic shortcuts: never assign the same key twice.
			while (nextKey < std::size(extraKeys) && std::find(keys.begin(), keys.end(), extraKeys[nextKey]) != keys.end()) ++nextKey;
			if (nextKey < std::size(extraKeys)) keys[i] = extraKeys[nextKey++];
		}
		for (size_t i = actions.size(); i > 0; --i)
		{
			const auto* action = actions[i - 1];
			addItem(action->type, action->name, &id, keys[i - 1], action);
		}
		// Start at the first YAML entries, which are at the top of this upward-growing list.
		_firstItem = id;
		return;
	}

	// throwing (if not a fixed weapon)
	if (!weapon->isFixed() && weapon->getCostThrow().Time > 0)
	{
		addItem(BA_THROW, "STR_THROW", &id, Options::keyBattleActionItem5);
	}

	if (weapon->isPsiRequired() && _action->actor->getBaseStats()->psiSkill <= 0)
	{
		return;
	}

	if (weapon->isManaRequired() && _action->actor->getOriginalFaction() == FACTION_PLAYER)
	{
		if (!_game->getMod()->isManaFeatureEnabled() || !_game->getSavedGame()->isManaUnlocked(_game->getMod()))
		{
			return;
		}
	}

	// priming
	if (weapon->getFuseTimerType() != BFT_NONE)
	{
		bool normalWeapon = weapon->getBattleType() != BT_GRENADE && weapon->getBattleType() != BT_FLARE && weapon->getBattleType() != BT_PROXIMITYGRENADE;
		if (_action->weapon->getFuseTimer() == -1)
		{
			if (weapon->getCostPrime().Time > 0)
			{
				addItem(BA_PRIME, weapon->getPrimeActionName(), &id, normalWeapon ? SDLK_UNKNOWN : Options::keyBattleActionItem1);
			}
		}
		else
		{
			if (weapon->getCostUnprime().Time > 0 && !weapon->getUnprimeActionName().empty())
			{
				addItem(BA_UNPRIME, weapon->getUnprimeActionName(), &id, normalWeapon ? SDLK_UNKNOWN : Options::keyBattleActionItem2);
			}
		}
	}

	if (weapon->getBattleType() == BT_FIREARM)
	{
		bool isLauncher = _action->weapon->getCurrentWaypoints() != 0;
		int slotLauncher = _action->weapon->getActionConf(BA_LAUNCH)->ammoSlot;
		int slotSnap = _action->weapon->getActionConf(BA_SNAPSHOT)->ammoSlot;
		int slotAuto = _action->weapon->getActionConf(BA_AUTOSHOT)->ammoSlot;

		if ((!isLauncher || slotLauncher != slotAuto) && weapon->getCostAuto().Time > 0)
		{
			addItem(BA_AUTOSHOT, weapon->getConfigAuto()->name, &id, Options::keyBattleActionItem3);
		}

		if ((!isLauncher || slotLauncher != slotSnap) && weapon->getCostSnap().Time > 0)
		{
			addItem(BA_SNAPSHOT,  weapon->getConfigSnap()->name, &id, Options::keyBattleActionItem2);
		}

		if (isLauncher)
		{
			addItem(BA_LAUNCH, "STR_LAUNCH_MISSILE", &id, Options::keyBattleActionItem1);
		}
		else if (weapon->getCostAimed().Time > 0)
		{
			addItem(BA_AIMEDSHOT,  weapon->getConfigAimed()->name, &id, Options::keyBattleActionItem1);
		}
	}

	if (weapon->getCostMelee().Time > 0)
	{
		std::string name = weapon->getConfigMelee()->name;
		if (name.empty())
		{
			// stun rod
			if (weapon->getBattleType() == BT_MELEE && weapon->getDamageType()->ResistType == DT_STUN)
			{
				name = "STR_STUN";
			}
			else
			// melee weapon
			{
				name = "STR_HIT_MELEE";
			}
		}
		addItem(BA_HIT, name, &id, Options::keyBattleActionItem4);
	}

	// special items
	if (weapon->getBattleType() == BT_MEDIKIT)
	{
		addItem(BA_USE, weapon->getMedikitActionName(), &id, Options::keyBattleActionItem1);
	}
	else if (weapon->getBattleType() == BT_SCANNER)
	{
		addItem(BA_USE, weapon->getPsiAttackName().empty() ? "STR_USE_SCANNER" : weapon->getPsiAttackName(), &id, Options::keyBattleActionItem1);
	}
	else if (weapon->getBattleType() == BT_PSIAMP)
	{
		if (weapon->getCostMind().Time > 0)
		{
			addItem(BA_MINDCONTROL, "STR_MIND_CONTROL", &id, Options::keyBattleActionItem3);
		}
		if (weapon->getCostPanic().Time > 0)
		{
			addItem(BA_PANIC, "STR_PANIC_UNIT", &id, Options::keyBattleActionItem2);
		}
		if (weapon->getCostUse().Time > 0)
		{
			addItem(BA_USE, weapon->getPsiAttackName(), &id, Options::keyBattleActionItem1);
		}
	}
	else if (weapon->getBattleType() == BT_MINDPROBE)
	{
		addItem(BA_USE, weapon->getPsiAttackName().empty() ? "STR_USE_MIND_PROBE" : weapon->getPsiAttackName(), &id, Options::keyBattleActionItem1);
	}

}

/**
 * Deletes the ActionMenuState.
 */
ActionMenuState::~ActionMenuState()
{

}

/**
 * Init function.
 */
void ActionMenuState::init()
{
	layoutMenu();
	if (_actionMenu.empty())
	{
		// Item don't have any actions, close popup.
		_game->popState();
	}
}

/**
 * Creates a menu item shared by the action and skill menus.
 */
void ActionMenuState::createMenuItem(int id)
{
	auto* item = new ActionMenuItem(id, _game, 0, 0);
	_actionMenu.push_back(item);
	_menuVariants.push_back(nullptr);
	add(item);
	item->onMouseClick((ActionHandler)&ActionMenuState::btnActionMenuItemClick);
}

void ActionMenuState::layoutMenu()
{
	const int count = static_cast<int>(_actionMenu.size());
	const int capacity = std::max(1, (Options::baseYResolution - 16) / ActionMenuItem::HEIGHT);
	_firstItem = std::max(0, std::min(_firstItem, std::max(0, count - capacity)));
	const int shown = std::min(count, capacity);
	const int x = std::max(0, std::min(_menuX, Options::baseXResolution - ActionMenuItem::WIDTH));
	const int bottom = std::max(shown * ActionMenuItem::HEIGHT, std::min(_menuY, Options::baseYResolution - 12));
	for (int i = 0; i < count; ++i)
	{
		auto* item = _actionMenu[i];
		item->setVisible(i >= _firstItem && i < _firstItem + shown);
		item->setX(x);
		item->setY(bottom - (i - _firstItem + 1) * ActionMenuItem::HEIGHT);
	}
	if (count > capacity && !_pageInfo)
	{
		_pageInfo = new Text(ActionMenuItem::WIDTH, 10);
		add(_pageInfo);
		_pageInfo->initText(_game->getMod()->getFont("FONT_BIG"), _game->getMod()->getFont("FONT_SMALL"), _game->getLanguage());
		_pageInfo->setSmall();
		_pageInfo->setHighContrast(true);
		_pageInfo->setColor(_game->getMod()->getInterface("battlescape")->getElement("actionMenu")->color);
	}
	if (_pageInfo)
	{
		_pageInfo->setVisible(count > capacity);
		_pageInfo->setX(x);
		_pageInfo->setY(bottom);
		const bool yamlOrder = !_menuVariants.empty() && _menuVariants.front();
		const int first = yamlOrder ? count - _firstItem - shown + 1 : _firstItem + 1;
		const int last = yamlOrder ? count - _firstItem : _firstItem + shown;
		_pageInfo->setText(std::to_string(first) + "-" + std::to_string(last) + " / " + std::to_string(count) + "  [PgUp/PgDn]");
	}
}

/**
 * Adds an action with its accuracy, cost and optional keyboard shortcut.
 */
void ActionMenuState::addItem(BattleActionType ba, const std::string &name, int *id, SDLKey key, const RuleItemAction* variant)
{
	std::string s1, s2;
	BattleActionCost preview;
	preview.type = ba;
	preview.actor = _action->actor;
	preview.weapon = _action->weapon;
	preview.itemAction = variant;
	int acc = BattleUnit::getFiringAccuracy(BattleActionAttack::GetBeforeShoot(preview), _game->getMod());
	int tu = _action->actor->getActionTUs(ba, _action->weapon, variant).Time;

	if (ba == BA_THROW || ba == BA_AIMEDSHOT || ba == BA_SNAPSHOT || ba == BA_AUTOSHOT || ba == BA_LAUNCH || ba == BA_HIT)
		s1 = tr("STR_ACCURACY_SHORT").arg(Unicode::formatPercentage(acc));
	s2 = tr("STR_TIME_UNITS_SHORT").arg(tu);
	createMenuItem(*id);
	_menuVariants.back() = variant;
	_menuKeys.push_back(key);
	_actionMenu[*id]->setAction(ba, tr(name), s1, s2, tu);
	_actionMenu[*id]->setVisible(true);
	if (key != SDLK_UNKNOWN)
	{
		_actionMenu[*id]->onKeyboardPress((ActionHandler)&ActionMenuState::btnActionMenuItemClick, key);
	}
	(*id)++;
}

/**
 * Closes the window on right-click.
 * @param action Pointer to an action.
 */
void ActionMenuState::handle(Action *action)
{
	const auto* event = action->getDetails();
	if (event->type == SDL_KEYDOWN)
	{
		for (size_t i = 0; i < _menuKeys.size(); ++i)
		{
			if (_menuKeys[i] != SDLK_UNKNOWN && event->key.keysym.sym == _menuKeys[i])
			{
				action->setSender(_actionMenu[i]);
				btnActionMenuItemClick(action);
				return;
			}
		}
	}
	int scroll = 0;
	if (event->type == SDL_MOUSEBUTTONDOWN)
	{
		if (event->button.button == SDL_BUTTON_WHEELUP) scroll = 1;
		if (event->button.button == SDL_BUTTON_WHEELDOWN) scroll = -1;
	}
	if (event->type == SDL_KEYDOWN)
	{
		const int page = std::max(1, (Options::baseYResolution - 16) / ActionMenuItem::HEIGHT);
		if (event->key.keysym.sym == SDLK_PAGEUP) scroll = page;
		if (event->key.keysym.sym == SDLK_PAGEDOWN) scroll = -page;
	}
	if (scroll)
	{
		_firstItem += scroll;
		layoutMenu();
		return;
	}
	State::handle(action);
	if (action->getDetails()->type == SDL_MOUSEBUTTONDOWN && _game->isRightClick(action))
	{
		_game->popState();
	}
	else if (action->getDetails()->type == SDL_KEYDOWN)
	{
		auto key = action->getDetails()->key.keysym.sym;
		if (key == Options::keyCancel || key == Options::keyBattleUseLeftHand || key == Options::keyBattleUseRightHand)
		{
			if (key != Options::keyBattleActionItem1 &&
				key != Options::keyBattleActionItem2 &&
				key != Options::keyBattleActionItem3 &&
				key != Options::keyBattleActionItem4 &&
				key != Options::keyBattleActionItem5)
			{
				_game->popState();
			}
		}
	}
}

/**
 * Executes the action corresponding to this action menu item.
 * @param action Pointer to an action.
 */
void ActionMenuState::btnActionMenuItemClick(Action *action)
{
	_game->getSavedGame()->getSavedBattle()->getPathfinding()->removePreview();

	int btnID = -1;

	if (_game->getSavedGame()->getSavedBattle()->isPreview())
	{
		_action->result = "STR_UNABLE_TO_USE_ALIEN_ARTIFACT_UNTIL_RESEARCHED";
		_game->popState();
		return;
	}

	// got to find out which button was pressed
	for (size_t i = 0; i < std::size(_actionMenu) && btnID == -1; ++i)
	{
		if (action->getSender() == _actionMenu[i])
		{
			btnID = i;
		}
	}

	if (btnID != -1)
	{
		_action->type = _actionMenu[btnID]->getAction();
		_action->itemAction = _menuVariants[btnID];
		_action->skillRules = nullptr;
		_action->updateTU();

		handleAction();
	}
}

void ActionMenuState::handleAction()
{
	// reset potential garbage from the previous action
	_action->terrainMeleeTilePart = 0;

	{
		const RuleItem *weapon = _action->weapon->getRules();
		bool newHitLog = false;
		std::string actionResult = "STR_UNKNOWN"; // needs a non-empty default/fall-back !

		if (_action->type != BA_THROW &&
			_action->actor->getOriginalFaction() == FACTION_PLAYER &&
			!_game->getSavedGame()->isResearched(weapon->getRequirements()))
		{
			_action->result = "STR_UNABLE_TO_USE_ALIEN_ARTIFACT_UNTIL_RESEARCHED";
			_game->popState();
		}
		else if (_action->type != BA_THROW &&
			!_game->getSavedGame()->getSavedBattle()->canUseWeapon(_action->weapon, _action->actor, false, _action->type, &actionResult, _action->getItemAction()))
		{
			_action->result = actionResult;
			_game->popState();
		}
		else if (_action->type == BA_PRIME)
		{
			const BattleFuseType fuseType = weapon->getFuseTimerType();
			if (fuseType == BFT_SET)
			{
				_game->pushState(new PrimeGrenadeState(_action, false, 0));
			}
			else
			{
				_action->value = weapon->getFuseTimerDefault();
				_game->popState();
			}
		}
		else if (_action->type == BA_UNPRIME)
		{
			_game->popState();
		}
		else if (_action->type == BA_USE && weapon->getBattleType() == BT_MEDIKIT)
		{
			BattleUnit *targetUnit = 0;
			TileEngine *tileEngine = _game->getSavedGame()->getSavedBattle()->getTileEngine();
			for (auto* bu : *_game->getSavedGame()->getSavedBattle()->getUnits())
			{
				// we can heal a unit that is at the same position, unconscious and healable(=woundable)
				if (bu->getPosition() == _action->actor->getPosition() &&
					bu != _action->actor &&
					bu->getStatus() == STATUS_UNCONSCIOUS &&
					(bu->isWoundable() || weapon->getAllowTargetImmune()) &&
					weapon->getAllowTargetGround())
				{
					if (bu->isBigUnit())
					{
						// never EVER apply anything to 2x2 units on the ground
						continue;
					}
					if ((weapon->getAllowTargetFriendGround() && bu->getOriginalFaction() == FACTION_PLAYER) ||
						(weapon->getAllowTargetNeutralGround() && bu->getOriginalFaction() == FACTION_NEUTRAL) ||
						(weapon->getAllowTargetHostileGround() && bu->getOriginalFaction() == FACTION_HOSTILE))
					{
						targetUnit = bu;
						break; // loop finished
					}
				}
			}
			if (!targetUnit && weapon->getAllowTargetStanding())
			{
				if (tileEngine->validMeleeRange(
					_action->actor->getPosition(),
					_action->actor->getDirection(),
					_action->actor,
					0, &_action->target, false))
				{
					Tile *tile = _game->getSavedGame()->getSavedBattle()->getTile(_action->target);
					if (tile != 0 && tile->getUnit() && (tile->getUnit()->isWoundable() || weapon->getAllowTargetImmune()))
					{
						if ((weapon->getAllowTargetFriendStanding() && tile->getUnit()->getOriginalFaction() == FACTION_PLAYER) ||
							(weapon->getAllowTargetNeutralStanding() && tile->getUnit()->getOriginalFaction() == FACTION_NEUTRAL) ||
							(weapon->getAllowTargetHostileStanding() && tile->getUnit()->getOriginalFaction() == FACTION_HOSTILE))
						{
							targetUnit = tile->getUnit();
						}
					}
				}
			}
			if (!targetUnit && weapon->getAllowTargetSelf())
			{
				targetUnit = _action->actor;
			}
			if (targetUnit)
			{
				_game->popState();
				BattleMediKitType type = weapon->getMediKitType();
				if (type)
				{
					if ((type == BMT_HEAL && _action->weapon->getHealQuantity() > 0) ||
						(type == BMT_STIMULANT && _action->weapon->getStimulantQuantity() > 0) ||
						(type == BMT_PAINKILLER && _action->weapon->getPainKillerQuantity() > 0))
					{
						if (_action->spendTU(&_action->result))
						{
							switch (type)
							{
							case BMT_HEAL:
								if (targetUnit->getFatalWounds())
								{
									for (int i = 0; i < BODYPART_MAX; ++i)
									{
										if (targetUnit->getFatalWound((UnitBodyPart)i))
										{
											tileEngine->medikitUse(_action, targetUnit, BMA_HEAL, (UnitBodyPart)i);
											tileEngine->medikitRemoveIfEmpty(_action);
											break;
										}
									}
								}
								else
								{
									tileEngine->medikitUse(_action, targetUnit, BMA_HEAL, BODYPART_TORSO);
									tileEngine->medikitRemoveIfEmpty(_action);
								}
								break;
							case BMT_STIMULANT:
								tileEngine->medikitUse(_action, targetUnit, BMA_STIMULANT, BODYPART_TORSO);
								tileEngine->medikitRemoveIfEmpty(_action);
								break;
							case BMT_PAINKILLER:
								tileEngine->medikitUse(_action, targetUnit, BMA_PAINKILLER, BODYPART_TORSO);
								tileEngine->medikitRemoveIfEmpty(_action);
								break;
							case BMT_NORMAL:
								break;
							}
						}
					}
					else
					{
						_action->result = "STR_NO_USES_LEFT";
					}
				}
				else
				{
					_game->pushState(new MedikitState(targetUnit, _action, tileEngine));
				}
			}
			else
			{
				_action->result = "STR_THERE_IS_NO_ONE_THERE";
				_game->popState();
			}
		}
		else if (_action->type == BA_USE && weapon->getBattleType() == BT_SCANNER)
		{
			// spend TUs first, then show the scanner
			if (_action->spendTU(&_action->result))
			{
				_game->popState();
				_game->pushState (new ScannerState(_action));
			}
			else
			{
				_game->popState();
			}
		}
		else if (_action->type == BA_LAUNCH)
		{
			// check beforehand if we have enough time units
			if (!_action->haveTU(&_action->result))
			{
				//nothing
			}
			else if (!_action->weapon->getAmmoForAction(BA_LAUNCH, &_action->result))
			{
				//nothing
			}
			else
			{
				_action->targeting = true;
				newHitLog = true;
			}
			_game->popState();
		}
		else if (_action->type == BA_HIT)
		{
			// check beforehand if we have enough time units
			if (!_action->haveTU(&_action->result))
			{
				//nothing
			}
			else if (!_game->getSavedGame()->getSavedBattle()->getTileEngine()->validMeleeRange(
				_action->actor->getPosition(),
				_action->actor->getDirection(),
				_action->actor,
				0, &_action->target))
			{
				if (!_game->getSavedGame()->getSavedBattle()->getTileEngine()->validTerrainMeleeRange(_action))
				{
					_action->result = "STR_THERE_IS_NO_ONE_THERE";
				}
			}
			else
			{
				newHitLog = true;
			}
			_game->popState();
		}
		else
		{
			_action->targeting = true;
			newHitLog = true;
			_game->popState();
		}

		// meleeAttackBState won't be available to clear the action type, do it here instead.
		if (_action->type == BA_HIT && !_action->result.empty())
		{
			_action->type = BA_NONE;
		}

		if (newHitLog)
		{
			_game->getSavedGame()->getSavedBattle()->appendToHitLog(HITLOG_PLAYER_FIRING, FACTION_PLAYER, tr(weapon->getType()));
		}
	}
}

/**
 * Updates the scale.
 * @param dX delta of X;
 * @param dY delta of Y;
 */
void ActionMenuState::resize(int &dX, int &dY)
{
	_menuX += dX / 2;
	_menuY += dY;
	layoutMenu();
}

}
