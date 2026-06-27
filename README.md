# Stacja pogodowa - STM32F429ZI

Stacja pogodowa na mikrokontrolerze STM32F429ZI (NUCLEO-F429ZI), mierząca temperaturę i ciśnienie atmosferyczne czujnikiem **BMP280**, wyświetlająca wyniki na **LCD 20x4** (przez ekspander I2C) i udostępniająca konfigurację/diagnostykę przez interaktywną konsolę **CLI** dostępną przez UART.

Projekt zaliczeniowy z przedmiotu *Standardy Komunikacji Międzyukładowej w modułowych systemach wbudowanych*.

## Funkcjonalności

- Pomiar temperatury i ciśnienia (BMP280, I2C1), uśredniany w oknie 5 próbek
- Wyświetlanie wyników na LCD 20x4 (I2C2)
- Konfigurowalna częstotliwość pomiaru (5 poziomów: 100 ms - 2 s)
- Min/max temperatury i ciśnienia oraz trend zmian ciśnienia
- Logowanie danych w formacie CSV na UART (do przechwycenia np. skryptem na PC)
- Watchdog sprzętowy (IWDG) + automatyczny restart czujnika po wykryciu usterki
- Interaktywna konsola CLI: echo znaków, backspace, historia komend (strzałki ↑/↓)

## Sprzęt

| Element | Opis |
|---|---|
| Mikrokontroler | STM32F429ZITX (NUCLEO-F429ZI) |
| Czujnik | BMP280, I2C1, adres `0x76` |
| Wyświetlacz | LCD 20x4 z ekspanderem I2C, I2C2 |
| Komunikacja z PC | UART3 przez ST-LINK VCP (USB) |

Schemat podłączenia interfejsów oraz pełny opis architektury znajdują się w [`specyfikacja_projektu.md`](specyfikacja_projektu.md).

## Wymagania do budowy projektu

- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html)
- Płytka NUCLEO-F429ZI podłączona przez USB (ST-LINK)
- Terminal UART (wbudowany terminal CubeIDE, PuTTY, Tera Term itp.) na porcie ST-LINK VCP, **115200 8N1** (lub zgodnie z konfiguracją USART3 w `.ioc`)

## Budowanie i wgrywanie

1. Sklonuj repozytorium
2. Otwórz folder projektu w STM32CubeIDE (`File → Open Projects from File System...`)
3. Zbuduj projekt (`Project → Build Project` lub Ctrl+B)
4. Podłącz płytkę i wgraj firmware (`Run` lub `Debug`)
5. Otwórz terminal UART na porcie ST-LINK i wpisz `help`, aby zobaczyć listę komend

## Komendy CLI

| Komenda | Opis |
|---|---|
| `help` | Lista dostępnych komend |
| `led on` / `led off` | Sterowanie diodą LD2 |
| `start meas` / `stop meas` | Start/stop pomiarów |
| `lcd on` / `lcd off` | Włączenie/wyłączenie wyświetlacza |
| `refresh [1..5]` | Okres pomiaru: 1=100 ms, 2=250 ms, 3=500 ms, 4=1 s, 5=2 s |
| `status` | Stan programu i ostatni pomiar |
| `stats` | Min/max, trend ciśnienia, licznik usterek czujnika |
| `stats reset` | Reset wartości min/max |
| `log on` / `log off` | Logowanie CSV na UART (`LOG,tick_ms,temp_x100,press_x100`) |
| `clear` | Czyszczenie ekranu terminala |
| ↑ / ↓ | Historia ostatnich komend |

## Struktura projektu

```
Core/
  Inc/        - pliki naglowkowe (app.h, cli.h, bmp280.h, lcd_i2c.h, ...)
  Src/
    main.c    - inicjalizacja peryferiow, petla glowna, watchdog
    app.c     - logika aplikacji: pomiar, usrednianie, min/max, trend, logowanie
    cli.c     - interpreter komend, edycja linii, historia
    bmp280.c  - sterownik czujnika BMP280 (I2C)
    lcd_i2c.c - sterownik wyswietlacza LCD (I2C)
Drivers/      - HAL/CMSIS (wygenerowane przez STM32CubeMX)
*.ioc         - konfiguracja peryferiow (STM32CubeMX)
```

## Autorzy

- **Osoba 1** - projekt sprzętowy, implementacja bazowa (czujnik, LCD, pierwsza wersja CLI)
- **Osoba 2** - rozszerzenia CLI, diagnostyka (watchdog, min/max, trend, logowanie), dokumentacja

## Licencja

Projekt edukacyjny / zaliczeniowy.
