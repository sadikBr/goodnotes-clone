#include "Store.h"

#include "../Core.h"

namespace store {

namespace {
QJsonValue call(const QString& cmd, const QJsonObject& args) {
  QString err;
  const QJsonValue v = InkleafCore::instance()->callJson(cmd, args, &err);
  if (!err.isEmpty()) throw StoreError(err);
  return v;
}
}  // namespace

QJsonObject createNotebook(const QJsonObject& input) { return call("createNotebook", {{"input", input}}).toObject(); }
QJsonObject notebook(const QString& id) { return call("notebook", {{"id", id}}).toObject(); }
QJsonArray pages(const QString& notebookId) { return call("pages", {{"notebookId", notebookId}}).toArray(); }
QJsonObject addPage(const QString& notebookId, int index, const QString& tmpl) {
  return call("addPage", {{"notebookId", notebookId}, {"index", index}, {"template", tmpl}}).toObject();
}
void setPageTemplate(const QString& pageId, const QString& tmpl, const QString& paperColor) {
  call("setPageTemplate", {{"pageId", pageId}, {"template", tmpl}, {"paperColor", paperColor}});
}
void setPageSection(const QString& pageId, const QString& section) {
  call("setPageSection", {{"pageId", pageId}, {"section", section.isEmpty() ? QJsonValue() : QJsonValue(section)}});
}
void setPageThumbnail(const QString& pageId, const QString& dataUrl) { call("setPageThumbnail", {{"pageId", pageId}, {"thumbnail", dataUrl}}); }
std::vector<ed::El> elements(const QString& pageId) { return ed::fromStored(call("elements", {{"pageId", pageId}}).toArray()); }
void saveElements(const QString& pageId, const std::vector<ed::El>& els) {
  QJsonArray rows;
  for (const ed::El& e : els) rows.append(ed::toStored(e));
  call("saveElements", {{"pageId", pageId}, {"elements", rows}});
}
void setNotebookMeta(const QString& id, const QString& meta) { call("setNotebookMeta", {{"id", id}, {"meta", meta}}); }
void setNotebookCover(const QString& id, const QString& color) { call("setNotebookCover", {{"id", id}, {"color", color}}); }

}  // namespace store
