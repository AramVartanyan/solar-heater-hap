# solar-heater-hap

Контролер за соларна инсталация за битов бойлер с топла вода, с поддръжка на Apple **HomeKit** (HAP).

> **Статус:** фаза на събиране на идеи и изисквания. Структурата на проекта предстои да бъде качена.

## Цел

Автоматично управление на соларна система за загряване на вода (бойлер), с наблюдение
и управление през HomeKit.

## Идеи и изисквания

_Тази секция се попълва постепенно, докато се обсъждат идеите._

- (предстои)

## Хардуер

- **Микроконтролер:** Waveshare **ESP32-S3-Zero** (ESP32-S3, Wi-Fi + BLE, USB-C)
- **Индикация:** вграденият RGB светодиод WS2812 на ESP32-S3-Zero (GPIO21), без допълнителни елементи
- **Температура:** 2 × DS18B20 (колектор и бойлер); всеки на отделен GPIO, VDD 5 V, pull-up към 3.3 V
- **Локално управление:** 3-цифрен 7-сегментен дисплей с TM1637 + 3 бутона
- **Изпълнителен елемент:** реле с 5 V бобина (транзистор и обратен диод), включва циркулационната помпа
- **Захранване:** 5 V шина (ESP32-S3-Zero, реле, датчици); 3.3 V шината остава само за ESP32-S3 и pull-up резисторите
- **Захранване 230 V → 5 V:** модул Hi-Link HLK-PM01 (5 V / 0.6 A) с изходни кондензатори
- **Кутия:** за DIN шина, ширина 2 модула
- **Базова платка:** клеми, HLK-PM01, реле за помпата, входове за датчиците — в процес на проектиране

Подробности: [doc/hardware.md](doc/hardware.md), [doc/base-board.md](doc/base-board.md)

## Софтуер

- **Платформа:** ESP-IDF **5.5.x**, target `esp32s3`; HomeKit — [esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk) (в следваща фаза)
- **Фърмуер:** `project/solar-heater-hap/` — изпълним файл `solar-heater-hap.bin`, едно име от основния MAC адрес — `Solar-xxxxxx`: в рутера,
  по mDNS (`Solar-xxxxxx.local`) и на точката за достъп (за настройка на Wi-Fi и за режим `AP`)
- Драйвер за WS2812 светодиода: `espressif/led_strip` (RMT, минимална конфигурация) — виж [doc/firmware.md](doc/firmware.md)
- Изводи: T_COL → GP1, T_TANK → GP2, реле → GP4, TM1637 → GP5/GP6, бутони → GP7/GP8/GP9, светодиод → GP21

### Компилиране

```sh
git clone --recursive https://github.com/AramVartanyan/solar-heater-hap.git
# (или след обикновен clone: git submodule update --init)
export HOMEKIT_PATH=/path/to/esp-homekit-sdk      # нужно от фазата с HomeKit нататък
cd solar-heater-hap/project/solar-heater-hap
idf.py set-target esp32s3
idf.py build flash monitor
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
| esp-homekit-sdk (`components/homekit`) | HomeKit — препратка, задава се с `HOMEKIT_PATH` |

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
│   └── homekit/              — препратка към esp-homekit-sdk (HOMEKIT_PATH)
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
│       └── main/             — app_main.c и модулите sh_*.c/h, Kconfig.projbuild, idf_component.yml
└── doc/                      — спецификации, схеми, изображения
    ├── hardware.md, base-board.md, firmware.md, led-indication.md, operating-modes.md, controllers-research.md, web-interface.md
    └── img/
```

## Документация

Важните документи (спецификации, схеми, изображения) се съхраняват в папка [`doc/`](doc/).

## История на промените

Виж [Changelog.md](Changelog.md).

## Лиценз

Виж [LICENSE](LICENSE).
