# Скриптовые наложения

Адаптация API [MeridianOXC/OpenXcom PR #120](https://github.com/MeridianOXC/OpenXcom/pull/120), head `54855bfea31a125a6b4cfedc88ef182f166aa63b`. Существующая отрисовка OpenXcomExMore сохранена; класс InventoryItemSprite и удаление старых методов рисования не перенесены.

## События

| Событие | Локальное правило | Аргументы |
|---|---|---|
| inventorySpriteOverlay | items | item, battle_game, overlay, render_context, anim_frame |
| handOverlay | items | item, battle_game, overlay, render_context, anim_frame |
| unitPaperdollOverlay | armors | unit, battle_game, overlay, anim_frame |
| unitRankOverlay | armors | unit, battle_game, overlay, anim_frame |

Все четыре события поддерживают глобальные обработчики в `extended.scripts` с отрицательными/положительными offset и локальное тело в `scripts`. Завершаются `return;`. Указатели item/unit/battle_game доступны для чтения, overlay и render_context — для изменения. Состояние наложения существует только на время одного вызова; сохранять указатели на него нельзя.

inventorySpriteOverlay вызывается для BIGOB в инвентаре (включая землю), руках панели боя, просмотре инвентаря противника, под курсором, в окне боеприпаса и статье Уфопедии. handOverlay вызывается для области руки/поднятого предмета/окна боеприпаса, но не для статьи Уфопедии. В Уфопедии item и battle_game равны null, anim_frame=0; рисование текста и получение ресурсов работают и без боя. Событие на карте для FLOOROB не добавлено.

unitPaperdollOverlay рисует в отдельном слое над фигурой солдата инвентаря. Его область соответствует всей поверхности фигуры (обычно 320×200), а не только силуэту. unitRankOverlay рисует над областью ранга/аватара панели боя, не над юнитом на карте и не над отдельным TinyRanks. Оба слоя очищаются и обновляются, поэтому исчезающие/движущиеся элементы не оставляют следов.

Сначала выполняются прежние select/recolor-обработчики изображения, затем наложения, затем штатные индикаторы соответствующего интерфейса. Глобальные offset определяют порядок внутри каждого нового события. Они не изменяют порядок событий разных видов.

## API рисования

`overlay` имеет тип `ptre SpriteOverlay`. Все координаты относительно верхнего левого угла его области. Рисование обрезается по границам области и целевой поверхности, не изменяет исходный ресурс. Палитровый индекс 0 прозрачен; для чёрного используйте 15.

```text
overlay.getWidth width;
overlay.getHeight height;
overlay.blit sprite x y;
overlay.blitCrop sprite x1 y1 x2 y2;
overlay.blitShade sprite x y shade;
overlay.blitShadeRecolor sprite x y shade colorGroup;
overlay.drawNumber number x y width height color;
overlay.drawText "Текст" x y width height color;
overlay.drawLine x1 y1 x2 y2 color;
overlay.drawRect x1 y1 x2 y2 color;
overlay.drawCirc x y radius color;
```

drawRect — заполненный прямоугольник с исключёнными правой/нижней границами: `0 0 4 3` занимает 4×3 пикселя. blitCrop рисует спрайт от (0,0), ограничивая область назначения прямоугольником с такими же исключёнными границами. drawLine включает конечную точку. drawCirc — заполненный круг.

В blitShadeRecolor colorGroup — номер ряда **0..15**, не готовый индекс палитры и не внутреннее значение Surface::blitNShade «ряд+1». shade ограничивается 0..16; 16 даёт чёрный. Нулевой sprite безопасно пропускается. drawNumber принимает неотрицательное число. drawText использует текущие шрифты и язык игры; строка передаётся как текст, а не автоматически переводимый ключ локализации. Размеры текстовых поверхностей ограничены 1..4096, радиус — 0..4096; неподдерживаемые координаты SDL отклоняются.

Изменения относительно исходного PR: исправлены координаты blitShadeRecolor, геометрия drawRect и размеры drawNumber; добавлено ограничение области рисования. blitShadeCrop, как и в исходной регистрации PR, не экспортируется в скриптовый API.

## Получение ресурсов и данных

```text
var ptr Sprite sprite;
rules.getSpriteFromSet sprite "BIGOBS.PCK" 3000;
rules.getNamedSprite sprite "BigWoundIndicator";
sprite.getWidth width;
sprite.getHeight height;
rules.getInterfaceElementColor color "inventory" "numStack";
rules.getInterfaceElementColor2 color "inventory" "numStack";
```

Отсутствующие ресурсы возвращают null, отсутствующие/неопределённые цвета — −1. Индекс в getSpriteFromSet уже должен быть индексом загруженного набора; автоматической конвертации локального индекса мода здесь нет.

Дополнительные методы RuleItem: getClipSize, getMediKitType, getMaxHealQuantity, getMaxPainKillerQuantity, getMaxStimulantQuantity, getInvWidth, getInvHeight, getBigSpriteIndex. Константы BMT_NORMAL/HEAL/PAINKILLER/STIMULANT; опечатка BMT_PAINKILER сохранена как совместимый псевдоним. BattleUnit: getStatus, indicatorsAreEnabled, hasNegativeHealthRegen и константы STATUS_*.

## Контекст и штатные индикаторы

`render_context` имеет тип `ptre InvSpriteContext`:

```text
render_context.getContext context;
render_context.isInContext result SCREEN_INVENTORY;
render_context.getRenderOptions options;
render_context.isRenderOptionSet result DRAW_AMMO;
render_context.unsetRenderOptions DRAW_AMMO;
render_context.setRenderOptions DRAW_AMMO;
```

Контекст — маска SCREEN_INVENTORY, SCREEN_BATTSCAPE (написание из PR), SCREEN_ALIEN_INV, SCREEN_UFOPEDIA с дополнительными CURSOR_HOVER, CURSOR_SELECTED, INVENTORY_AMMO. isInContext/isRenderOptionSet возвращают пересечение масок, не обязательно 1; проверяйте `neq result 0`.

Опции: DRAW_GRENADE_INDICATOR, DRAW_CORPSE_STATE, DRAW_FATAL_WOUNDS, DRAW_AMMO, DRAW_MEDIKIT, DRAW_TWOHAND_INDICATOR. Изменения общие для inventorySpriteOverlay и последующего handOverlay данного предмета и сбрасываются при следующей отрисовке.

В этой адаптации опции управляют **имеющимися** индикаторами конкретного пути отрисовки. Установка DRAW_AMMO в статье Уфопедии не создаст счётчик патронов, которого там нет. Проверки типа предмета, бессознательного состояния, ненулевых ран и indicatorsAreEnabled продолжают действовать. Индикаторы реакции и запрета реакции форка сохраняются и этими флагами не управляются. Переход к универсальному InventoryItemSprite остаётся отдельной задачей.

## Пример

См. `docs/examples/sprite-overlays.rul`. Это необязательный пример, не подключённый к игровому ruleset. Он показывает оставшийся заряд лечения аптечки, метку в области руки, анимированные маркеры на фигуре солдата и возле ранга, а также квадрат на изображении предмета в Уфопедии. Для использования скопируйте выбранные обработчики в собственный ruleset; уникальность offset проверяется относительно остальных файлов мода.

Для ручной проверки текущего OpenSCP:

1. Скопируйте файл примера в `bin/standard/OpenSCP/Ruleset/zz_sprite_overlays_demo.rul` и перезапустите игру с новой сборкой `bin/Win32/Release/OpenXcom.exe`.
2. Откройте инвентарь солдата с аптечкой: в верхнем левом углу аптечки должно появиться число оставшихся зарядов лечения. Переложите её в руку, на землю и поднимите курсором; число должно следовать за предметом.
3. На фигуре солдата в инвентаре должна двигаться короткая вертикальная метка. Переключайте солдат: метка не должна оставлять следов.
4. На боевом экране проверьте короткую линию внизу области занятой руки и движущуюся метку возле ранга/портрета выбранного солдата.
5. Откройте статью Уфопедии с изображением предмета, в том числе из главного меню без загруженного боя: в верхнем левом углу изображения должен появиться квадрат 4×4 пикселя.
6. Проверьте обычные счётчики патронов, аптечки и индикаторы гранат; затем загрузите `Recolor_bug.sav` и проверьте прежнюю перекраску тела в позиции `[41,40,8]`.

После проверки удалите только `zz_sprite_overlays_demo.rul` и перезапустите игру. Демонстрация использует индекс цвета 48; размеры указаны в исходных пикселях интерфейса до масштабирования.

## Проверки

`tests/run-sprite-overlays.ps1` собирает Win32 Release и запускает проверки геометрии, палитры, crop, null-ресурсов и настоящего исполнения четырёх скриптовых хуков. `-SkipBuild` использует уже собранные объекты. Полный снимок API текущего движка записывается в `obj/sprite-overlay-tests/API.log`; это снимок регистраций без загрузки тегов конкретного мода. Старый `OpenSCP/Ruleset/API.log` не перезаписывается.

Для визуальной приёмки: проверить штатный интерфейс без новых скриптов, необязательный пример, фигуру солдата при переключении юнитов, статью Уфопедии без боя, крупные предметы, FPV и перекраску тел в Recolor_bug.sav. Формат сохранений не меняется.
