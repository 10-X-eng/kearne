#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

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
                        std::function<QString()> presentationStatus,
                        QObject *parent);
  ~ObservationController() override;

private:
  struct PendingPointerMotion;

  void framePresented();
  void performNextOperation();
  void continuePointerMotion();
  void capturePreviewImage();
  void recordPresentedState();
  void finishActiveReceipt();
  void capture();

  QQuickWindow &window_;
  UiSession &session_;
  ThemeManager &themes_;
  QString outputDirectory_;
  QList<QJsonObject> pendingOperations_;
  QList<QJsonObject> actionReceipts_;
  QString sessionId_;
  std::function<bool()> presentationCurrent_;
  std::function<QString()> presentationStatus_;
  int presentedFrames_ = 0;
  int settledFrames_ = 0;
  QByteArray lastFrameFingerprint_;
  std::unique_ptr<PendingPointerMotion> pointerMotion_;
  std::optional<qsizetype> activeReceipt_;
  std::optional<std::chrono::steady_clock::time_point> actionStarted_;
  std::optional<std::chrono::steady_clock::time_point> inputCompleted_;
  bool actionScheduled_ = false;
  bool pointerStepScheduled_ = false;
  bool pointerPreviewCapturePending_ = false;
  bool captureScheduled_ = false;
};

} // namespace kearne::ui
