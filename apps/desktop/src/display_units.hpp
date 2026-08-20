#pragma once

#include "frontend_contract.hpp"

#include <QLocale>
#include <QString>

#include <cmath>

namespace kearne::ui {

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

} // namespace kearne::ui
