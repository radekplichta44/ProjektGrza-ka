# System Podgrzewania Wody na ESP32 (Termostat PID)

W pełni zautomatyzowany system podgrzewania wody obsługiwany za pomocą aplikacji webowej. Projekt opiera się na mikrokontrolerze ESP32 i realizuje precyzyjną kontrolę temperatury (w zakresie od $30^{\circ}C$ do $90^{\circ}C$) z wykorzystaniem zaawansowanego algorytmu PID. 

Dzięki zastosowaniu dynamicznej interpolacji parametrów oraz autorskiego mechanizmu Anti-Windup, układ utrzymuje wartość temperatury wody w stanie ustalonym z dokładnością do $\pm0.5^{\circ}C$ dla zbiornika o pojemności 1 litra.

## Główne cechy projektu

* **Zaawansowany algorytm PID:** Sterowanie mocą grzałki (PWM) z inteligentnym członem całkującym i różniczkującym.
* **Gain Scheduling (Interpolacja Liniowa):** Parametry $K_p$, $K_i$ oraz $K_d$ są wyliczane i zmieniane płynnie w zależności od zadanej temperatury. Pozwala to na kompensację nieliniowych strat ciepła (konwekcja, silne parowanie wody przy wyższych temperaturach).
* **Asymetryczny Anti-Windup:** Mechanizm, który zapobiega przeregulowaniu poprzez aktywację całki tylko w małym oknie błędu ($\pm 2.5^{\circ}C$). Dodatkowo, całka jest ograniczana od dołu do 0, co całkowicie eliminuje problem głębokich "dołków" (spadków temperatury) po osiągnięciu zadanego progu.
* **Bezprzewodowy Interfejs Webowy:** Wbudowana w ESP32 aplikacja interfejsu umożliwiająca zadawanie wartości referencyjnej oraz bieżący odczyt temperatury z czujnika. Komunikacja odbywa się w technologii AJAX (bez przeładowywania strony).

## Wymagania sprzętowe

Do zbudowania układu wykorzystano następujące elementy:
* **Mikrokontroler:** Moduł kontrolera ESP32 pełniący funkcję logiki układu.
* **Element wykonawczy:** Przekaźnik SSR-10DA służący do załączania obwodu wysokiego napięcia zasilającego grzałkę.
* **Grzałka:** Grzałka o mocy 650W zasilana napięciem 230 VAC.
* **Czujnik temperatury:** Zanurzeniowy czujnik temperatury wody DS18B20.
* **Zasilanie:** Zasilacz 230VAC/5VDC.
* **Elementy dyskretne:** Tranzystor NPN 2N2222 umożliwiający sterowanie przekaźnikiem za pomocą mikrokontrolera ESP32 oraz rezystory (np. 1kΩ na bazę tranzystora i 4.7kΩ jako pull-up dla magistrali 1-Wire).

## Model Matematyczny i Sterowanie

Identyfikację modelu matematycznego obiektu przeprowadzono na podstawie odpowiedzi skokowej. Na podstawie badań wyznaczono model inercyjny pierwszego rzędu z opóźnieniem transportowym:

$$G(s)=\frac{9.2392}{2.5187\cdot10^{3}s+1}e^{-137.22s}$$ 
*(Zidentyfikowana transmitancja układu)*

System sterowania został podzielony na dwie główne sekcje. Część obliczeniową stanowi regulator, zrealizowany w formie oprogramowania mikrokontrolera, który na podstawie uchybu wypracowuje cyfrowy sygnał sterujący. Algorytm różnicuje swoje podejście w zależności od temperatury docelowej:
* **Dla niskich temperatur ($30^{\circ}C$):** Słabsze zjawiska konwekcyjne. Mniejsze $K_p$ zapobiega przeregulowaniu.
* **Dla wysokich temperatur ($80^{\circ}C$):** Ogromne straty ciepła przez parowanie. Mocniejsze $K_i$ i odpowiednio dostrojone $K_d$ pozwalają "dobić" do celu i utrzymać pożądaną wartość pomimo uciekającej energii.

## Instalacja i Uruchomienie

1. Otwórz projekt w środowisku Arduino IDE lub PlatformIO.
2. Upewnij się, że masz zainstalowane biblioteki: `OneWire`, `DallasTemperature`, `WiFi`, `WebServer`.
3. W sekcji konfiguracji podaj swoje dane dostępowe do sieci Wi-Fi:
   ```cpp
   const char* ssid = "TWOJE_WIFI";
   const char* password = "TWOJE_HASLO";
   Skompiluj i wgraj kod na ESP32.

4. Otwórz Monitor Portu Szeregowego (Baud: 115200). Odczytaj adres IP urządzenia po udanym połączeniu.

5. Wpisz adres IP w przeglądarce na telefonie lub komputerze, aby uzyskać dostęp do panelu sterowania. Z poziomu panelu możesz płynnie zadawać temperaturę, odczytywać stan pracy i monitorować układ na żywo.

Projekt zrealizowany przez grupę ACIR A1B: