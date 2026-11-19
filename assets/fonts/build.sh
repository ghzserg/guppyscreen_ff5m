#!/bin/bash

docker build -t lv_font_conv .

SIZE=16
OPTS="--bpp 3 --size $SIZE --lcd \
--font NotoSans-SemiBold.ttf --range 32-127,160-255,268-271,282-283,286-287,304-305,321-322,327-328,344-345,350-351,352-353,356-357,366-367,381-382,8226,1025,1040-1103,1105 \
--font FontAwesome5-Solid+Brands+Regular.woff --range 61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62189,62212,62810,63426,63650 \
--format lvgl -o notosemi_$SIZE.c"

docker run --rm -v "$PWD:/work" -w /work lv_font_conv $OPTS

SIZE=20

OPTS="--bpp 3 --size $SIZE --lcd \
--font NotoSans-SemiBold.ttf --range 32-127,160-255,268-271,282-283,286-287,304-305,321-322,327-328,344-345,350-351,352-353,356-357,366-367,381-382,8226,1025,1040-1103,1105 \
--font FontAwesome5-Solid+Brands+Regular.woff --range 61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62189,62212,62810,63426,63650 \
--format lvgl -o notosemi_$SIZE.c"

docker run --rm -v "$PWD:/work" -w /work lv_font_conv $OPTS

# Цифры и кнопки удаления/подтверждения для Numpad, 32 px.
OPTS="--bpp 3 --size 32 --lcd \
--font NotoSans-Bold.ttf --range 48-57 \
--font FontAwesome5-Solid+Brands+Regular.woff --range 61452,62810 \
--lv-fallback notosemi_16 --format lvgl -o notobold_32_numpad.c"

docker run --rm -v "$PWD:/work" -w /work lv_font_conv $OPTS

# Температуры на Home, поле ввода Numpad, значения и полоса прогресса экрана
# печати, 22 px: цифры, единицы всех языков и буквы "ETA: {}".
OPTS="--bpp 3 --size 22 --lcd \
--font NotoSans-SemiBold.ttf --range 32,37,44-58,65,69,83,84,97,100,101,103-105,109,110,114,115,252,1054,1072,1076,1083,1084,1086,1089,1090,1095,1100 \
--lv-fallback notosemi_20 --format lvgl -o notosemi_22_digits.c"

docker run --rm -v "$PWD:/work" -w /work lv_font_conv $OPTS

mv -f *.c ../