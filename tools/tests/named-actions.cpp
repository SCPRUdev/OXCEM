#include "../../src/Engine/Options.h"
#include "../../src/Engine/Yaml.h"
#include "../../src/Engine/Exception.h"
#include "../../src/Mod/Mod.h"
#include "../../src/Mod/Armor.h"
#include "../../src/Mod/Unit.h"
#include "../../src/Mod/RuleItem.h"
#include "../../src/Mod/RuleSkill.h"
#include "../../src/Savegame/BattleItem.h"
#include "../../src/Savegame/BattleUnit.h"
#include "../../src/Savegame/SavedBattleGame.h"
#include "../../src/Battlescape/BattlescapeGame.h"
#include <iostream>
#include <stdexcept>
using namespace OpenXcom;
namespace OpenXcom { Exception::Exception(const std::string& message) : std::runtime_error(message) {} }
#ifdef main
#undef main
#endif
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main()
{
 try
 {
  SDL_Init(0);
  YAML::setGlobalErrorHandler();
  Mod mod;
  RuleItem rule("TEST", 0);
  auto* base = const_cast<RuleItemAction*>(rule.getConfigAuto());
  base->accuracy = 42;
  base->cost.Time = 25;
  base->cost.Energy = 3;
  base->flat.Time = true;
  base->arcing = true;
  YAML::YamlRootNodeReader yaml(YAML::YamlString(R"(
actions:
  - {id: burst, extends: auto, shots: 3, ammoSlot: -1, arcing: false}
  - id: launcher
    extends: auto
    ammoSlot: 1
    shots: 1
    range: 40
    accuracy: 35
    cost: {time: 50, energy: 7}
    flat: {time: false, energy: true}
)"), "named-actions-test");
  rule.loadActions(yaml.toBase());
  check(rule.hasActions() && rule.getActions().size() == 2, "load named list");
  const auto* burst = &rule.getActions()[0];
  const auto* launcher = &rule.getActions()[1];
  check(burst->accuracy == 42 && getDefault(burst->cost).Energy == 3, "inherit base parameters");
  int id = 0;
  BattleItem weapon(&rule, &id);
  check(!weapon.getArcingShot(BA_AUTOSHOT, burst) && weapon.getArcingShot(BA_AUTOSHOT, launcher), "independent trajectories");
  check(weapon.haveNextShotsForAction(BA_AUTOSHOT, 2, burst) && !weapon.haveNextShotsForAction(BA_AUTOSHOT, 1, launcher), "independent shot counts");
  Armor armor("TEST", 0);
  Unit unitRule("TEST");
  BattleUnit unit(&mod, &unitRule, FACTION_PLAYER, 1, nullptr, &armor, nullptr, 0, nullptr);
  unit.getBaseStats()->tu = 80;
  BattleAction action;
  action.actor = &unit;
  action.weapon = &weapon;
  action.type = BA_AUTOSHOT;
  action.itemAction = burst;
  action.updateTU();
  check(action.Time == 25 && action.Energy == 3, "flat burst cost");
  action.itemAction = launcher;
  action.updateTU();
  check(action.Time == 40 && action.Energy == 7, "percentage launcher cost");
  auto attack = BattleActionAttack::GetBeforeShoot(action);
  check(attack.item_action == launcher && !attack.damage_item, "missing launcher ammo cannot use another slot");
  action.itemAction = burst;
  check(BattleActionAttack::GetBeforeShoot(action).damage_item == &weapon, "self-use ammo selection");
  auto copied = action;
  check(BattleActionAttack::GetAferShoot(copied, &weapon).item_action == burst, "variant survives attack copy and impact");
  action.type = BA_HIT;
  check(!action.getItemAction(), "different base action rejects stale variant");
  RuleItem other("OTHER", 0);
  BattleItem otherWeapon(&other, &id);
  action.type = BA_AUTOSHOT;
  action.weapon = &otherWeapon;
  check(!action.getItemAction(), "different weapon rejects stale variant");
  int upper, lower;
  rule.calculateLimits(upper, lower, 0, BA_AUTOSHOT, launcher);
  check(upper == 40, "variant range used for accuracy falloff");
  YAML::YamlRootNodeReader replace(YAML::YamlString("actions: [{id: only, extends: throw}]"), "replace-list");
  rule.loadActions(replace.toBase());
  check(rule.getActions().size() == 1 && rule.getActions()[0].id == "only", "list replacement");
  bool rejected = false;
  try {
   YAML::YamlRootNodeReader bad(YAML::YamlString("actions: [{id: a, extends: auto}, {id: a, extends: auto}]"), "duplicate-list");
   rule.loadActions(bad.toBase());
  } catch (const std::exception&) { rejected = true; }
  check(rejected, "duplicate IDs rejected");
  auto global = mod.getScriptGlobal();
  global->beginLoad();
  ModScript::BonusStatsScripts parsers{global, &mod, "bonuses"};
  ModScript::BattleUnitScripts unitParsers{global, &mod, "unit"};
  ModScript::AccuracyMultiplierStatBonus::Container script;
  script.loadContainer("variant-probe", "var text action_id; item_action.getId action_id; if eq action_id \"launcher\"; item_action.getAmmoSlot bonus; else; set bonus -100; end; return bonus;", parsers.get<ModScript::AccuracyMultiplierStatBonus>());
  ModScript::HitUnit::Container hitScript;
  hitScript.loadContainer("hit-probe", "if neq item_action null; item_action.getAccuracy power; end; return power part side;", unitParsers.get<ModScript::HitUnit>());
  global->endLoad();
  RuleItemAction probe;
  probe.id = "launcher";
  probe.ammoSlot = 1;
  probe.accuracy = 35;
  ModScript::BonusStatsCommon::Output output{0};
  ModScript::BonusStatsCommon::Worker work{nullptr, 0, nullptr, nullptr, BA_AUTOSHOT, nullptr, &probe};
  work.execute(script, output);
  check(output.getFirst() == 1, "selected variant exposed to scripts");
  ModScript::HitUnit::Output hitOutput{0, 0, 0};
  ModScript::HitUnit::Worker hitWork{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, BA_AUTOSHOT, &probe};
  hitWork.execute(hitScript, hitOutput);
  check(hitOutput.getFirst() == 35, "hit script uses variant accuracy");
  std::cout << "Named action regression checks passed\n";
  return 0;
 }
 catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
