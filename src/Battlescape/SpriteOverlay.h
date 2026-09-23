/*
 * Copyright 2010-2026 OpenXcom Developers.
 * Adapted from MeridianOXC/OpenXcom PR #120 (54855bfea31a125a6b4cfedc88ef182f166aa63b).
 * This file is part of OpenXcom, distributed under the GNU GPL v3 or later.
 */
#pragma once
#include <memory>
#include "../Engine/Surface.h"
#include "../Mod/ModScript.h"

namespace OpenXcom
{
class Mod;
class Game;
class Language;
class BattleItem;
class RuleItem;
class SavedBattleGame;

/** Context of an inventory overlay. Options can suppress existing indicators;
 * they do not bypass the host's eligibility checks or create new indicators. */
struct InventorySpriteContext
{

	enum RenderContext
	{
		SCREEN_INVENTORY = 1 << 0, SCREEN_BATTSCAPE = 1 << 1,
		SCREEN_ALIEN_INV = 1 << 2, SCREEN_UFOPEDIA = 1 << 3,
		CURSOR_HOVER = 1 << 4, CURSOR_SELECTED = 1 << 5, INVENTORY_AMMO = 1 << 6
	};
	enum OverlayOptions
	{
		DRAW_NONE = 0, DRAW_GRENADE = 1 << 0, DRAW_CORPSE_STATE = 1 << 1,
		DRAW_FATAL_WOUNDS = 1 << 2, DRAW_AMMO = 1 << 3,
		DRAW_MEDIKIT = 1 << 4, DRAW_TWOHAND = 1 << 5
	};
	const int renderContext;
	int options;
	bool has(int option) const { return (options & option) != 0; }
	static constexpr const char* ScriptName = "InvSpriteContext";
	static void ScriptRegister(ScriptParserBase* parser);
};

/** Non-owning drawing context. Lazily allocated scratch surface clips all
 * drawing to the overlay bounds; finish() composites it onto the target. */
class SpriteOverlay
{
	Surface& _target;
	SDL_Rect _bounds;
	const SavedBattleGame* _save;
	const Mod* _mod;
	Language* _language;
	std::unique_ptr<Surface> _layer;
	Surface* layer();
public:
	SpriteOverlay(Surface& target, SDL_Rect bounds, const SavedBattleGame* save, const Mod* mod, Language* language);
	SpriteOverlay(const SpriteOverlay&) = delete;
	SpriteOverlay& operator=(const SpriteOverlay&) = delete;
	static SDL_Rect bounds(int x, int y, int width, int height);
	static SDL_Rect surfaceBounds(const Surface& target) { return bounds(0, 0, target.getWidth(), target.getHeight()); }
	static SDL_Rect itemBounds(const RuleItem& rule, int x, int y);
	static void itemOverlays(Game* game, Surface& target, const BattleItem* item, int x, int y,
		InventorySpriteContext& context, int frame, const SDL_Rect* handBounds = nullptr);
	void finish();
	int getWidth() const { return _bounds.w; }
	int getHeight() const { return _bounds.h; }

	template<typename Callback, typename Rule, typename Battle>
	void draw(const Rule& rule, const Battle* battle, int animationFrame)
	{
		ModScript::scriptCallback<Callback>(&rule, battle, _save, this, animationFrame);
		finish();
	}
	void drawItem(const RuleItem& rule, const BattleItem* item, InventorySpriteContext& context, int frame, bool hand = false);

	// Script operations. Coordinates are local; rectangles/crops use exclusive ends.
	void blit(const Surface* sprite, int x, int y);
	void blitCrop(const Surface* sprite, int x1, int y1, int x2, int y2);
	void blitShade(const Surface* sprite, int x, int y, int shade);
	void blitShadeCrop(const Surface* sprite, int shade, int x, int y, int x1, int y1, int x2, int y2);
	void blitShadeRecolor(const Surface* sprite, int x, int y, int shade, int color);
	void drawNumber(int value, int x, int y, int width, int height, int color);
	void drawText(const std::string& text, int x, int y, int width, int height, int color);
	void drawLine(int x1, int y1, int x2, int y2, int color);
	void drawRect(int x1, int y1, int x2, int y2, int color);
	void drawCirc(int x, int y, int radius, int color);
	static constexpr const char* ScriptName = "SpriteOverlay";
	static void ScriptRegister(ScriptParserBase* parser);
};
}
