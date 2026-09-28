# План интеграции визуальных погодных эффектов (WeatherVisuals)

## Обзор
Интеграция модуля современных погодных эффектов в `modern-wow-renderer` (D3D9 прокси-рендерер для клиента WoW 3.3.5a / Ascension). 
Цель: заменить плоские 2D-билборды стандартного клиента на физически корректные, depth-tested осадки, динамические брызги на поверхностях, капли на объективе камеры и плотную погодную пелену, используя ассеты и параметры из клиента Modern/Classic.

> **Коррекция реализации (2026-09-26):** первоначальная версия ниже была
> ошибочно реализована полноэкранным quad в `Present`, а не через нативные
> частицы. Она рисовала осадки поверх UI/экрана входа и не была связана с
> текущей погодой WoW. Этот путь отключён. Текущий безопасный этап подменяет
> текстуру только на распознанных нативных world-space weather draw calls;
> отсутствие нативных осадков означает отсутствие добавленного эффекта.
> Пункты про splash, wetness, lens и roof occlusion ниже снова считаются
> будущей работой, пока для них нет корректной точки интеграции до UI.

---

## Архитектура системы

```
ModernWoWRenderer.cpp (D3D9 Proxy Pipeline)
   │
   ├── HookedClear / BeforeClear ───────────► Инициализация фрейма, сброс аккумуляторов
   │
   ├── DrawCallClassifier / SetTexture ─────► Распознавание шейдеров и поверхностей
   │
   ├── HookedPresent / EndScene
   │     │
   │     ├── DepthCapture ──────────────────► Линейная глубина z-buffer сцены
   │     ├── CameraCapture ─────────────────► Матрицы вида/проекции, вектор взгляда
   │     │
   │     ▼
   │  WeatherVisuals::Render(device)
   │     │
   │     ├── Pass 1: Volumetric Precipitation (Depth-Occluded Rain / Snow)
   │     │     ├─ Шейдер осадков с проверкой z-буфера
   │     │     └─ Отсечение капель под крышами и укрытиями
   │     │
   │     ├── Pass 2: Surface Splashes & Wetness
   │     │     ├─ Всплески raindropsplash01 на горизонтальных плоскостях
   │     │     └─ Эффект мокрого блеска (wet specular)
   │     │
   │     ├── Pass 3: Camera Lens Raindrops (Капли на экране)
   │     │     ├─ Стекающие капли с преломлением сцены
   │     │     └─ Автоматическое высыхание в помещении
   │     │
   │     └── Pass 4: Atmosphere Modulation (Связка с VolumeIntegration)
   │           └─ Сгущение пелены тумана и рассеивание света
   │
   └── GraphicsEffects.ini ─────────────────► Живая настройка (F12) и горячие клавиши (F9)
```

---

## Этапы реализации

### Этап 1. Подготовка ассетов и текстур
* [x] Извлечение официальных погодных текстур из WoW Classic Beta (`raindrop01`, `raindropsplash01`, `snowflake01`, `snowmist01`, `weathermistgrainy01`).
* [x] Конвертация в форматы, совместимые с DirectX 9 (PNG через WIC и DDS DXT5).
* [x] Размещение текстур в каталоге `textures/weather/` внутри сборки рендерера с автоматической загрузкой через `WicTextureLoader` / D3D9.

### Этап 2. Модуль `WeatherVisuals.h` / `WeatherVisuals.cpp`
* **Структура настроек (`WeatherConfig`):**
  * `Enabled` (bool) — общий переключатель.
  * `Mode` (int) — 0: Auto (по зоне/памяти), 1: Rain, 2: Snow, 3: Sandstorm.
  * `RainDensity` (float) — плотность потока осадков.
  * `RainSpeed` (float) — скорость падения.
  * `WindAngle` / `WindStrength` (float) — наклон капель ветром.
  * `DepthOcclusion` (bool) — защита от пролетания сквозь крыши.
  * `SurfaceSplashes` (bool) — всплески на земле.
  * `LensDroplets` (bool) — капли на стекле экрана.
  * `AtmosphereHaze` (float) — коэффициент усиления тумана `VolumeIntegration`.
* **HLSL шейдеры (`WeatherShaders.h` / встроенный HLSL ps_3_0):**
  * `weatherRainPS`: многослойные полосы дождя с проецированием в мировом пространстве, сдвигом по времени и отсечением по `depthMap`.
  * `weatherSnowPS`: мягкие хлопья снега с покачиванием по синусоиде и затуханием на препятствиях.
  * `weatherSplashPS`: генерация кругов на воде и брызг на поверхностях с нормалью вверх ($N_z > 0.65$).
  * `weatherLensPS`: капли на экране с искажением UV координат основного буфера кадра (Refraction).

### Этап 3. Тестирование глубины и окклюзия укрытий (Anti-Indoor Clipping)
* Использование линейной глубины из `DepthCapture::Instance().GetDepthTexture()`.
* Восстановление координаты мира $P_{world}$ для каждого луча пикселя:
  $$P_{view} = \text{RayDir} \times Z_{linear}$$
  $$P_{world} = \text{InverseView} \times P_{view}$$
* Отсечение: капля видна только в интервале $Z_{near} \le Z_{drop} < Z_{scene}$. Если $Z_{drop} \ge Z_{scene}$, капля скрыта геометрией мира (крышей, мостом, навесом).
* Детект нахождения под укрытием: проверка расстояния до потолка прямо над камерой. Если над камерой низкий потолок ($Z_{up} < 15$ ярдов), верхний фронт осадков отключается.

### Этап 4. Интеграция в конвейер `ModernWoWRenderer.cpp`
* Вызов `weathervisuals::Initialize(device)` при создании устройства.
* Вызов `weathervisuals::Render(device, backBuffer, depthTexture)` в `HookedEndScene` / `HookedPresent` перед финальным оверлеем.
* Регистрация в `GraphicsEffects.ini` под секцией `[WeatherVisuals]`.
* Поддержка горячей перезагрузки по **F12** и хоткея быстрого переключения **F10** (`F9` уже занят celestial diagnostics).

### Этап 5. Связка с атмосферой (`VolumeIntegration`)
* Модуляция параметров `groundMistDensity`, `fogWash` и `extinction` в `VolumeIntegration.h` в зависимости от текущей силы осадков.

---

## План на будущее (Звуки и аудио-окклюзия)
* По завершении визуальной части: реализация `WeatherAudio.cpp` с программным Low-Pass фильтром на аудио-поток при флаге `IsIndoors`.
