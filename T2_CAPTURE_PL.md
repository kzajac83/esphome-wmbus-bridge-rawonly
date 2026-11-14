# Nasłuch wM-Bus na 868,300 MHz i zapis do CSV

Projekt używa komponentu bridge’a z gałęzi `codex/wmbus-t2` forka `kzajac83/esphome-wmbus-bridge-rawonly` i płytki LilyGO TTGO LoRa32 T3 v1.6.1 z SX1276 w wariancie 868 MHz. Radio pracuje w trybie odbioru.

| Kierunek w T2 | Częstotliwość | Kodowanie | Profil bridge’a |
| --- | --- | --- | --- |
| Czytnik → licznik | 868,300 MHz | 32,768 kchip/s, Manchester | `s1` |
| Licznik → czytnik | 868,950 MHz | 100 kchip/s, 3 z 6 | `t1` |

Każdy profil zajmuje jeden odbiornik. Do równoczesnego nasłuchu obu kierunków użyj dwóch odbiorników. Pierwszy może dalej pracować z dotychczasowym firmware T1. Na 868,300 MHz pojawia się również S-mode. Etykieta `s1` w CSV oznacza profil odbioru. Potwierdzenie T2 wymaga korelacji z odczytem własnego licznika przez zgodny czytnik. Sam regularny telegram licznika na 868,950 MHz też nie rozstrzyga, czy licznik obsługuje T2.

Źródła sprzętu i PHY: [LilyGO T3 v1.6.1](https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/blob/master/docs/en/t3_v161_sx1276/t3_v161_sx1276_hw.md), [Silicon Labs](https://docs.silabs.com/rail/latest/rail-wmbus-apps-with-efr32/03-using-the-configurator), [TI AN067](https://www.ti.com/lit/pdf/swra234).

## Firmware w ESPHome Device Builder w Home Assistant

1. Dodaj konfigurację `lilygo-t2-capture.yaml` do katalogu konfiguracji Device Buildera lub wklej ją do edytora YAML nowego urządzenia. Builder pobierze komponent z forka przy kompilacji.
2. Dopisz brakujące wpisy do pliku `secrets.yaml` Device Buildera według `secrets.example.yaml`. Wpisz własne dane Wi-Fi, adres i dane brokera MQTT oraz hasło przyszłych aktualizacji OTA. Jeśli broker Mosquitto działa jako dodatek w HA, podaj adres IP hosta HA dostępny z Wi-Fi ESP32. Dla brokera bez uwierzytelniania ustaw `mqtt_user` i `mqtt_password` na `""`. Jeśli masz już te sekrety pod innymi nazwami, dopasuj odwołania `!secret` w YAML.
3. Podłącz antenę 868 MHz i wyjmij kartę SD przed wgrywaniem. W Device Builderze wybierz **Install**. Jeśli płytka jest podłączona przez USB do komputera z przeglądarką, wybierz instalację z tego komputera przez USB. Jeśli jest podłączona przez USB do hosta Home Assistant, wybierz instalację przez USB z hosta. **Manual download** pobiera plik `.bin` do samodzielnego wgrania. Do ręcznej instalacji przez USB wybierz format **Factory**. Po udanym wgraniu i połączeniu z Wi-Fi kolejne wersje można instalować przez **Wirelessly** (OTA).

Dokumentacja: [ESPHome Device Builder](https://esphome.io/install/), [komponenty z GitHuba](https://esphome.io/components/external_components/), [aktualizacje OTA](https://esphome.io/components/ota/esphome/).

Jeśli wgrywasz przez lokalne ESPHome na Windows, uzupełnij `secrets.yaml` obok konfiguracji i podstaw właściwy port:

```powershell
esphome run lilygo-t2-capture.yaml --device COM5
```

## Zapis CSV z brokera MQTT

Uruchom `capture_csv.py` na komputerze z dostępem do brokera. Jeśli używasz tego samego środowiska Pythona co ESPHome, biblioteka Paho zwykle jest już zainstalowana. W osobnym środowisku możesz ją zainstalować przez `python -m pip install paho-mqtt==1.6.1`.

```powershell
python capture_csv.py ADRES_BROKERA --user UZYTKOWNIK
```

Na testowej maszynie z Debianem wystarczy zwykłe konto z Pythonem, `venv` i dostępem do brokera:

```bash
git clone --branch codex/wmbus-t2 https://github.com/kzajac83/esphome-wmbus-bridge-rawonly.git
cd esphome-wmbus-bridge-rawonly
python3 -m venv .venv
.venv/bin/python -m pip install paho-mqtt==1.6.1
.venv/bin/python capture_csv.py ADRES_BROKERA --user UZYTKOWNIK
```

Skrypt poprosi o hasło. Możesz też przekazać je zmienną środowiskową `WMBUS_MQTT_PASSWORD`. Dla brokera bez uwierzytelniania pomiń opcję `--user`.

Wykonaj odczyt swojego licznika zgodnym czytnikiem i sprawdź, czy w tym czasie przybyły wiersze w `wmbus-capture.csv`. Domyślnie plik powstaje w katalogu, z którego uruchamiasz skrypt. Opcja `--output` pozwala wskazać inną ścieżkę.

## Co trafia do pliku

`raw_hex` zawiera odebrane bajty z FIFO radia po synchronizacji, przed dekodowaniem Manchester i sprawdzeniem CRC. Zrzut może zawierać niepełne ramki i dodatkowe bajty za telegramem. `logged_at_utc` to czas odebrania wiadomości MQTT przez rejestrator. `uptime_ms` pochodzi z ESP32. Skrypt dopisuje każdy wpis i opróżnia bufor pliku. Używaj jednego procesu zapisującego na plik.

Surowy strumień MQTT używa QoS 0. Do zapisu ramki potrzebne są działające połączenie z brokerem i uruchomiony rejestrator. Po zerwaniu połączenia skrypt ponownie subskrybuje temat po powrocie brokera.

Temat surowych zrzutów skonfigurowano jako `wmbus/lilygo-t2-capture/raw`. Opcja `radio_raw_topic` zmienia miejsce publikacji zrzutów włączonych przez `publish_radio_raw`. Dotychczasowy domyślny temat to `wmbus_bridge/raw`. Bridge może też publikować zweryfikowane telegramy na osobnym temacie `/telegram`.
