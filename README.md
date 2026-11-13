# solar-heater-hap

Контролер за соларна инсталация за битов бойлер с топла вода, с поддръжка на Apple **HomeKit** (HAP).

Контролерът управлява циркулационната помпа между слънчевия колектор и бойлера: пренася топлината на слънцето
към водата възможно най-ефективно (особено през зимата и в облачни дни) и пази колектора и бойлера от прегряване.
Наблюдава се и се управлява от дисплея и бутоните на кутията, от уеб страница в браузъра и от Apple Home.

## Възможности

- **Диференциално управление** на помпата по температурите на колектора и бойлера — разлики за включване и
  изключване, минимална температура на колектора, максимум на бойлера (Tmax), минимални времена на работа и
  престой; два комплекта настройки — лято и зима, с автоматичен избор на сезона.
- **Защити:** защита на колектора от прегряване, забрана за пускане при горещ колектор (по точката на кипене на
  топлоносителя), авариен таван на бойлера (95 °C), защита от замръзване, защита от блокиране на помпата.
- **Ваканция** и **нощно охлаждане** на бойлера.
- **Разпознаване на нагревателя** в бойлера, защита от „порочния кръг“ — теч на топлина от нагревателя през
  колектора, откриване на обратен термосифон (`E8`).
- **Импулсен режим** за датчици, монтирани на тръбите (без гилзи).
- **Поведение при повреден датчик**, **аварийни съобщения** и откриване на повреда на помпата (`E6`).
- **Тест на помпата** и **обезвъздушаване** на кръга.
- **Локално управление:** 3-цифрен дисплей и 3 бутона (меню, „+“, „−“), заключване на клавиатурата.
- **Светлинна индикация** с един RGB светодиод — какво прави системата и дали има връзка.
- **Уеб интерфейс** на телефона — състояние, графика за деня, управление, настройки, данни на монтажника,
  история (5 години дневни обобщения, CSV), обновяване, нулиране; на български и английски.
- **Мрежа:** Wi-Fi с портал за настройка, собствена точка за достъп (`AP`) за работа без рутер, име
  `Solar-xxxxxx` и `Solar-xxxxxx.local`, часовник по мрежата.
- **Обновяване на фърмуера** по мрежата (OTA).
- **Apple HomeKit:** Heater Cooler (ваканция, Tmax, състояние, единици °C / °F, заключване), датчиците
  `Collector` и `Tank`.
- Без връзка с HomeKit и без Wi-Fi всичко работи локално.

## Състояние на проекта

- **Фърмуерът е написан** с всички функции по-горе; логиката е покрита с тестове на компютъра. Предстоят тестовете
  на реалния хардуер.
- **Базовата платка** (клеми, HLK-PM01, реле за помпата, входове за датчиците) е в процес на проектиране.
- **Предстои:** датчик Pt1000 за колектора, град и прогноза за времето, инструкция с препоръчителни стойности на
  параметрите, електронен модел за симулация на логиката, охладителна система (охладителна кула вместо колектор);
  моделът на помпата още не е избран.

## Хардуер

- **Микроконтролер:** Waveshare **ESP32-S3-Zero** (ESP32-S3, Wi-Fi + BLE, USB-C)
- **Индикация:** вграденият RGB светодиод WS2812 на ESP32-S3-Zero (GPIO21), без допълнителни елементи
- **Температура:** 2 × DS18B20 (колектор и бойлер); всеки на отделен GPIO, VDD 5 V, pull-up към 3.3 V
- **Локално управление:** 3-цифрен 7-сегментен дисплей с TM1637 + 3 бутона
- **Изпълнителен елемент:** реле с 5 V бобина (транзистор и обратен диод), включва циркулационната помпа
- **Захранване:** 5 V шина (ESP32-S3-Zero, реле, датчици); 3.3 V шината остава само за ESP32-S3 и pull-up
  резисторите
- **Захранване 230 V → 5 V:** модул Hi-Link HLK-PM01 (5 V / 0.6 A) с изходни кондензатори
- **Кутия:** за DIN шина, ширина 2 модула
- **Изводи:** T_COL → GP1, T_TANK → GP2, реле → GP4, TM1637 → GP5/GP6, бутони → GP7/GP8/GP9, светодиод → GP21

Подробности: [doc/hardware.md](doc/hardware.md), [doc/base-board.md](doc/base-board.md).

## Софтуер

- **Платформа:** ESP-IDF **5.5.x**, target `esp32s3`; HomeKit —
  [esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk) (подмодул `components/homekit`, клонът
  `homekit` на форка [AramVartanyan/esp-homekit-sdk](https://github.com/AramVartanyan/esp-homekit-sdk)).
- **Фърмуер:** `project/solar-heater-hap/` — изпълним файл `solar-heater-hap.bin`. Едно име от основния MAC адрес —
  `Solar-xxxxxx`: в рутера, по mDNS (`Solar-xxxxxx.local`) и на точката за достъп (за настройка на Wi-Fi и за режим
  `AP`).
- Драйвер за WS2812 светодиода: `espressif/led_strip` (RMT, минимална конфигурация) — виж
  [doc/firmware.md](doc/firmware.md).

### Компилиране

```sh
git clone --recursive https://github.com/AramVartanyan/solar-heater-hap.git
# (или след обикновен clone: git submodule update --init)
cd solar-heater-hap/project/solar-heater-hap
idf.py set-target esp32s3
idf.py build flash monitor
```

### Тестове на компютъра

```sh
make -C project/solar-heater-hap/test          # тестовете на логиката
make -C project/solar-heater-hap/test sim      # уеб страницата със заместител на контролера: http://localhost:8080/
```

### Общи библиотеки (git submodules в `project/common/`)

| Хранилище | Роля |
|---|---|
| [captive-wifi](https://github.com/AramVartanyan/captive-wifi) | Wi-Fi връзка с портал за настройка |
| [outputwrite](https://github.com/AramVartanyan/outputwrite) | GPIO / реле помощни функции |
| [esp_tm1637](https://github.com/AramVartanyan/esp_tm1637) | Дисплей TM1637 (1.1.1: ESP-IDF 5, 1 – 6 цифри, таймаут на ACK, push-pull, нулиране на неизползваните цифри) |
| [container_nvs](https://github.com/AramVartanyan/container_nvs) | Съхранение на настройките в NVS |
| [ds18b20](https://github.com/AramVartanyan/ds18b20) | 1-Wire и датчици DS18B20 (собствен драйвер: ESP8266_RTOS_SDK, ESP-IDF 4.x / 5.x) |
| [general-ota](https://github.com/AramVartanyan/general-ota) | Обновяване на фърмуера по HTTPS (без зависимост от HomeKit) |

### Компоненти от esp-homekit-sdk (`components/`)

| Хранилище | Роля |
|---|---|
| [button](https://github.com/AramVartanyan/button) | Бутони — собствена поправена версия, която замества `button` от esp-homekit-sdk (git submodule в `components/button`) |
| [esp-homekit-sdk](https://github.com/AramVartanyan/esp-homekit-sdk) (`components/homekit`) | HomeKit — git submodule, клонът `homekit` на форка (само `components/homekit` от `master`, обновява се автоматично с GitHub Action); `HOMEKIT_PATH` по подразбиране е коренът на това хранилище |

При външен `HOMEKIT_PATH` папката `components/` на това хранилище се добавя последна, така че собственият `button`
има предимство пред този от SDK-то.

Управлявани компоненти (`main/idf_component.yml`): `espressif/led_strip`, `espressif/mdns`.

### Референции

| Хранилище | Роля |
|---|---|
| [mppt-1k](https://github.com/AramVartanyan/mppt-1k) | Референтен проект (ESP-IDF + HomeKit), модел за структурата |
| [window-hap-sdk-8266](https://github.com/AramVartanyan/window-hap-sdk-8266) | Референтен проект (HomeKit, `app_wifi`) |
| [esp-led-status](https://github.com/AramVartanyan/esp-led-status) | Индикация чрез LED шаблони (за обикновен светодиод) |

## Структура на хранилището

```
solar-heater-hap/
├── README.md, Changelog.md, LICENSE
├── components/               — компоненти от esp-homekit-sdk
│   ├── button/               — собствена поправена версия (git submodule)
│   └── homekit/              — подмодул: клонът homekit на форка esp-homekit-sdk
├── project/
│   ├── common/               — общи библиотеки (git submodules)
│   │   ├── captive-wifi/
│   │   ├── outputwrite/
│   │   ├── esp_tm1637/
│   │   ├── container_nvs/
│   │   ├── ds18b20/
│   │   └── general-ota/
│   └── solar-heater-hap/     — фърмуерът
│       ├── CMakeLists.txt, sdkconfig.defaults, partitions_hap.csv
│       ├── main/             — app_main.c и модулите sh_*.c/h, web/ (уеб страницата), Kconfig.projbuild, idf_component.yml
│       └── test/             — тестовете на компютъра и заместителят на уеб страницата (web_sim)
└── doc/                      — документация, схеми, изображения
    ├── README.md             — съдържанието на документацията
    ├── operating-modes.md, led-indication.md, web-interface.md, hardware.md, base-board.md, firmware.md, controllers-research.md
    ├── web/                  — макет на уеб интерфейса
    ├── img/
    └── tasks/                — архив на разработката (заданията)
```

## Документация

Съдържанието е в [doc/README.md](doc/README.md):

- [Логиката на контролера](doc/operating-modes.md) — режими, защити, меню и бутони, HomeKit
- [Светлинна индикация](doc/led-indication.md)
- [Уеб интерфейс](doc/web-interface.md)
- [Хардуер](doc/hardware.md) и [базова платка](doc/base-board.md)
- [Фърмуер](doc/firmware.md)
- [Проучване на контролери на пазара](doc/controllers-research.md)

## История на промените

Виж [Changelog.md](Changelog.md).

## Лиценз

Виж [LICENSE](LICENSE).
