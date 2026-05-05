# BeehiveScale — Рабочий процесс (хобби-репозиторий)

Простая схема: как мы меняем, версионируем, откатываем.

---

## 1. Версии прошивки

Формат: **MAJOR.MINOR.PATCH** (например `4.2.0`).

- **MAJOR** (4 → 5) — ломаем EEPROM или API. Нужна ручная миграция пользователем.
- **MINOR** (4.2 → 4.3) — новые фичи, совместимо.
- **PATCH** (4.2.0 → 4.2.1) — только багфиксы, без новых фич.

Версия живёт в **одном месте** — `BeehiveScale/Version.h`:

```cpp
#define FW_VERSION_MAJOR 4
#define FW_VERSION_MINOR 2
#define FW_VERSION_PATCH 0
```

Автоматически появляется:
- в Serial при boot (`[BeehiveScale v4.2.0] boot`)
- в шапке Web UI
- в JSON `/api/data` (поле `fw`)
- в Telegram `/help`

**Правило:** перед релизом — бампнуть версию в `Version.h`, записать изменения в `CHANGELOG.md`.

---

## 2. Git workflow

Одна ветка **`main`** — работаем trunk-based (для хобби так проще). Эксперименты — в отдельных ветках `feature/xxx`, которые потом мёржим в main.

### Формат коммитов — Conventional Commits

```
<тип>: <краткое описание>

<опционально: детали, мотивация>
```

Типы:
- `feat:` — новая фича → MINOR bump
- `fix:` — багфикс → PATCH bump
- `security:` — уязвимость → PATCH или MINOR
- `perf:` — оптимизация
- `refactor:` — рефакторинг без изменения поведения
- `docs:` — только документация
- `build:` — сборка, CI, PlatformIO
- `chore:` — рутина (обновить .gitignore и т.д.)

### Примеры

```
feat: CSRF-защита на все POST endpoints
fix: sleepDur=0 guard в main loop
security: admin/OTA пароли в EEPROM вместо хардкода
docs: обновить README про пин DS18B20 (GPIO3)
```

### Один коммит = одно изменение

Плохо: «Fix 17 issues» (непонятно что откатывать).
Хорошо: отдельный коммит на каждую логическую правку.

---

## 3. Релизы — Git теги

Когда хочется зафиксировать рабочую версию:

```bash
git tag -a v4.2.0 -m "Release 4.2.0 — security hardening"
git push origin v4.2.0
```

На GitHub тег автоматически превращается в **Release**. CI (`.github/workflows/build.yml`) при пуше тега `v*` соберёт `.bin` и прикрепит к релизу — будет что скачать и прошить обратно при откате.

### Проверка истории

```bash
git log --oneline                # список всех коммитов
git log v4.1-audit..HEAD         # что изменилось с предыдущего тега
git diff v4.1-audit HEAD         # полный diff от тега до текущего
git tag -l                       # все теги
```

---

## 4. Откат изменений

### Откатить один коммит (forward-only, безопасно)

```bash
git revert <hash>                # создаст обратный коммит
```

История сохраняется, безопасно для уже запушенных изменений.

### Вернуться на рабочую версию целиком (локально для тестов)

```bash
git checkout v4.1-audit          # на тег (detached HEAD)
# ... потестили, возвращаемся ...
git checkout main
```

### Полный откат main на старый тег (деструктивно, только локально)

```bash
git reset --hard v4.1-audit      # ОПАСНО: теряются все коммиты после
# НЕ пушить force на main — это перепишет историю у всех
```

### Откат прошивки на железе

1. Скачать `firmware.bin` из старого GitHub Release.
2. Прошить через OTA: `/api/reboot` в режим OTA, либо espota.py:
   ```bash
   espota.py -i 192.168.x.x -p 8266 --auth=<ota_pass> -f firmware.bin
   ```
3. Или USB-прошивка через Arduino IDE/PlatformIO со старым коммитом.

### Откат настроек (EEPROM)

Перед каждой прошивкой — `/api/backup` → сохранить JSON. После — `/api/backup/restore` → восстановить калибровку, Wi-Fi, Telegram, интервалы.

---

## 5. Типовой цикл работы

1. **Задача** — бага найдена или фича нужна.
2. **Ветка** (опционально для большой задачи):
   ```bash
   git checkout -b feature/my-thing
   ```
3. **Правим код** — инкрементально, маленькими коммитами:
   ```bash
   git add BeehiveScale/Memory.cpp
   git commit -m "fix: уменьшен износ EEPROM в restore"
   ```
4. **Собрать** — Arduino IDE (`Sketch → Verify`) или `pio run`.
5. **Тест на устройстве** — прошить, погонять сутки.
6. **Мёрж в main** (если была ветка):
   ```bash
   git checkout main
   git merge feature/my-thing
   ```
7. **Бамп версии** в `Version.h`, запись в `CHANGELOG.md`.
8. **Тег и push**:
   ```bash
   git tag -a v4.2.1 -m "Release 4.2.1"
   git push origin main v4.2.1
   ```
9. **GitHub Release** создастся автоматически (CI), `.bin` будет прикреплён.

---

## 6. Чеклист перед релизом

- [ ] Код компилируется без ошибок
- [ ] Потестил на реальной плате (минимум: boot, Web UI, тарировка, лог)
- [ ] Обновил `Version.h`
- [ ] Добавил раздел в `CHANGELOG.md`
- [ ] Коммит с сообщением `chore(release): v4.X.Y`
- [ ] Тег `v4.X.Y`
- [ ] Push тега → GitHub Release
- [ ] Скачать `.bin` из Release, убедиться что прошивается

---

## 7. Полезные команды

```bash
git status                                  # что поменялось
git diff                                    # видеть изменения
git log --oneline -20                       # последние 20 коммитов
git show v4.1-audit                         # что было в теге
git tag -l 'v4.*'                           # все v4.x теги
git revert HEAD                             # отменить последний коммит
git stash                                   # временно спрятать изменения
git stash pop                               # вернуть их
```
