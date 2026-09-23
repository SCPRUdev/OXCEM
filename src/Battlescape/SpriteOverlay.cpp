/*
 * Copyright 2010-2026 OpenXcom Developers.
 * Adapted from MeridianOXC/OpenXcom PR #120 (54855bfea31a125a6b4cfedc88ef182f166aa63b).
 * This file is part of OpenXcom, distributed under the GNU GPL v3 or later.
 */
#include "SpriteOverlay.h"
#include <algorithm>
#include "../Engine/ScriptBind.h"
#include "../Engine/Language.h"
#include "../Engine/Game.h"
#include "../Savegame/SavedGame.h"
#include "../Interface/NumberText.h"
#include "../Interface/Text.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"

namespace OpenXcom
{
namespace
{
// SDL_gfx uses signed 16-bit coordinates. Reject inputs it cannot represent.
bool coordinate(int v) { return v >= -32768 && v <= 32767; }
bool dimensions(int w, int h) { return w > 0 && h > 0 && w <= 4096 && h <= 4096; }
void getContext(const InventorySpriteContext* c, int& out) { out = c ? c->renderContext : 0; }
void isInContext(const InventorySpriteContext* c, int& out, int mask) { out = c ? c->renderContext & mask : 0; }
void getOptions(const InventorySpriteContext* c, int& out) { out = c ? c->options : 0; }
void isOption(const InventorySpriteContext* c, int& out, int mask) { out = c ? c->options & mask : 0; }
void setOptions(InventorySpriteContext* c, int mask) { if (c) c->options |= mask & 63; }
void unsetOptions(InventorySpriteContext* c, int mask) { if (c) c->options &= ~mask; }
}

SpriteOverlay::SpriteOverlay(Surface& target, SDL_Rect area, const SavedBattleGame* save, const Mod* mod, Language* language)
	: _target(target), _bounds(area), _save(save), _mod(mod), _language(language)
{
}

SDL_Rect SpriteOverlay::bounds(int x, int y, int w, int h)
{
	if (!coordinate(x) || !coordinate(y) || !dimensions(w, h)) return SDL_Rect{0, 0, 0, 0};
	return SDL_Rect{static_cast<Sint16>(x), static_cast<Sint16>(y), static_cast<Uint16>(w), static_cast<Uint16>(h)};
}

SDL_Rect SpriteOverlay::itemBounds(const RuleItem& rule, int x, int y)
{
	return bounds(x, y, rule.getInventoryWidth() * RuleInventory::SLOT_W, rule.getInventoryHeight() * RuleInventory::SLOT_H);
}

Surface* SpriteOverlay::layer()
{
	if (!_layer && dimensions(_bounds.w, _bounds.h))
	{
		_layer.reset(new Surface(_bounds.w, _bounds.h));
		_layer->setPalette(_target.getPalette());
	}
	return _layer.get();
}

void SpriteOverlay::itemOverlays(Game* game, Surface& target, const BattleItem* item, int x, int y,
	InventorySpriteContext& context, int frame, const SDL_Rect* handBounds)
{
	if (!item) return;
	const auto rule = item->getRules();
	const auto save = game->getSavedGame()->getSavedBattle();
	SpriteOverlay(target, itemBounds(*rule, x, y), save, game->getMod(), game->getLanguage())
		.drawItem(*rule, item, context, frame);
	if (handBounds)
		SpriteOverlay(target, *handBounds, save, game->getMod(), game->getLanguage())
			.drawItem(*rule, item, context, frame, true);
}

void SpriteOverlay::finish()
{
	if (_layer)
	{
		_layer->blitNShade(&_target, _bounds.x, _bounds.y, 0);
		_layer.reset();
	}
}

void SpriteOverlay::finishMasked(GraphSubset mask, int shade)
{
	if (_layer)
	{
		_layer->blitNShade(&_target, _bounds.x, _bounds.y, shade, mask);
		_layer.reset();
	}
}

void SpriteOverlay::drawItem(const RuleItem& rule, const BattleItem* item, InventorySpriteContext& context, int frame, bool hand)
{
	if (hand)
		ModScript::scriptCallback<ModScript::HandOverlay>(&rule, item, _save, this, &context, frame);
	else
		ModScript::scriptCallback<ModScript::InventorySpriteOverlay>(&rule, item, _save, this, &context, frame);
	finish();
}

void SpriteOverlay::blit(const Surface* sprite, int x, int y) { blitShade(sprite, x, y, 0); }
void SpriteOverlay::blitShade(const Surface* sprite, int x, int y, int shade)
{
	if (sprite && coordinate(x) && coordinate(y) && layer())
		sprite->blitNShade(_layer.get(), x, y, std::clamp(shade, 0, 16));
}
void SpriteOverlay::blitCrop(const Surface* sprite, int x1, int y1, int x2, int y2)
{
	blitShadeCrop(sprite, 0, 0, 0, x1, y1, x2, y2);
}
void SpriteOverlay::blitShadeCrop(const Surface* sprite, int shade, int x, int y, int x1, int y1, int x2, int y2)
{
	if (!sprite || !coordinate(x) || !coordinate(y)) return;
	x1 = std::max(0, x1); y1 = std::max(0, y1);
	x2 = std::min(getWidth(), x2); y2 = std::min(getHeight(), y2);
	if (x2 > x1 && y2 > y1 && layer())
		sprite->blitNShade(_layer.get(), x, y, std::clamp(shade, 0, 16), GraphSubset({x1, x2}, {y1, y2}));
}
void SpriteOverlay::blitShadeRecolor(const Surface* sprite, int x, int y, int shade, int color)
{
	if (sprite && coordinate(x) && coordinate(y) && layer())
		sprite->blitNShade(_layer.get(), x, y, std::clamp(shade, 0, 16), false, std::clamp(color, 0, 15) + 1);
}
void SpriteOverlay::drawNumber(int value, int x, int y, int w, int h, int color)
{
	if (value < 0 || !coordinate(x) || !coordinate(y) || !dimensions(w, h) || !layer()) return;
	NumberText number(w, h, x, y);
	number.setPalette(_target.getPalette());
	number.setColor(static_cast<Uint8>(color));
	number.setBordered(false);
	number.setValue(value);
	number.blit(_layer->getSurface());
}
void SpriteOverlay::drawText(const std::string& text, int x, int y, int w, int h, int color)
{
	if (!_mod || !_language || !coordinate(x) || !coordinate(y) || !dimensions(w, h) || !layer()) return;
	Text label(w, h, x, y);
	label.setPalette(_target.getPalette());
	label.initText(_mod->getFont("FONT_BIG"), _mod->getFont("FONT_SMALL"), _language);
	label.setSmall();
	label.setColor(static_cast<Uint8>(color));
	label.setText(text);
	label.blit(_layer->getSurface());
}
void SpriteOverlay::drawLine(int x1, int y1, int x2, int y2, int color)
{
	if (coordinate(x1) && coordinate(y1) && coordinate(x2) && coordinate(y2) && layer())
		_layer->drawLine(x1, y1, x2, y2, static_cast<Uint8>(color));
}
void SpriteOverlay::drawRect(int x1, int y1, int x2, int y2, int color)
{
	x1 = std::max(0, x1); y1 = std::max(0, y1);
	x2 = std::min(getWidth(), x2); y2 = std::min(getHeight(), y2);
	if (x2 > x1 && y2 > y1 && layer())
		_layer->drawRect(x1, y1, x2 - x1, y2 - y1, static_cast<Uint8>(color));
}
void SpriteOverlay::drawCirc(int x, int y, int radius, int color)
{
	if (coordinate(x) && coordinate(y) && radius >= 0 && radius <= 4096 && layer())
		_layer->drawCircle(x, y, radius, static_cast<Uint8>(color));
}

void InventorySpriteContext::ScriptRegister(ScriptParserBase* parser)
{
	Bind<InventorySpriteContext> b{parser};
	b.add<&getContext>("getContext");
	b.add<&isInContext>("isInContext");
	b.add<&getOptions>("getRenderOptions");
	b.add<&isOption>("isRenderOptionSet");
	b.add<&setOptions>("setRenderOptions", "Enable options supported by the current host; eligibility checks still apply");
	b.add<&unsetOptions>("unsetRenderOptions");
	b.addCustomConst("SCREEN_INVENTORY", SCREEN_INVENTORY);
	b.addCustomConst("SCREEN_BATTSCAPE", SCREEN_BATTSCAPE);
	b.addCustomConst("SCREEN_ALIEN_INV", SCREEN_ALIEN_INV);
	b.addCustomConst("SCREEN_UFOPEDIA", SCREEN_UFOPEDIA);
	b.addCustomConst("CURSOR_HOVER", CURSOR_HOVER);
	b.addCustomConst("CURSOR_SELECTED", CURSOR_SELECTED);
	b.addCustomConst("INVENTORY_AMMO", INVENTORY_AMMO);
	b.addCustomConst("DRAW_GRENADE_INDICATOR", DRAW_GRENADE);
	b.addCustomConst("DRAW_CORPSE_STATE", DRAW_CORPSE_STATE);
	b.addCustomConst("DRAW_FATAL_WOUNDS", DRAW_FATAL_WOUNDS);
	b.addCustomConst("DRAW_AMMO", DRAW_AMMO);
	b.addCustomConst("DRAW_MEDIKIT", DRAW_MEDIKIT);
	b.addCustomConst("DRAW_TWOHAND_INDICATOR", DRAW_TWOHAND);
	b.addCustomConst("INV_SLOT_W", RuleInventory::SLOT_W);
	b.addCustomConst("INV_SLOT_H", RuleInventory::SLOT_H);
	b.addCustomConst("INV_HAND_SLOT_COUNT_W", RuleInventory::HAND_W);
	b.addCustomConst("INV_HAND_SLOT_COUNT_H", RuleInventory::HAND_H);
	b.addCustomConst("INV_HAND_OVERLAY_W", RuleInventory::HAND_W * RuleInventory::SLOT_W);
	b.addCustomConst("INV_HAND_OVERLAY_H", RuleInventory::HAND_H * RuleInventory::SLOT_H);
}

void SpriteOverlay::ScriptRegister(ScriptParserBase* parser)
{
	parser->registerRawPointerType<Surface>("Sprite");
	Bind<SpriteOverlay> b{parser};
	b.add<&SpriteOverlay::blit>("blit", "sprite x y");
	b.add<&SpriteOverlay::blitCrop>("blitCrop", "sprite x1 y1 x2 y2; destination crop, exclusive ends");
	b.add<&SpriteOverlay::blitShade>("blitShade", "sprite x y shade (0..16)");
	b.add<&SpriteOverlay::blitShadeRecolor>("blitShadeRecolor", "sprite x y shade colorGroup (0..15)");
	b.add<&SpriteOverlay::drawNumber>("drawNumber", "nonnegativeNumber x y width height paletteIndex");
	b.add<&SpriteOverlay::drawText>("drawText", "text x y width height paletteIndex; current language fonts");
	b.add<&SpriteOverlay::drawLine>("drawLine", "x1 y1 x2 y2 paletteIndex");
	b.add<&SpriteOverlay::drawRect>("drawRect", "x1 y1 x2 y2 paletteIndex; exclusive ends");
	b.add<&SpriteOverlay::drawCirc>("drawCirc", "x y radius paletteIndex");
	b.add<&SpriteOverlay::getWidth>("getWidth");
	b.add<&SpriteOverlay::getHeight>("getHeight");
}

ModScript::InventorySpriteOverlayParser::InventorySpriteOverlayParser(ScriptGlobal* shared, const std::string& name, Mod* mod)
	: ScriptParserEvents{shared, name, "item", "battle_game", "overlay", "render_context", "anim_frame"}
{
	BindBase b{this}; b.addCustomPtr<const Mod>("rules", mod);
}
ModScript::HandOverlayParser::HandOverlayParser(ScriptGlobal* shared, const std::string& name, Mod* mod)
	: ScriptParserEvents{shared, name, "item", "battle_game", "overlay", "render_context", "anim_frame"}
{
	BindBase b{this}; b.addCustomPtr<const Mod>("rules", mod);
}
ModScript::UnitPaperdollOverlayParser::UnitPaperdollOverlayParser(ScriptGlobal* shared, const std::string& name, Mod* mod)
	: ScriptParserEvents{shared, name, "unit", "battle_game", "overlay", "anim_frame"}
{
	BindBase b{this}; b.addCustomPtr<const Mod>("rules", mod);
}
ModScript::UnitRankOverlayParser::UnitRankOverlayParser(ScriptGlobal* shared, const std::string& name, Mod* mod)
	: ScriptParserEvents{shared, name, "unit", "battle_game", "overlay", "anim_frame"}
{
	BindBase b{this}; b.addCustomPtr<const Mod>("rules", mod);
}
ModScript::UnitSpriteOverlayParser::UnitSpriteOverlayParser(ScriptGlobal* shared, const std::string& name, Mod* mod)
	: ScriptParserEvents{shared, name, "unit", "battle_game", "overlay", "anim_frame", "unit_part", "shade"}
{
	BindBase b{this}; b.addCustomPtr<const Mod>("rules", mod);
}
}
