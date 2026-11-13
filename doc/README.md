# Документация

Документите са подредени в реда, в който е удобно да се четат.

## Как работи контролерът

| Файл | Съдържание |
|---|---|
| [`operating-modes.md`](operating-modes.md) | Логиката на управлението: нормален режим, защити, ваканция, нощно охлаждане, сезони, нагревател, импулсен режим, аварии, меню и бутони, мрежа, HomeKit |
| [`led-indication.md`](led-indication.md) | Светлинната индикация с един RGB светодиод: цветове, шаблони, аварии |
| [`web-interface.md`](web-interface.md) | Уеб интерфейсът — 5 раздела, достъп с код, история, обновяване, нулиране; макет [`web/mockup.html`](web/mockup.html) |

## Хардуер

| Файл | Съдържание | Източник |
|---|---|---|
| [`hardware.md`](hardware.md) | Компонентите: микроконтролер, светодиод, DS18B20, реле, дисплей и бутони, захранване | — |
| [`base-board.md`](base-board.md) | Базовата платка: DIN кутия, клеми, HLK-PM01, реле, входове за датчиците, изводи (в процес на проектиране) | — |
| [`img/ESP32-S3-Zero-pinout.webp`](img/ESP32-S3-Zero-pinout.webp) | Извеждания (pinout) на платката Waveshare ESP32-S3-Zero | Waveshare |
| [`XL-0807RGBC-WS2812B.pdf`](XL-0807RGBC-WS2812B.pdf) | Datasheet на вградения RGB светодиод XINGLIGHT XL-0807RGBC-WS2812B | [Waveshare wiki](https://files.waveshare.com/wiki/ESP32-C3-Zero/XL-0807RGBC-WS2812B.pdf) |
| [`DS18B20.pdf`](DS18B20.pdf) | Datasheet на температурния датчик DS18B20 | [Analog Devices](https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf) |
| [`img/HLK-PM01.webp`](img/HLK-PM01.webp) | Захранващ модул Hi-Link HLK-PM01 (5 V / 0.6 A) | Hi-Link |

## Фърмуер

| Файл | Съдържание |
|---|---|
| [`firmware.md`](firmware.md) | Как е реализиран фърмуерът: драйвери, изводи (GPIO), модулите, HomeKit, ресурси |

## Проучване

| Файл | Съдържание |
|---|---|
| [`controllers-research.md`](controllers-research.md) | Контролери на пазара (tSOL DELTA, RESOL DeltaSol BS/4, Steca, Sorel) — параметри и идеи |

## Архив на разработката

[`tasks/`](tasks/) — заданията, по които е разработван фърмуерът. Пазят се до края на разработката.
