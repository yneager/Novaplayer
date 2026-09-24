#pragma once

// An addon's own configuration page (…/configure), shown in a separate
// window. The page is untrusted remote content: it runs in an off-the-record
// WebEngine profile with no WebChannel, so it cannot reach LAMBDA's own UI.
// Configuration pages end with an "Install" link to stremio://…/manifest.json
// (or the https manifest URL); following it installs that configured URL,
// the same deep link Stremio handles (stremio-core AddonDetails maps
// stremio:// to https://).

#include <QWidget>

class QLabel;
class QLineEdit;
class QWebEngineProfile;
class QWebEngineView;

class AddonConfigureWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit AddonConfigureWindow(QWidget *parent = nullptr);

    void openUrl(const QString &url);

    // Whether a navigation target is an addon install link.
    static bool isInstallLink(const QUrl &url);

signals:
    void installRequested(const QString &manifestUrl);

private:
    QWebEngineProfile *profile_ = nullptr;
    QWebEngineView *view_ = nullptr;
    QLineEdit *address_ = nullptr;
    QLabel *hint_ = nullptr;
};
