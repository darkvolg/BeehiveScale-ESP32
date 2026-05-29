#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 65
#define FW_VERSION_SUFFIX ""  // 5.0.65 — вес округляется до десятых (0.1 кг) везде в отображении: TG-отчёты/тревоги, LCD-экран, главная страница, график (ось Y + тултип), архив, статистика. Причина: сотые (10г) ниже реального шума весов улья — ложная точность + дрожание графика. CSV-экспорт и Excel оставлены до сотых (для анализа). Батарея/температура не тронуты.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
