#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QQuickWindow;

namespace kearne::ui {

class ThemeManager;
class UiSession;

[[nodiscard]] QList<QJsonObject>
parseSemanticOperations(const QStringList &actions,
                        const QStringList &encodedOperations);

class ObservationController final : public QObject {
public:
  ObservationController(QQuickWindow &window, UiSession &session,
                        ThemeManager &themes, QString outputDirectory,
                        QList<QJsonObject> operations,
                        std::function<bool()> presentationCurrent,
                        QObject *parent);

private:
  void performNextOperation();
  void capture();

  QQuickWindow &window_;
  UiSession &session_;
  ThemeManager &themes_;
  QString outputDirectory_;
  QList<QJsonObject> pendingOperations_;
  QList<QJsonObject> actionReceipts_;
  QString sessionId_;
  std::function<bool()> presentationCurrent_;
  int presentedFrames_ = 0;
  int settledFrames_ = 0;
  QByteArray lastFrameFingerprint_;
  bool actionScheduled_ = false;
  bool captureScheduled_ = false;
};

} // namespace kearne::ui
