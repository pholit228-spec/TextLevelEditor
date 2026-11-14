# Инструкция для ИИ: как писать уровень GD текстом

Скопируй этот файл ИИ целиком и добавь: «Сделай уровень (тема/сложность/длина…). Выдай ТОЛЬКО скрипт одним блоком кода».
Потом скопируй ответ ИИ и нажми в редакторе кнопку **Paste** (или положи в `level.txt` и нажми **File**).

## Правила языка
- Одна команда на строку (или через `; `). Комментарии: `#` или `//`.
- Координаты в юнитах GD: **30 = 1 блок**. Поверхность земли y = 0, блок на земле имеет центр **y = 15**, x = 0 — старт уровня. Уровень идёт вправо.
- Скорости (юнитов/сек): 1x ≈ 311, 2x ≈ 388, 3x ≈ 468, 4x ≈ 577.
- Числа принимают выражения без пробелов: `600+$i*300`, `(1+$i/2)`. Функции: abs floor ceil round sqrt sin cos (градусы) min max pow mod.
- Любое свойство можно задать числом-ключом: `57=1.2` (группы 1 и 2), `28=300`.

## Объекты
```
place ID x y [rot=90] [scale=1.5] [flipx=on] [flipy=on] [group=1,2] [color=5] [dcolor=3] [z=-1] [zorder=2] [layer=1]
```
ID — число или имя: `block spike spike_small startpos coin`,
порталы `cube ship ball ufo wave robot spider swing speed05 speed1 speed2 speed3 speed4 grav_down grav_up mini big dual single mirror_on mirror_off`,
орбы/пэды `yellow_orb yellow_pad pink_orb pink_pad blue_orb blue_pad green_orb red_orb red_pad black_orb dash_green dash_pink`.
Остальные объекты — просто по числовому ID.

Массовое размещение:
```
fill ID x1 y1 x2 y2 [step=30]        # прямоугольник
line ID x1 y1 x2 y2 [step=30]        # линия (в т.ч. диагональ)
repeat ID x y COUNT [dx dy]          # ряд, по умолчанию dx=30 dy=0
text x y "Привет" [scale=0.5]        # текстовый объект
raw 1,8,2,15,3,15                    # сырая строка одного объекта
```
Циклы и переменные:
```
let W = 30
for i 0 9            # включительно, шаг по умолчанию 1: for i 0 100 10
  place spike 600+$i*300 15
endfor
```
`units block` — координаты в блоках (клетка 0,0 = центр 15,15); `units px` — вернуть юниты.

## Удаление / перемещение / покраска
Селектор: `ID` (или `*` = любой) + необязательно `x y` (точка), `in x1 y1 x2 y2` (область), `ingroup=N`, `tol=0.6`.
```
del spike 600 15
del * in 0 0 300 300
move block 1200 15 to 1200 45      # из точки в точку
move * in 0 0 300 300 by 0 30      # сдвиг области
paint block 1200 45 color=5 dcolor=3
paint * ingroup=3 scale=2 layer=1  # любое свойство
clear                              # удалить всё
```

## Триггеры
`trig ИМЯ [x y] свойства`. Если x y не указаны — триггер ставится автоматически высоко над уровнем (удобно для триггеров на старте).
Триггер срабатывает, когда игрок проходит его x (или `spawn=on` / `touch=on`).
Имена: `color move pulse alpha toggle spawn rotate follow shake animate touch count stop instantcount ondeath followy collision pickup bgeffecton bgeffectoff random zoom camerastatic cameraoffset reverse song timewarp camerarotate cameraedge scale advrandom end sfx`.

Свойства: `target=ГРУППА time=сек dx= dy= ease=inout|in|out|0..18 easerate= channel= r= g= b= opacity= blend= toggle=on/off spawn=on touch=on multi=on delay= deg= times= center= strength= interval= fadein= hold= fadeout= pulsemode= pulsetarget= item= count= lockx= locky=`

Примеры:
```
trig move 900 300 target=1 dx=0 dy=60 time=1 ease=inout    # сдвиг группы 1 вверх на 2 блока
trig alpha 1500 300 target=2 opacity=0 time=0.5
trig toggle 1800 300 target=3 toggle=off
trig rotate 2000 300 target=4 center=5 deg=360 time=2
trig shake 2500 300 strength=3 interval=0.05 time=2
```
Цвета каналов одной командой (ставит color-триггер на старте):
```
colorset bg 20 40 90
colorset g1 hex=204080
colorset obj 255 255 255 time=1
```
Каналы: `bg g1 ground line 3dl obj p1 p2 lbg g2` или номер (1–999 — свои). Свой цвет блоков: `colorset 5 255 80 0` + `place block 100 15 color=5`.

## Настройки уровня
```
name Мой уровень      # (латиница надёжнее)
bg 5   ground 3   groundline 1   font 1
song 12               # официальная песня по номеру
customsong 123456     # ID из Музыкальной библиотеки / Newgrounds
speed 0.5|1|2|3|4     # стартовая скорость
startmode cube|ship|ball|ufo|wave|robot|spider|swing
mini on   dual on   twoplayer on
```
Фон/земля обновятся визуально после сохранения и повторного входа в редактор.

## Советы по дизайну
- Куб на 1x прыгает примерно на 2 блока в высоту и 3–4 блока в длину: не ставь шипы вплотную друг к другу без площадок и проверяй playtest.
- Порталы ставь на отдельных x с запасом ≥ 5 блоков до препятствий.
- Для корабля/волны ограничь коридор `fill block … ` сверху и снизу.
- Начинай уровень с `colorset` фона и земли, потом препятствия слева направо.
