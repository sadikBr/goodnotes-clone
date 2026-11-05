#include "Assistant.h"

#include <QDate>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <QtDebug>

#include "Catalog.h"
#include "Core.h"
#include "InkCanvas.h"
#include "daftar/Cover.h"
#include "daftar/Daftar.h"
#include "daftar/Store.h"
#include "draw/Covers.h"
#include "editor/Model.h"
#include "editor/Table.h"

namespace {

struct Failure : std::runtime_error {
  explicit Failure(const QString& m) : std::runtime_error(m.toStdString()) {}
  QString message() const { return QString::fromStdString(what()); }
};

/// A command of the core; throws with its message when it fails.
QJsonValue core(const QString& cmd, const QJsonObject& args = {}) {
  QString err;
  const QJsonValue v = InkleafCore::instance()->callJson(cmd, args, &err);
  if (!err.isEmpty()) throw Failure(err);
  return v;
}

QString compact(const QJsonValue& v) {
  return QString::fromUtf8(v.isObject() ? QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact) : QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
}

QJsonObject prop(const char* type, const QString& description) { return {{"type", type}, {"description", description}}; }

QJsonObject schema(const QJsonObject& props, const QStringList& required = {}) {
  return {{"type", "object"}, {"properties", props}, {"required", QJsonArray::fromStringList(required)}};
}

bool arabic(const QString& s) {
  for (const QChar c : s)
    if (c.unicode() >= 0x0600 && c.unicode() <= 0x06FF) return true;
  return false;
}

const QStringList kCoverColors = {"#2563eb", "#0d9488", "#16a34a", "#ca8a04", "#ea580c", "#dc2626", "#db2777", "#7c3aed", "#475569", "#111827"};
const QStringList kFolderColors = {"#2563eb", "#0d9488", "#16a34a", "#ca8a04", "#ea580c", "#dc2626", "#db2777", "#7c3aed", "#64748b"};

/// The paper ids Inkleaf has (the lesson log's colours too).
QStringList paperIds() {
  QStringList out;
  Catalog cat;
  for (const QVariant& v : cat.templates()) {
    const QVariantMap m = v.toMap();
    const QString id = m.value("id").toString();
    if (!id.startsWith("cover") && (!m.value("hidden").toBool() || id.startsWith("daftar-"))) out << id;
  }
  return out;
}

}  // namespace

Assistant::Assistant(QObject* parent) : QObject(parent) {}

void Assistant::setContext(const QString& notebookId, const QString& title, int page) {
  m_ctxNotebook = notebookId;
  m_ctxTitle = title;
  m_ctxPage = page;
}

void Assistant::addUi(const QVariantMap& m) {
  m_ui.append(m);
  emit changed();
}

void Assistant::setStatus(const QString& s) {
  m_status = s;
  emit changed();
}

void Assistant::reset() {
  ++m_generation;
  m_ui.clear();
  m_api = {};
  m_plan = {};
  m_planState.clear();
  m_note.clear();
  m_busy = false;
  m_status.clear();
  m_nextRef = 1;
  emit changed();
}

void Assistant::stop() {
  if (!m_busy) return;
  ++m_generation;
  m_busy = false;
  m_status.clear();
  // The conversation stays usable: the unanswered turn is dropped.
  while (!m_api.isEmpty() && m_api.last().toObject()["role"].toString() != "user") m_api.removeLast();
  if (!m_api.isEmpty()) m_api.removeLast();
  m_plan = {};
  m_planState.clear();
  addUi({{"role", "error"}, {"text", "Stopped."}});
}

// ------------------------------------------------------------------ talking to the AI

void Assistant::send(const QString& text) {
  const QString t = text.trimmed();
  if (t.isEmpty() || m_busy) return;
  // A plan still waiting is dropped by asking something else.
  if (planWaiting()) {
    m_planState = "replaced";
    for (QVariant& v : m_ui) {
      QVariantMap m = v.toMap();
      if (m["role"] == "plan" && m["state"] == "pending") {
        m["state"] = "replaced";
        v = m;
      }
    }
    m_note += "(The user didn't carry out that plan; nothing was changed.)\n";
    m_plan = {};
  }
  addUi({{"role", "user"}, {"text", t}});
  QString where = m_ctxNotebook.isEmpty() ? QStringLiteral("The user is in their library.")
                                          : QString("The user has the notebook “%1” (id %2) open, at page %3.").arg(m_ctxTitle, m_ctxNotebook).arg(m_ctxPage + 1);
  const QDate today = QDate::currentDate();
  const QString when = "Today is " + QLocale(QLocale::English).toString(today, "dddd d MMMM yyyy") + " (" + today.toString(Qt::ISODate) + ").";
  m_api.append(QJsonObject{{"role", "user"}, {"text", m_note + "[" + when + " " + where + "]\n\n" + t}});
  m_note.clear();
  m_rounds = 0;
  m_busy = true;
  setStatus("Thinking…");
  runStep();
}

void Assistant::runStep() {
  const int gen = ++m_generation;
  const QJsonObject req{{"system", systemPrompt()}, {"messages", m_api}, {"tools", tools()}, {"maxTokens", 12000}};
  QThread* t = QThread::create([this, gen, req] {
    QString err;
    const QJsonValue r = InkleafCore::instance()->callJson("aiStep", {{"request", req}}, &err);
    QMetaObject::invokeMethod(this, [this, gen, r, err] { onStep(gen, r, err); }, Qt::QueuedConnection);
  });
  connect(t, &QThread::finished, t, &QObject::deleteLater);
  t->start();
}

void Assistant::onStep(int generation, const QJsonValue& answer, const QString& error) {
  if (generation != m_generation || !m_busy) return;
  if (!error.isEmpty()) {
    m_busy = false;
    m_status.clear();
    QString text = error;
    const QString low = error.toLower();
    if (low.contains("tool") && (low.contains("support") || low.contains("not available") || low.contains("unknown") || low.contains("invalid")))
      text = "This AI model can't use Inkleaf's actions (tool calling). Choose one that can in Settings → AI: Claude, GPT, Gemini, or a local model with tool support.\n\n(" + error + ")";
    // The question is dropped, so asking again works.
    while (!m_api.isEmpty() && m_api.last().toObject()["role"].toString() != "user") m_api.removeLast();
    if (!m_api.isEmpty()) m_api.removeLast();
    m_plan = {};
    addUi({{"role", "error"}, {"text", text}});
    return;
  }
  const QJsonObject a = answer.toObject();
  const QString text = a["text"].toString().trimmed();
  const QJsonArray calls = a["calls"].toArray();
  m_api.append(QJsonObject{{"role", "assistant"}, {"text", text}, {"calls", calls}});
  if (calls.isEmpty()) {
    m_busy = false;
    m_status.clear();
    if (!text.isEmpty()) addUi({{"role", "assistant"}, {"text", text}});
    if (!m_plan.isEmpty()) {
      m_planState = "pending";
      QVariantList steps;
      for (const QJsonValue& v : m_plan) steps << QVariantMap{{"summary", v.toObject()["summary"].toString()}, {"icon", v.toObject()["icon"].toString()}, {"done", false}};
      addUi({{"role", "plan"}, {"steps", steps}, {"state", "pending"}});
    } else if (text.isEmpty()) {
      addUi({{"role", "assistant"}, {"text", "Done."}});
    }
    emit changed();
    return;
  }
  if (!text.isEmpty()) setStatus(text.left(140));
  QJsonArray results;
  for (const QJsonValue& v : calls) {
    const QJsonObject c = v.toObject();
    bool err = false, queued = false;
    QString out;
    try {
      out = handle(c["name"].toString(), c["input"].toObject(), &err, &queued);
    } catch (const Failure& f) {
      out = f.message();
      err = true;
    } catch (const store::StoreError& f) {
      out = f.message();
      err = true;
    }
    results.append(QJsonObject{{"id", c["id"].toString()}, {"content", out.left(60000)}, {"error", err}});
  }
  m_api.append(QJsonObject{{"role", "tool"}, {"results", results}});
  if (++m_rounds > 30) {
    m_busy = false;
    m_status.clear();
    addUi({{"role", "error"}, {"text", "That took too many steps; try asking for less at once."}});
    return;
  }
  setStatus(m_plan.isEmpty() ? "Thinking…" : QString("Planning… (%1 step%2 so far)").arg(m_plan.size()).arg(m_plan.size() == 1 ? "" : "s"));
  runStep();
}

QString Assistant::systemPrompt() const {
  return QStringLiteral(
      "You are the assistant inside Inkleaf, a handwriting notes app used a lot by teachers (in Morocco: Arabic and French; also English). "
      "You can answer questions about the user's notes and you can create and organise things for them with the tools.\n\n"
      "How the tools work:\n"
      "- Reading tools (get_library, search_notes, read_notebook, get_daftar, list_papers) run at once. Use them to find ids and to read before you write or answer. Never guess an id.\n"
      "- Changing tools (create_folder, create_notebook, rename, move, set_cover, write_page, add_heading, add_contents_page, add_daftar_lesson, copy_pages) are NOT done when you call them: "
      "each becomes a step of a plan. When you've planned everything, stop calling tools and write a short reply; the app then shows the user your plan and they choose Do it or Cancel. "
      "Plan the whole job in one go.\n"
      "- Something a step creates gets an id like \"new:3\" (the tool's answer tells you which); use it in later steps (e.g. a notebook inside a folder you're creating).\n"
      "- You can't delete anything, and locked (password-protected) notebooks can't be read or changed.\n"
      "- write_page puts typed content on a page: headings, paragraphs, lists, tables, questions with answer lines. Write real, useful content (a worksheet's actual questions, a summary's actual points), "
      "well organised, at the right school level. Long content continues on new pages by itself.\n"
      "- For a daftar (lesson log), read it with get_daftar first: its classes, timetable and holidays. Lessons go on school days when that class has a lesson in the timetable, not on holidays.\n\n"
      "Your reply: in the user's language (Arabic, French or English, as they write), short and friendly. If you planned steps, say in a sentence or two what the plan does "
      "(the app lists the steps itself). If you only answered a question, just answer it, with where in their notes it comes from. Don't use tables in your reply; use short lists.");
}

QJsonArray Assistant::tools() const {
  QJsonArray t;
  auto add = [&](const QString& name, const QString& description, const QJsonObject& s) { t.append(QJsonObject{{"name", name}, {"description", description}, {"schema", s}}); };
  const QJsonObject idNb = prop("string", "The notebook's id (from get_library, or \"new:N\" for one this plan creates).");
  // --- reading
  add("get_library", "Lists the user's folders and notebooks (ids, titles, folders, page counts, kinds such as daftar or whiteboard).", schema({}));
  add("search_notes", "Searches the user's notes (titles, typed text, PDFs, and handwriting when the AI has read it) for words; returns where they are.",
      schema({{"query", prop("string", "The words to look for.")}}, {"query"}));
  add("read_notebook", "Reads a notebook: its pages (number, paper, section) and their text (typed text, PDF text, and handwriting when the AI has read it), and its contents' headings.",
      schema({{"notebook_id", idNb}, {"question", prop("string", "Optional: what you're looking for, to read the most related pages first in a long notebook.")}}, {"notebook_id"}));
  add("get_daftar", "Reads a daftar (lesson log): its school year, school, subject, classes, timetable (day 1 = Monday … 6 = Saturday, times) and holidays.",
      schema({{"notebook_id", idNb}}, {"notebook_id"}));
  add("list_papers", "Lists the kinds of paper (ids such as ruled, grid, cornell, dotted…) and cover designs and colours that notebooks and pages can use.", schema({}));
  // --- changing (planned)
  add("create_folder", "Plans a new folder (top level, or inside another folder).",
      schema({{"name", prop("string", "The folder's name.")}, {"parent_folder_id", prop("string", "Optional: the folder to put it in.")}, {"color", prop("string", "Optional: a colour as #rrggbb.")}}, {"name"}));
  add("create_notebook", "Plans a new notebook, with a cover page then its first page.",
      schema({{"title", prop("string", "The notebook's title.")},
              {"folder_id", prop("string", "Optional: the folder to put it in.")},
              {"paper", prop("string", "Optional: the paper's id (list_papers); ruled by default.")},
              {"cover_color", prop("string", "Optional: the cover colour as #rrggbb.")},
              {"cover_design", prop("string", "Optional: a cover design id (list_papers); classic by default.")}},
             {"title"}));
  add("rename", "Plans renaming a folder or a notebook.",
      schema({{"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"folder", "notebook"}}}}, {"id", prop("string", "Its id.")}, {"name", prop("string", "The new name.")}}, {"kind", "id", "name"}));
  add("move", "Plans moving a folder or a notebook into a folder (or to the top level).",
      schema({{"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"folder", "notebook"}}}}, {"id", prop("string", "Its id.")}, {"folder_id", prop("string", "The folder to move it into; empty for the top level.")}},
             {"kind", "id"}));
  add("set_cover", "Plans changing a notebook's cover colour and/or design.",
      schema({{"notebook_id", idNb}, {"color", prop("string", "Optional: #rrggbb.")}, {"design", prop("string", "Optional: a cover design id.")}}, {"notebook_id"}));
  QJsonObject block{{"type", "object"},
                    {"properties", QJsonObject{{"type", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"title", "heading", "text", "bullets", "numbered", "table", "questions", "space"}}}},
                                               {"text", prop("string", "For title, heading and text.")},
                                               {"items", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "For bullets, numbered and questions."}}},
                                               {"rows", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}}, {"description", "For table: rows of cells; the first row is the header."}}},
                                               {"answer_lines", prop("integer", "For questions: lines left to answer each one (0 to 8; 2 by default).")}}},
                    {"required", QJsonArray{"type"}}};
  add("write_page", "Plans typed content on a page of a notebook: on a new page at the end (default), or below what's already on a page. Content that doesn't fit continues on new pages.",
      schema({{"notebook_id", idNb},
              {"page", prop("integer", "Optional: the page number (from 1) to write on, below what's there; leave out for a new page at the end.")},
              {"paper", prop("string", "Optional, for a new page: the paper's id.")},
              {"blocks", QJsonObject{{"type", "array"}, {"items", block}, {"description", "The content, top to bottom."}}}},
             {"notebook_id", "blocks"}));
  add("add_heading", "Plans a heading in a notebook's contents for one of its pages (shown in the pages panel, a contents page and exported PDFs).",
      schema({{"notebook_id", idNb}, {"page", prop("integer", "The page number, from 1.")}, {"title", prop("string", "The heading.")}, {"level", prop("integer", "0 for a heading, 1 for a sub-heading.")}},
             {"notebook_id", "page", "title"}));
  add("add_contents_page", "Plans a contents page at the front of a notebook (its headings and chapters with page numbers, each a link), or brings it up to date.",
      schema({{"notebook_id", idNb}}, {"notebook_id"}));
  add("add_daftar_lesson", "Plans a lesson in a daftar: a row for a class on a date (date, class and time from the timetable) with what's done in the lesson, one short line each (up to 8 lines).",
      schema({{"notebook_id", idNb},
              {"class", prop("string", "The class, exactly as the daftar names it.")},
              {"date", prop("string", "The day, YYYY-MM-DD.")},
              {"lines", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "The lesson: its title, then the activities."}}}},
             {"notebook_id", "class", "date", "lines"}));
  add("copy_pages", "Plans copying pages (their paper and what's written on them) from one notebook to the end of another.",
      schema({{"from_notebook_id", prop("string", "The notebook to copy from.")},
              {"pages", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}, {"description", "Page numbers, from 1."}}},
              {"to_notebook_id", idNb}},
             {"from_notebook_id", "pages", "to_notebook_id"}));
  return t;
}

// ------------------------------------------------------------------ tools

QString Assistant::handle(const QString& name, const QJsonObject& input, bool* error, bool* queued) {
  static const QStringList reading = {"get_library", "search_notes", "read_notebook", "get_daftar", "list_papers"};
  static const QStringList changing = {"create_folder", "create_notebook", "rename", "move", "set_cover", "write_page", "add_heading", "add_contents_page", "add_daftar_lesson", "copy_pages"};
  if (reading.contains(name)) return read(name, input);
  if (changing.contains(name)) {
    *queued = true;
    return plan(name, input);
  }
  *error = true;
  return "There's no tool called " + name + ".";
}

QString Assistant::idOf(const QJsonObject& input, const char* key) const { return input.value(QLatin1String(key)).toString().trimmed(); }

QString Assistant::read(const QString& name, const QJsonObject& input) {
  const QJsonObject lib = core("library").toObject();
  QHash<QString, QJsonObject> notebooks;
  for (const QJsonValue& v : lib["notebooks"].toArray())
    if (v.toObject()["trashedAt"].isNull()) notebooks.insert(v.toObject()["id"].toString(), v.toObject());
  auto notebook = [&](const QString& id) {
    if (id.startsWith("new:")) throw Failure("That notebook is only planned so far: it can't be read until the plan is done.");
    const auto it = notebooks.constFind(id);
    if (it == notebooks.constEnd()) throw Failure("There's no notebook with the id " + id + ". Use get_library for the ids.");
    if (it->value("locked").toBool() && !it->value("unlocked").toBool()) throw Failure("“" + it->value("title").toString() + "” is password protected: it can't be read.");
    return *it;
  };
  if (name == "get_library") {
    setStatus("Looking through your library…");
    QJsonArray folders, nbs;
    for (const QJsonValue& v : lib["folders"].toArray()) {
      const QJsonObject f = v.toObject();
      if (!f["trashedAt"].isNull()) continue;
      folders.append(QJsonObject{{"id", f["id"]}, {"name", f["name"]}, {"parent_folder_id", f["parentId"]}});
    }
    for (const QJsonObject& n : notebooks) {
      const QString kind = QJsonDocument::fromJson(n["meta"].toString().toUtf8()).object()["kind"].toString();
      QJsonObject o{{"id", n["id"]}, {"title", n["title"]}, {"folder_id", n["folderId"]}, {"pages", n["pageCount"]}, {"cover_color", n["coverColor"]}};
      if (!kind.isEmpty()) o["kind"] = kind;
      if (n["locked"].toBool()) o["locked"] = true;
      nbs.append(o);
    }
    return compact(QJsonObject{{"folders", folders}, {"notebooks", nbs}});
  }
  if (name == "search_notes") {
    const QString q = input["query"].toString().trimmed();
    if (q.isEmpty()) throw Failure("Give words to look for.");
    setStatus("Searching for “" + q + "”…");
    QJsonArray out;
    for (const QJsonValue& v : core("search", {{"query", q}}).toArray()) {
      const QJsonObject h = v.toObject();
      out.append(QJsonObject{{"notebook_id", h["notebookId"]}, {"notebook", h["notebookTitle"]}, {"page", h["pageNumber"]}, {"snippet", h["snippet"]}});
      if (out.size() >= 25) break;
    }
    return out.isEmpty() ? "Nothing found for “" + q + "”." : compact(out);
  }
  if (name == "read_notebook") {
    const QJsonObject n = notebook(idOf(input, "notebook_id"));
    setStatus("Reading “" + n["title"].toString() + "”…");
    QJsonArray pages;
    int k = 0;
    for (const QJsonValue& v : core("pages", {{"notebookId", n["id"]}}).toArray()) {
      const QJsonObject p = v.toObject();
      QJsonObject o{{"page", ++k}, {"paper", p["template"]}};
      if (!p["section"].toString().isEmpty()) o["section"] = p["section"];
      if (!p["backgroundAsset"].isNull() && !p["backgroundAsset"].toString().isEmpty()) o["pdf"] = true;
      pages.append(o);
    }
    const QJsonObject meta = QJsonDocument::fromJson(n["meta"].toString().toUtf8()).object();
    const QString text = core("notesForAi", {{"notebookId", n["id"]}, {"question", input["question"].toString()}, {"budget", 50000}}).toString();
    return compact(QJsonObject{{"title", n["title"]}, {"pages", pages}, {"headings", meta["contents"].toArray()}}) + "\n\nText of its pages:\n" + (text.trimmed().isEmpty() ? "(nothing typed or read yet)" : text);
  }
  if (name == "get_daftar") {
    const QJsonObject n = notebook(idOf(input, "notebook_id"));
    const auto d = daftar::parseDaftar(QJsonValue(n["meta"].toString()));
    if (!d) throw Failure("“" + n["title"].toString() + "” isn't a daftar.");
    setStatus("Reading the daftar…");
    QJsonObject o = d->toJson();
    o.remove("classColors");
    for (const QString& k : o.keys())
      if (!QStringList{"year", "school", "subject", "teacher", "classes", "timetable", "holidays", "kind"}.contains(k)) o.remove(k);
    return compact(o);
  }
  if (name == "list_papers") {
    QJsonArray papers, designs, own;
    Catalog cat;
    for (const QVariant& v : cat.templates()) {
      const QVariantMap m = v.toMap();
      const QString id = m.value("id").toString();
      if (id.startsWith("cover") || (m.value("hidden").toBool() && !id.startsWith("daftar-"))) continue;
      papers.append(QJsonObject{{"id", id}, {"label", m.value("label").toString()}, {"category", m.value("category").toString()}});
    }
    for (const auto& d : draw::coverDesigns()) designs.append(QJsonObject{{"id", d.id}, {"label", d.label}});
    for (const QJsonValue& v : core("pageTemplates").toArray()) own.append(QJsonObject{{"id", "own:" + v.toObject()["id"].toString()}, {"label", v.toObject()["name"]}});
    return compact(QJsonObject{{"papers", papers}, {"your_templates", own}, {"cover_designs", designs}, {"cover_colors", QJsonArray::fromStringList(kCoverColors)}});
  }
  throw Failure("Unknown tool.");
}

QString Assistant::plan(const QString& name, const QJsonObject& input) {
  // Checks what it can now (so the model can fix a step), and words the step for the user.
  const QJsonObject lib = core("library").toObject();
  QHash<QString, QJsonObject> notebooks, folders;
  for (const QJsonValue& v : lib["notebooks"].toArray())
    if (v.toObject()["trashedAt"].isNull()) notebooks.insert(v.toObject()["id"].toString(), v.toObject());
  for (const QJsonValue& v : lib["folders"].toArray())
    if (v.toObject()["trashedAt"].isNull()) folders.insert(v.toObject()["id"].toString(), v.toObject());
  QHash<QString, QString> planned;  // ref → what it is ("folder"/"notebook") and its name
  for (const QJsonValue& v : m_plan) {
    const QJsonObject s = v.toObject();
    if (!s["ref"].toString().isEmpty()) planned.insert(s["ref"].toString(), s["what"].toString() + ":" + s["name"].toString());
  }
  auto nbName = [&](const QString& id) -> QString {
    if (id.startsWith("new:")) {
      if (!planned.value(id).startsWith("notebook:")) throw Failure(id + " isn't a notebook this plan creates.");
      return "“" + planned.value(id).mid(9) + "”";
    }
    const auto it = notebooks.constFind(id);
    if (it == notebooks.constEnd()) throw Failure("There's no notebook with the id " + id + ". Use get_library for the ids.");
    if (it->value("locked").toBool()) throw Failure("“" + it->value("title").toString() + "” is password protected: it can't be changed.");
    return "“" + it->value("title").toString() + "”";
  };
  auto folderName = [&](const QString& id) -> QString {
    if (id.isEmpty()) return "the top level";
    if (id.startsWith("new:")) {
      if (!planned.value(id).startsWith("folder:")) throw Failure(id + " isn't a folder this plan creates.");
      return "“" + planned.value(id).mid(7) + "”";
    }
    const auto it = folders.constFind(id);
    if (it == folders.constEnd()) throw Failure("There's no folder with the id " + id + ".");
    return "“" + it->value("name").toString() + "”";
  };
  auto checkPaper = [&](const QString& p) {
    if (p.isEmpty() || p.startsWith("own:")) return;
    if (!paperIds().contains(p)) throw Failure("There's no paper “" + p + "”. Use list_papers.");
  };
  QJsonObject step{{"tool", name}, {"input", input}};
  QString summary, icon, ref;
  if (name == "create_folder") {
    const QString n = input["name"].toString().trimmed();
    if (n.isEmpty()) throw Failure("Give the folder a name.");
    const QString parent = idOf(input, "parent_folder_id");
    summary = "Create the folder “" + n + "”" + (parent.isEmpty() ? QString() : " in " + folderName(parent));
    icon = "folder";
    ref = "new:" + QString::number(m_nextRef++);
    step["what"] = "folder";
    step["name"] = n;
  } else if (name == "create_notebook") {
    const QString t = input["title"].toString().trimmed();
    if (t.isEmpty()) throw Failure("Give the notebook a title.");
    const QString f = idOf(input, "folder_id");
    checkPaper(input["paper"].toString());
    QStringList bits;
    if (!input["paper"].toString().isEmpty()) bits << input["paper"].toString() + " paper";
    if (!input["cover_design"].toString().isEmpty()) bits << input["cover_design"].toString() + " cover";
    summary = "Create the notebook “" + t + "”" + (f.isEmpty() ? QString() : " in " + folderName(f)) + (bits.isEmpty() ? QString() : " (" + bits.join(", ") + ")");
    icon = "book";
    ref = "new:" + QString::number(m_nextRef++);
    step["what"] = "notebook";
    step["name"] = t;
  } else if (name == "rename") {
    const QString kind = input["kind"].toString(), id = idOf(input, "id"), n = input["name"].toString().trimmed();
    if (n.isEmpty()) throw Failure("Give the new name.");
    summary = "Rename " + (kind == "folder" ? "the folder " + folderName(id) : "the notebook " + nbName(id)) + " to “" + n + "”";
    icon = "pen";
  } else if (name == "move") {
    const QString kind = input["kind"].toString(), id = idOf(input, "id"), to = idOf(input, "folder_id");
    if (kind == "folder" && id == to) throw Failure("A folder can't go inside itself.");
    summary = "Move " + (kind == "folder" ? "the folder " + folderName(id) : "the notebook " + nbName(id)) + " into " + folderName(to);
    icon = "folder";
  } else if (name == "set_cover") {
    summary = "Change the cover of " + nbName(idOf(input, "notebook_id"));
    icon = "book";
  } else if (name == "write_page") {
    const QString nb = nbName(idOf(input, "notebook_id"));
    const QJsonArray blocks = input["blocks"].toArray();
    if (blocks.isEmpty()) throw Failure("Give the content to write (blocks).");
    checkPaper(input["paper"].toString());
    QString what;
    for (const QJsonValue& b : blocks)
      if (b.toObject()["type"].toString() == "title" || b.toObject()["type"].toString() == "heading") {
        what = b.toObject()["text"].toString().left(60);
        break;
      }
    int questions = 0;
    for (const QJsonValue& b : blocks)
      if (b.toObject()["type"].toString() == "questions") questions += b.toObject()["items"].toArray().size();
    const int page = input["page"].toInt(0);
    summary = (page > 0 ? QString("Write on page %1 of ").arg(page) : QString("Add a page to ")) + nb + (what.isEmpty() ? QString() : ": “" + what + "”") +
              (questions ? QString(" (%1 question%2)").arg(questions).arg(questions == 1 ? "" : "s") : QString());
    icon = "text";
  } else if (name == "add_heading") {
    const QString t = input["title"].toString().trimmed();
    if (t.isEmpty() || input["page"].toInt() < 1) throw Failure("Give the heading and its page number.");
    summary = QString("Add the heading “%1” for page %2 of %3").arg(t).arg(input["page"].toInt()).arg(nbName(idOf(input, "notebook_id")));
    icon = "heading";
  } else if (name == "add_contents_page") {
    summary = "Add a contents page to " + nbName(idOf(input, "notebook_id"));
    icon = "contents";
  } else if (name == "add_daftar_lesson") {
    const QString id = idOf(input, "notebook_id");
    const QString nb = nbName(id);
    const QDate d = QDate::fromString(input["date"].toString(), Qt::ISODate);
    if (!d.isValid()) throw Failure("Give the date as YYYY-MM-DD.");
    const QString cls = input["class"].toString().trimmed();
    if (!id.startsWith("new:")) {
      const auto meta = daftar::parseDaftar(QJsonValue(notebooks.value(id)["meta"].toString()));
      if (!meta) throw Failure(nb + " isn't a daftar.");
      if (!meta->classes.contains(cls)) throw Failure("The daftar's classes are: " + meta->classes.join(", ") + ".");
    }
    const QJsonArray lines = input["lines"].toArray();
    if (lines.isEmpty()) throw Failure("Give the lesson's lines.");
    summary = cls + " · " + QLocale(QLocale::English).toString(d, "ddd d MMM") + ": “" + lines[0].toString().left(60) + "”";
    icon = "docs";
  } else if (name == "copy_pages") {
    const QJsonArray pages = input["pages"].toArray();
    if (pages.isEmpty()) throw Failure("Give the page numbers to copy.");
    summary = QString("Copy %1 page%2 of %3 into %4").arg(pages.size()).arg(pages.size() == 1 ? "" : "s").arg(nbName(idOf(input, "from_notebook_id")), nbName(idOf(input, "to_notebook_id")));
    icon = "copy";
  }
  step["summary"] = summary;
  step["icon"] = icon;
  if (!ref.isEmpty()) step["ref"] = ref;
  m_plan.append(step);
  QString out = QString("Planned as step %1: %2. It will be done when the user approves the plan.").arg(m_plan.size()).arg(summary);
  if (!ref.isEmpty()) out += " Its id for later steps: " + ref + ".";
  return out;
}

// ------------------------------------------------------------------ carrying out the plan

QJsonValue Assistant::resolve(const QJsonValue& v) const {
  if (v.isString()) {
    const QString s = v.toString().trimmed();
    if (s.startsWith("new:")) {
      if (!m_refs.contains(s)) throw Failure(s + " wasn't created");
      return m_refs.value(s);
    }
    return v;
  }
  if (v.isObject()) {
    QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) it.value() = resolve(it.value());
    return o;
  }
  if (v.isArray()) {
    QJsonArray a = v.toArray();
    for (int i = 0; i < a.size(); ++i) a[i] = resolve(a[i]);
    return a;
  }
  return v;
}

void Assistant::approve() {
  if (!planWaiting()) return;
  emit aboutToChange();
  m_journal = {};
  m_touched.clear();
  m_refs.clear();
  QString failed;
  int done = 0;
  for (const QJsonValue& v : m_plan) {
    try {
      execute(v.toObject());
      ++done;
    } catch (const Failure& f) {
      failed = f.message();
    } catch (const store::StoreError& f) {
      failed = f.message();
    }
    if (!failed.isEmpty()) break;
  }
  m_planState = failed.isEmpty() ? "done" : "failed";
  for (QVariant& v : m_ui) {
    QVariantMap m = v.toMap();
    if (m["role"] != "plan" || m["state"] != "pending") continue;
    QVariantList steps = m["steps"].toList();
    for (int i = 0; i < steps.size(); ++i) {
      QVariantMap s = steps[i].toMap();
      s["done"] = i < done;
      if (i == done && !failed.isEmpty()) s["failed"] = true;
      steps[i] = s;
    }
    m["steps"] = steps;
    m["state"] = m_planState;
    if (!failed.isEmpty()) m["error"] = failed;
    v = m;
  }
  m_note += failed.isEmpty() ? "(The user approved the plan and all of it was done.)\n"
                             : QString("(The user approved the plan; steps 1 to %1 were done, then step %2 failed: %3)\n").arg(done).arg(done + 1).arg(failed);
  m_plan = {};
  emit changed();
  emit notebooksChanged(m_touched);
}

void Assistant::cancelPlan() {
  if (!planWaiting()) return;
  m_planState = "cancelled";
  for (QVariant& v : m_ui) {
    QVariantMap m = v.toMap();
    if (m["role"] == "plan" && m["state"] == "pending") {
      m["state"] = "cancelled";
      v = m;
    }
  }
  m_note += "(The user cancelled the plan; nothing was changed.)\n";
  m_plan = {};
  emit changed();
}

void Assistant::execute(const QJsonObject& stepIn) {
  const QString tool = stepIn["tool"].toString();
  const QJsonObject in = resolve(stepIn["input"]).toObject();
  const QString ref = stepIn["ref"].toString();
  if (tool == "create_folder") {
    QJsonObject args{{"name", in["name"].toString().trimmed()}, {"color", in["color"].toString().isEmpty() ? kFolderColors[0] : in["color"].toString()}};
    if (!in["parent_folder_id"].toString().isEmpty()) args["parentId"] = in["parent_folder_id"];
    const QString id = core("createFolder", args).toObject()["id"].toString();
    m_refs.insert(ref, id);
    journal({{"op", "deleteFolder"}, {"id", id}});
    return;
  }
  if (tool == "create_notebook") {
    QString paper = in["paper"].toString().isEmpty() ? QStringLiteral("ruled") : in["paper"].toString();
    QString own;
    if (paper.startsWith("own:")) {
      own = paper.mid(4);
      paper = "blank";
    }
    const QString color = in["cover_color"].toString().isEmpty() ? kCoverColors[0] : in["cover_color"].toString();
    QJsonObject input{{"title", in["title"].toString().trimmed()}, {"template", paper}, {"coverColor", color}, {"paperColor", "#fffdf7"}, {"width", 794}, {"height", 1123}, {"coverPage", true}};
    if (!in["folder_id"].toString().isEmpty()) input["folderId"] = in["folder_id"];
    const QJsonObject nb = core("createNotebook", {{"input", input}}).toObject();
    const QString id = nb["id"].toString();
    m_refs.insert(ref, id);
    journal({{"op", "deleteNotebook"}, {"id", id}});
    const QString design = in["cover_design"].toString();
    if (!design.isEmpty() && design != "classic") cover::applyCover(nb, design, color);
    else cover::setUpCover(store::notebook(id));
    if (!own.isEmpty()) {
      const QJsonArray pages = store::pages(id);
      if (pages.size() > 1) core("usePageTemplate", {{"pageId", pages[1].toObject()["id"]}, {"templateId", own}});
    }
    touch(id);
    return;
  }
  if (tool == "rename" || tool == "move") {
    const bool folder = in["kind"].toString() == "folder";
    const QString id = in["id"].toString();
    const QJsonObject lib = core("library").toObject();
    QJsonObject before;
    for (const QJsonValue& v : lib[folder ? "folders" : "notebooks"].toArray())
      if (v.toObject()["id"].toString() == id) before = v.toObject();
    if (before.isEmpty()) throw Failure("It's no longer in the library.");
    if (tool == "rename") {
      core(folder ? "renameFolder" : "renameNotebook", {{"id", id}, {folder ? "name" : "title", in["name"].toString().trimmed()}});
      journal({{"op", folder ? "renameFolder" : "renameNotebook"}, {"id", id}, {"name", before[folder ? "name" : "title"]}});
    } else {
      const QString to = in["folder_id"].toString();
      const QJsonValue target = to.isEmpty() ? QJsonValue() : QJsonValue(to);
      core(folder ? "moveFolder" : "moveNotebook", {{"id", id}, {folder ? "parentId" : "folderId", target}});
      journal({{"op", folder ? "moveFolder" : "moveNotebook"}, {"id", id}, {"to", before[folder ? "parentId" : "folderId"]}});
    }
    if (!folder) touch(id);
    return;
  }
  if (tool == "set_cover") {
    const QString id = in["notebook_id"].toString();
    const QJsonObject nb = store::notebook(id);
    const QJsonArray pages = store::pages(id);
    const QJsonObject first = pages.isEmpty() ? QJsonObject() : pages[0].toObject();
    journal({{"op", "cover"}, {"id", id}, {"color", nb["coverColor"]}, {"pageId", first["id"]}, {"template", first["template"]}, {"paper", first["paperColor"]}});
    const QString color = in["color"].toString().isEmpty() ? nb["coverColor"].toString() : in["color"].toString();
    QString design = in["design"].toString();
    if (design.isEmpty()) design = first["template"].toString().startsWith("cover-") ? first["template"].toString().mid(6) : QStringLiteral("classic");
    cover::applyCover(nb, design, color);
    touch(id);
    return;
  }
  if (tool == "write_page") {
    const QString id = in["notebook_id"].toString();
    QJsonArray pages = store::pages(id);
    int at = in["page"].toInt(0) - 1;
    QJsonObject page;
    std::vector<ed::El> els;
    double y = 0;
    auto newPage = [&](const QString& paper) {
      QString tmpl = paper;
      QString own;
      if (tmpl.startsWith("own:")) {
        own = tmpl.mid(4);
        tmpl.clear();
      }
      // (A notebook whose last page is its cover: plain ruled.)
      if (tmpl.isEmpty() && !pages.isEmpty() && pages.last().toObject()["template"].toString().startsWith("cover")) tmpl = "ruled";
      const QJsonObject p = core("addPage", {{"notebookId", id}, {"template", tmpl.isEmpty() ? QJsonValue() : QJsonValue(tmpl)}}).toObject();
      journal({{"op", "deletePage"}, {"id", p["id"]}});
      if (!own.isEmpty()) core("usePageTemplate", {{"pageId", p["id"]}, {"templateId", own}});
      pages = store::pages(id);
      for (const QJsonValue& v : pages)
        if (v.toObject()["id"] == p["id"]) return v.toObject();
      return p;
    };
    if (at >= 0) {
      if (at >= pages.size()) throw Failure(QString("The notebook has %1 pages").arg(pages.size()));
      page = pages[at].toObject();
      els = store::elements(page["id"].toString());
      // Below what's there.
      for (const ed::El& e : els) y = std::max(y, ed::EditorCore::boxOf(e).y + ed::EditorCore::boxOf(e).h);
    } else {
      // A notebook this plan made: its empty first page takes the content.
      bool reused = false;
      if (m_refs.values().contains(id) && !pages.isEmpty()) {
        const QJsonObject last = pages.last().toObject();
        if (!last["template"].toString().startsWith("cover") && store::elements(last["id"].toString()).empty()) {
          page = last;
          const QString paper = in["paper"].toString();
          if (paper.startsWith("own:")) core("usePageTemplate", {{"pageId", page["id"]}, {"templateId", paper.mid(4)}});
          else store::setPageTemplate(page["id"].toString(), paper.isEmpty() ? QStringLiteral("blank") : paper, page["paperColor"].toString());
          reused = true;
        }
      }
      if (!reused) page = newPage(in["paper"].toString().isEmpty() ? QStringLiteral("blank") : in["paper"].toString());
    }
    const QString paperForMore = in["paper"].toString().isEmpty() ? QStringLiteral("blank") : in["paper"].toString();
    double W = page["width"].toDouble(794), H = page["height"].toDouble(1123);
    double margin = std::max(48.0, W * 0.08);
    double top = margin + 8;
    y = std::max(top, y + (y > 0 ? 24 : 0));
    QJsonArray added;
    double z = 0;
    for (const ed::El& e : els) z = std::max(z, e.z);
    auto flush = [&]() {
      if (added.isEmpty() && in["page"].toInt(0) <= 0) return;
      store::saveElements(page["id"].toString(), els);
    };
    auto room = [&](double h) {
      if (y + h <= H - margin || y <= top + 1) return;
      flush();
      if (in["page"].toInt(0) > 0 && !added.isEmpty()) journal({{"op", "removeElements"}, {"pageId", page["id"]}, {"ids", added}});
      added = {};
      page = newPage(paperForMore);
      els = {};
      z = 0;
      W = page["width"].toDouble(794);
      H = page["height"].toDouble(1123);
      y = top;
    };
    auto textEl = [&](const QString& t, double size, bool bold, const QString& color, double indent = 0) {
      ed::El e;
      e.id = ed::newId();
      e.kind = ed::Kind::Text;
      e.x = margin + indent;
      e.w = W - 2 * margin - indent;
      e.text = t;
      e.fontSize = size;
      e.color = color;
      e.font = QStringLiteral("sans");
      if (bold) e.bold = true;
      if (arabic(t)) e.align = QStringLiteral("right");
      // How tall it is: lines that wrap at about half the size per character.
      const double perLine = std::max(8.0, e.w / (size * 0.52));
      int lines = 0;
      for (const QString& l : t.split('\n')) lines += std::max(1, int(std::ceil(l.size() / perLine)));
      const double h = lines * size * 1.45;
      room(h);
      e.y = y;
      e.z = ++z;
      added.append(e.id);
      els.push_back(std::move(e));
      y += h;
    };
    auto lineEl = [&](double x0, double x1) {
      ed::El e;
      e.id = ed::newId();
      e.kind = ed::Kind::Shape;
      e.color = "#9ca3af";
      e.width = 1;
      e.shape.kind = "line";
      e.shape.points = {{x0, y}, {x1, y}};
      e.z = ++z;
      added.append(e.id);
      els.push_back(std::move(e));
    };
    for (const QJsonValue& bv : in["blocks"].toArray()) {
      const QJsonObject b = bv.toObject();
      const QString type = b["type"].toString();
      QStringList items;
      for (const QJsonValue& v : b["items"].toArray()) items << v.toString().trimmed();
      if (type == "title") {
        textEl(b["text"].toString(), 28, true, "#111827");
        y += 14;
      } else if (type == "heading") {
        y += 8;
        textEl(b["text"].toString(), 20, true, "#1f2937");
        y += 6;
      } else if (type == "text") {
        textEl(b["text"].toString(), 15, false, "#1f2937");
        y += 10;
      } else if (type == "bullets" || type == "numbered") {
        int n = 0;
        for (const QString& it : items) textEl((type == "bullets" ? QStringLiteral("• ") : QString::number(++n) + ". ") + it, 15, false, "#1f2937", 8);
        y += 10;
      } else if (type == "questions") {
        const int lines = std::clamp(b.contains("answer_lines") ? b["answer_lines"].toInt() : 2, 0, 8);
        int n = 0;
        for (const QString& q : items) {
          room(26 + lines * 30);
          textEl(QString::number(++n) + ". " + q, 15, true, "#111827");
          for (int k = 0; k < lines; ++k) {
            y += 30;
            lineEl(margin + 18, W - margin);
          }
          y += 18;
        }
      } else if (type == "table") {
        const QJsonArray rows = b["rows"].toArray();
        if (rows.isEmpty()) continue;
        int cols = 1;
        for (const QJsonValue& r : rows) cols = std::max(cols, int(r.toArray().size()));
        cols = std::min(cols, 12);
        ed::El e;
        e.id = ed::newId();
        e.kind = ed::Kind::Table;
        e.color = "#374151";
        e.width = 1.5;
        e.fontSize = 13;
        e.x = margin;
        e.w = W - 2 * margin;
        e.h = rows.size() * 30.0;
        room(e.h);
        e.y = y;
        ed::TableData t;
        t.cols.assign(size_t(cols), 1.0);
        t.rows.assign(size_t(rows.size()), 1.0);
        t.cells.assign(size_t(rows.size()), QStringList(QVector<QString>(cols).toList()));
        t.header = rows.size() > 1;
        for (int r = 0; r < rows.size(); ++r)
          for (int c = 0; c < cols && c < rows[r].toArray().size(); ++c) t.setCell(r, c, rows[r].toArray()[c].toString());
        ed::setTableData(e, t);
        e.z = ++z;
        added.append(e.id);
        y += e.h + 16;
        els.push_back(std::move(e));
      } else if (type == "space") {
        y += 40;
      }
    }
    flush();
    if (in["page"].toInt(0) > 0 && !added.isEmpty()) journal({{"op", "removeElements"}, {"pageId", page["id"]}, {"ids", added}});
    touch(id);
    return;
  }
  if (tool == "add_heading") {
    const QString id = in["notebook_id"].toString();
    const QJsonObject nb = store::notebook(id);
    const QJsonArray pages = store::pages(id);
    const int at = in["page"].toInt() - 1;
    if (at < 0 || at >= pages.size()) throw Failure(QString("The notebook has %1 pages").arg(pages.size()));
    QJsonObject meta = QJsonDocument::fromJson(nb["meta"].toString().toUtf8()).object();
    journal({{"op", "setMeta"}, {"id", id}, {"meta", nb["meta"]}});
    QJsonArray list = meta["contents"].toArray();
    list.append(QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"page", pages[at].toObject()["id"]}, {"title", in["title"].toString().trimmed().left(200)}, {"level", std::clamp(in["level"].toInt(), 0, 3)}});
    meta["contents"] = list;
    store::setNotebookMeta(id, QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)));
    touch(id);
    return;
  }
  if (tool == "add_contents_page") {
    const QString id = in["notebook_id"].toString();
    const QJsonObject nb = store::notebook(id);
    const QString before = QJsonDocument::fromJson(nb["meta"].toString().toUtf8()).object()["contentsPage"].toString();
    QJsonArray beforeRows;
    if (!before.isEmpty())
      for (const ed::El& e : store::elements(before)) beforeRows.append(ed::toStored(e));
    // The editor's own code, on the notebook, off screen.
    InkCanvas c;
    c.setNotebookId(id);
    c.insertContentsPage();
    c.flush();
    const QString after = QJsonDocument::fromJson(store::notebook(id)["meta"].toString().toUtf8()).object()["contentsPage"].toString();
    if (before.isEmpty()) {
      journal({{"op", "setMeta"}, {"id", id}, {"meta", nb["meta"]}});
      if (!after.isEmpty()) journal({{"op", "deletePage"}, {"id", after}});
    } else {
      journal({{"op", "pageRows"}, {"pageId", before}, {"rows", beforeRows}});
    }
    touch(id);
    return;
  }
  if (tool == "add_daftar_lesson") {
    QStringList lines;
    for (const QJsonValue& v : in["lines"].toArray()) lines << v.toString().trimmed().left(140);
    const QJsonObject r = daftar::addLessonOn(in["notebook_id"].toString(), in["class"].toString().trimmed(), QDate::fromString(in["date"].toString(), Qt::ISODate), lines.mid(0, 8));
    if (r["newPage"].toBool()) journal({{"op", "deletePage"}, {"id", r["pageId"]}});
    else journal({{"op", "removeElements"}, {"pageId", r["pageId"]}, {"ids", r["ids"]}});
    touch(in["notebook_id"].toString());
    return;
  }
  if (tool == "copy_pages") {
    const QString from = in["from_notebook_id"].toString(), to = in["to_notebook_id"].toString();
    const QJsonArray src = store::pages(from);
    for (const QJsonValue& v : in["pages"].toArray()) {
      const int at = v.toInt() - 1;
      if (at < 0 || at >= src.size()) throw Failure(QString("The notebook to copy from has %1 pages").arg(src.size()));
      const QJsonObject p = src[at].toObject();
      const bool pdf = !p["backgroundAsset"].isNull() && !p["backgroundAsset"].toString().isEmpty();
      const QString tmpl = p["template"].toString().startsWith("cover") ? QStringLiteral("blank") : p["template"].toString();
      const QJsonObject np = core("addPage", {{"notebookId", to}, {"template", tmpl}}).toObject();
      journal({{"op", "deletePage"}, {"id", np["id"]}});
      store::setPageTemplate(np["id"].toString(), tmpl, p["paperColor"].toString());
      core("setPageSize", {{"pageId", np["id"]}, {"width", p["width"]}, {"height", p["height"]}});
      std::vector<ed::El> els = store::elements(p["id"].toString());
      for (ed::El& e : els) e.id = ed::newId();
      store::saveElements(np["id"].toString(), els);
      Q_UNUSED(pdf);  // (a PDF page's own picture stays with its notebook: what's written on it is copied)
    }
    touch(to);
    return;
  }
  throw Failure("Unknown step " + tool);
}

void Assistant::undo() {
  if (m_journal.isEmpty()) return;
  emit aboutToChange();
  m_touched.clear();
  QStringList problems;
  for (int i = m_journal.size() - 1; i >= 0; --i) {
    const QJsonObject j = m_journal[i].toObject();
    const QString op = j["op"].toString();
    try {
      if (op == "deleteFolder") core("deleteFolder", {{"id", j["id"]}});
      else if (op == "deleteNotebook") core("deleteNotebook", {{"id", j["id"]}});
      else if (op == "renameFolder") core("renameFolder", {{"id", j["id"]}, {"name", j["name"]}});
      else if (op == "renameNotebook") {
        core("renameNotebook", {{"id", j["id"]}, {"title", j["name"]}});
        touch(j["id"].toString());
      } else if (op == "moveFolder") core("moveFolder", {{"id", j["id"]}, {"parentId", j["to"]}});
      else if (op == "moveNotebook") core("moveNotebook", {{"id", j["id"]}, {"folderId", j["to"]}});
      else if (op == "deletePage") core("deletePage", {{"pageId", j["id"]}});
      else if (op == "removeElements") {
        QSet<QString> ids;
        for (const QJsonValue& v : j["ids"].toArray()) ids.insert(v.toString());
        std::vector<ed::El> els = store::elements(j["pageId"].toString());
        els.erase(std::remove_if(els.begin(), els.end(), [&](const ed::El& e) { return ids.contains(e.id); }), els.end());
        store::saveElements(j["pageId"].toString(), els);
      } else if (op == "pageRows") {
        core("saveElements", {{"pageId", j["pageId"]}, {"elements", j["rows"]}});
      } else if (op == "setMeta") {
        core("setNotebookMeta", {{"id", j["id"]}, {"meta", j["meta"]}});
        touch(j["id"].toString());
      } else if (op == "cover") {
        core("setNotebookCover", {{"id", j["id"]}, {"color", j["color"]}});
        if (!j["pageId"].toString().isEmpty()) core("setPageTemplate", {{"pageId", j["pageId"]}, {"template", j["template"]}, {"paperColor", j["paper"]}});
        cover::setUpCover(store::notebook(j["id"].toString()));
        touch(j["id"].toString());
      }
    } catch (const Failure& f) {
      problems << f.message();
    } catch (const store::StoreError& f) {
      problems << f.message();
    }
  }
  // Pages that were deleted belonged to notebooks: those reload too.
  for (const QJsonValue& v : m_journal) {
    const QJsonObject j = v.toObject();
    if (j["op"] == "deletePage" || j["op"] == "removeElements" || j["op"] == "pageRows") m_touched << QStringLiteral("*");
  }
  m_journal = {};
  for (QVariant& v : m_ui) {
    QVariantMap m = v.toMap();
    if (m["role"] == "plan" && (m["state"] == "done" || m["state"] == "failed")) {
      m["state"] = "undone";
      v = m;
    }
  }
  m_note += "(The user undid everything the last plan did.)\n";
  if (!problems.isEmpty()) addUi({{"role", "error"}, {"text", "Some of it couldn't be undone: " + problems.join("; ")}});
  emit changed();
  emit notebooksChanged(m_touched);
}

// ------------------------------------------------------------------ for the self-tests

QString Assistant::runToolForTest(const QString& name, const QJsonObject& input, bool* queued) {
  bool err = false, q = false;
  QString out;
  try {
    out = handle(name, input, &err, &q);
  } catch (const Failure& f) {
    out = "ERROR: " + f.message();
  } catch (const store::StoreError& f) {
    out = "ERROR: " + f.message();
  }
  if (queued) *queued = q;
  if (q && !out.startsWith("ERROR")) m_planState = "pending";
  return out;
}

bool Assistant::approveForTest() {
  if (m_plan.isEmpty()) return false;
  m_planState = "pending";
  m_ui.append(QVariantMap{{"role", "plan"}, {"state", "pending"}, {"steps", QVariantList()}});
  approve();
  return m_planState == "done";
}
