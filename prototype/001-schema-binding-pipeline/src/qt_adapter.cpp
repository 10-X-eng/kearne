#include "engine.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace wire = kearne::schema::v1;

QString qString(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

wire::RpcRequest requestFromUi(const QVariantMap &arguments) {
  wire::RpcRequest request;
  wire::CommandEnvelope *command = request.mutable_command();
  command->set_request_id("request-0001");
  command->set_base_revision_id("revision-0000");
  command->mutable_rename_project()->set_project_id(
      arguments.value(QStringLiteral("projectId")).toString().toStdString());
  command->mutable_rename_project()->set_display_name(
      arguments.value(QStringLiteral("displayName")).toString().toStdString());
  return request;
}

} // namespace

int main() {
  try {
    const QVariantMap arguments{
        {QStringLiteral("projectId"), QStringLiteral("project-01")},
        {QStringLiteral("displayName"), QStringLiteral("Mounting Plate")},
    };
    kearne::schema_prototype::Engine engine;
    const wire::RpcResponse command = engine.handle(requestFromUi(arguments));
    const wire::RpcResponse query =
        engine.handle(kearne::schema_prototype::makeMetadataQuery(
            std::string(command.command().revision_id())));
    const QVariantMap result{
        {QStringLiteral("displayName"), qString(query.query().display_name())},
        {QStringLiteral("revisionId"),
         qString(query.query().observed_revision_id())},
    };
    if (result.value(QStringLiteral("displayName")) !=
            QStringLiteral("Mounting Plate") ||
        result.value(QStringLiteral("revisionId")) !=
            QStringLiteral("revision-0001")) {
      throw std::runtime_error("Qt adapter changed the semantic result");
    }
    const QByteArray encoded =
        QJsonDocument(QJsonObject::fromVariantMap(result))
            .toJson(QJsonDocument::Compact);
    std::cout.write(encoded.constData(), encoded.size());
    std::cout << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
