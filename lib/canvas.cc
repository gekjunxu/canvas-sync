#include "canvas.h"

#include <QJsonParseError>
#include <QTimer>

namespace
{
constexpr int MAX_RETRIES = 3;
constexpr int RETRY_DELAY_MS = 1000;

int response_status(QNetworkReply *reply)
{
  return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

bool is_retryable(QNetworkReply *reply, int status)
{
  if (status == 429 || (status >= 500 && status <= 599)) return true;

  switch (reply->error()) {
  case QNetworkReply::ConnectionRefusedError:
  case QNetworkReply::RemoteHostClosedError:
  case QNetworkReply::TemporaryNetworkFailureError:
  case QNetworkReply::NetworkSessionFailedError:
  case QNetworkReply::TimeoutError:
  case QNetworkReply::UnknownNetworkError:
    return true;
  default:
    return false;
  }
}

QString next_page_url(QNetworkReply *reply)
{
  const QString link_header =
      QString::fromUtf8(reply->rawHeader("Link"));
  for (const QString &link : link_header.split(',', Qt::SkipEmptyParts)) {
    if (!link.contains("rel=\"next\"") && !link.contains("rel=next"))
      continue;

    const int start = link.indexOf('<');
    const int end = link.indexOf('>', start + 1);
    if (start >= 0 && end > start) return link.mid(start + 1, end - start - 1);
  }
  return {};
}

void log_request_failure(QNetworkReply *reply, int status)
{
  // Log only the path. A download URL can contain temporary credentials.
  qWarning() << "Canvas request failed:" << reply->url().path()
             << "HTTP status:" << status
             << "network error:" << reply->errorString();
  if (status == 403)
    qWarning() << "Canvas denied access; check course enrollment and token "
                  "permissions.";
  else if (status == 429)
    qWarning() << "Canvas rate-limited the request; retrying when possible.";
}
}

void ICanvas::reset_counts()
{
  for (size_t i = 0; i < 4; i++)
    count[i] = 0;
}

size_t ICanvas::increment_total_downloads(size_t n)
{
  size_t x;
  count_mtx.lock();
  count[DOWNLOAD_TOTAL] += n;
  x = count[DOWNLOAD_TOTAL];
  count_mtx.unlock();
  return x;
}

size_t ICanvas::increment_done_downloads()
{
  size_t x;
  count_mtx.lock();
  count[DOWNLOAD_DONE]++;
  x = count[DOWNLOAD_DONE];
  count_mtx.unlock();
  return x;
}

bool ICanvas::is_done_downloading()
{
  bool x;
  count_mtx.lock();
  x = count[DOWNLOAD_DONE] == count[DOWNLOAD_TOTAL];
  count_mtx.unlock();
  return x;
}

void write_file(const std::filesystem::path &path, const QByteArray &data)
{
  QString file = QString::fromStdString(path.string());
  if (std::filesystem::exists(path)) {
    QFile::remove(file);
  }
  QSaveFile f(file);
  if (!f.open(QIODevice::WriteOnly)) {
    qWarning() << "Could not open file for writing:" << file;
    return;
  }
  f.write(data);
  f.commit();
  f.deleteLater();
}

void Canvas::terminate(QNetworkReply *r)
{
  disconnect(r);
  r->deleteLater();
}

void Canvas::request_json(const QString &url, int retry_count,
                          JsonCallback done)
{
  auto *reply = this->get_full(url);
  connect(reply, &QNetworkReply::finished, this, [=]() {
    const int status = response_status(reply);
    const bool failed = reply->error() != QNetworkReply::NoError ||
                        status >= 400;

    if (failed) {
      log_request_failure(reply, status);
      if (retry_count < MAX_RETRIES && is_retryable(reply, status)) {
        const int delay = RETRY_DELAY_MS * (1 << retry_count);
        qWarning() << "Retrying Canvas request in" << delay << "ms";
        terminate(reply);
        QTimer::singleShot(delay, this, [=]() {
          request_json(url, retry_count + 1, done);
        });
        return;
      }

      terminate(reply);
      done(QJsonDocument(), {});
      return;
    }

    const QByteArray body = reply->readAll();
    const QString next = next_page_url(reply);
    terminate(reply);

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
      qWarning() << "Canvas returned invalid JSON:" << parse_error.errorString();
      done(QJsonDocument(), {});
      return;
    }
    done(document, next);
  });
}

void Canvas::fetch_json_pages(const QString &url, QJsonArray pages,
                              JsonCallback done)
{
  request_json(url, 0, [=](const QJsonDocument &document, const QString &next) {
    if (!document.isArray()) {
      done(QJsonDocument(), {});
      return;
    }

    QJsonArray all_pages = pages;
    for (const QJsonValue &value : document.array()) all_pages.append(value);

    if (next.isEmpty()) {
      done(QJsonDocument(all_pages), {});
      return;
    }
    fetch_json_pages(next, all_pages, done);
  });
}

QNetworkReply *Canvas::get_full(const QString &url)
{
  QNetworkRequest r((QUrl(url)));
  if (!this->token_inner.isEmpty()) {
    r.setRawHeader("Authorization", ("Bearer " + this->token_inner).toUtf8());
  }
  return this->nw->get(r);
}

QNetworkReply *Canvas::get(const QString &url)
{
  return this->get_full(this->base_url + url);
}

QNetworkReply *Canvas::get(const QString &fmt, const int &param)
{
  return this->get_full(this->base_url + QString(fmt).arg(param));
}

bool Canvas::has_network_err(QNetworkReply *r)
{
  if (r->error() != QNetworkReply::NoError) {
    qDebug() << "Network Error: " << r->errorString() << '\n'
             << "Error Type: " << r->error() << '\n'
             << "from url:" << r->url();
    return true;
  }
  return false;
}

void Canvas::authenticate()
{
  request_json(this->base_url + "/api/v1/users/self/profile", 0,
               [this](const QJsonDocument &document, const QString &) {
    emit authenticate_done(document.isObject() &&
                           is_valid_profile(document.object()));
  });
}

void Canvas::fetch_courses()
{
  fetch_json_pages(this->base_url + "/api/v1/courses?per_page=100", {},
                   [this](const QJsonDocument &document, const QString &) {
    emit fetch_courses_done(document.isArray()
                                ? to_courses(document)
                                : std::vector<Course>());
  });
}

void Canvas::fetch_folders(const Course &c)
{
  fetch_json_pages(
      this->base_url +
          QString("/api/v1/courses/%1/folders?per_page=100").arg(c.id),
      {}, [this, c](const QJsonDocument &document, const QString &) {
        emit fetch_folders_done(c, document.isArray()
                                       ? to_folders(document)
                                       : std::vector<Folder>());
      });
}

void Canvas::fetch_files(const Folder &fo)
{
  fetch_json_pages(
      this->base_url +
          QString("/api/v1/folders/%1/files?per_page=100").arg(fo.id),
      {}, [this, fo](const QJsonDocument &document, const QString &) {
        emit fetch_files_done(fo, document.isArray()
                                     ? to_files(document)
                                     : std::vector<File>());

        bool done = false;
        count_mtx.lock();
        done = ++count[FETCH_DONE] == count[FETCH_TOTAL];
        count_mtx.unlock();
        if (done) emit all_fetch_done();
      });
}

void Canvas::download(const File &file, const Folder &folder)
{
  download_file(file, folder, 0);
}

void Canvas::download_file(const File &file, const Folder &folder,
                           int retry_count)
{
  auto *reply = this->get_full(file.url.c_str());
  connect(reply, &QNetworkReply::finished, this, [=]() {
    const int status = response_status(reply);
    const bool failed = reply->error() != QNetworkReply::NoError ||
                        status >= 400;
    if (failed) {
      log_request_failure(reply, status);
      if (retry_count < MAX_RETRIES && is_retryable(reply, status)) {
        const int delay = RETRY_DELAY_MS * (1 << retry_count);
        terminate(reply);
        QTimer::singleShot(delay, this, [=]() {
          download_file(file, folder, retry_count + 1);
        });
        return;
      }
      terminate(reply);
      finish_download();
      return;
    }

    const QByteArray data = reply->readAll();
    terminate(reply);
    write_file(folder.local_dir / file.filename, data);
    finish_download();
  });
}

void Canvas::finish_download()
{
  size_t progress;
  bool done;
  count_mtx.lock();
  progress = ++count[DOWNLOAD_DONE];
  done = count[DOWNLOAD_DONE] == count[DOWNLOAD_TOTAL];
  count_mtx.unlock();

  emit download_done(progress);
  if (done) emit all_download_done();
}
