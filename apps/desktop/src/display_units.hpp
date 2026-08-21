#pragma once

#include "frontend_contract.hpp"

#include <QLocale>
#include <QString>

#include <cmath>
#include <numbers>
#include <optional>

namespace kearne::ui {

[[nodiscard]] inline QString gridSpacingFor(const QString &unitId) {
  if (unitId == QStringLiteral("cm"))
    return QStringLiteral("1 cm");
  if (unitId == QStringLiteral("m"))
    return QStringLiteral("0.01 m");
  if (unitId == QStringLiteral("in"))
    return QStringLiteral("0.5 in");
  return QStringLiteral("10 mm");
}

[[nodiscard]] inline double gridSpacingMillimetersFor(const QString &unitId) {
  return unitId == QStringLiteral("in") ? 12.7 : 10.0;
}

[[nodiscard]] inline QString formatDisplayedLength(double millimeters,
                                                   const QString &unitId) {
  if (!std::isfinite(millimeters))
    return QStringLiteral("—");
  double value = millimeters;
  QString symbol = QStringLiteral("mm");
  int decimals = 3;
  if (unitId == QStringLiteral("cm")) {
    value /= 10.0;
    symbol = QStringLiteral("cm");
    decimals = 4;
  } else if (unitId == QStringLiteral("m")) {
    value /= millimetersPerMeter;
    symbol = QStringLiteral("m");
    decimals = 6;
  } else if (unitId == QStringLiteral("in")) {
    value /= 25.4;
    symbol = QStringLiteral("in");
    decimals = 4;
  }
  if (std::abs(value) < std::pow(10.0, -decimals) * 0.5)
    value = 0.0;
  QString number = QLocale::c().toString(value, 'f', decimals);
  while (number.contains(QLatin1Char('.')) && number.endsWith(QLatin1Char('0')))
    number.chop(1);
  if (number.endsWith(QLatin1Char('.')))
    number.chop(1);
  return number + QLatin1Char(' ') + symbol;
}

[[nodiscard]] inline std::optional<double>
parseDisplayedLengthMetres(QString text, const QString &defaultUnitId) {
  text = text.trimmed().toLower();
  QString unit = defaultUnitId;
  for (const QString &candidate : {QStringLiteral("mm"), QStringLiteral("cm"),
                                   QStringLiteral("in"), QStringLiteral("m")}) {
    if (text.endsWith(candidate)) {
      unit = candidate;
      text.chop(candidate.size());
      text = text.trimmed();
      break;
    }
  }
  bool valid = false;
  const double value = text.toDouble(&valid);
  if (!valid || !std::isfinite(value))
    return std::nullopt;
  const double scale = unit == QStringLiteral("mm")   ? 0.001
                       : unit == QStringLiteral("cm") ? 0.01
                       : unit == QStringLiteral("in") ? 0.0254
                                                      : 1.0;
  return value * scale;
}

[[nodiscard]] inline std::optional<double>
parseDisplayedAngleRadians(QString text) {
  text = text.trimmed().toLower();
  bool degrees = true;
  if (text.endsWith(QStringLiteral("deg")))
    text.chop(3);
  else if (text.endsWith(QChar{0x00b0}))
    text.chop(1);
  else if (text.endsWith(QStringLiteral("rad"))) {
    text.chop(3);
    degrees = false;
  }
  bool valid = false;
  const double value = text.trimmed().toDouble(&valid);
  if (!valid || !std::isfinite(value))
    return std::nullopt;
  return degrees ? value * std::numbers::pi / 180.0 : value;
}

} // namespace kearne::ui
