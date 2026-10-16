/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkAppearanceDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>

#include <KConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>

#include <KoFileDialog.h>

#include "IkInterfaceManager.h"

namespace
{
/// Small painted mock-up of a panel in the chosen skin, so the effect of the
/// tint and accent can be judged before applying.
class IkSkinPreview : public QWidget
{
public:
    explicit IkSkinPreview(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(96);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setColors(const QColor &window, const QColor &view, const QColor &button,
                   const QColor &text, const QColor &accent, const QColor &accentText)
    {
        m_window = window; m_view = view; m_button = button;
        m_text = text; m_accent = accent; m_accentText = accentText;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(m_view);
        p.drawRoundedRect(r, 10, 10);

        const QRectF panel(r.left() + 12, r.top() + 12, r.width() * 0.55, r.height() - 24);
        p.setBrush(m_window);
        p.drawRoundedRect(panel, 10, 10);

        QRectF chip(panel.left() + 10, panel.top() + 10, 70, 28);
        p.setBrush(m_accent);
        p.drawRoundedRect(chip, 8, 8);
        p.setPen(m_accentText);
        p.drawText(chip, Qt::AlignCenter, i18nc("preview of a selected item", "Active"));

        QRectF btn(chip.right() + 8, chip.top(), 70, 28);
        p.setPen(Qt::NoPen);
        p.setBrush(m_button);
        p.drawRoundedRect(btn, 8, 8);
        p.setPen(m_text);
        p.drawText(btn, Qt::AlignCenter, i18nc("preview of a button", "Button"));

        p.drawText(QRectF(panel.left() + 10, chip.bottom() + 6, panel.width() - 20, 22),
                   Qt::AlignLeft | Qt::AlignVCenter, i18n("Layer 1 · Normal · 100%"));

        // Colour dots like the app icon, to judge the tint against real colours.
        const QColor dots[] = {QColor("#F2A541"), QColor("#E2643E"), QColor("#7B6CF0"), QColor("#5BA8E8"), QColor("#5FB59A")};
        qreal x = panel.right() + 20;
        for (const QColor &c : dots) {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(QPointF(x, r.center().y()), 9, 9);
            x += 26;
        }
    }

private:
    QColor m_window, m_view, m_button, m_text, m_accent, m_accentText;
};
}

IkAppearanceDialog::IkAppearanceDialog(IkInterfaceManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle(i18n("Interface && Skins"));
    setObjectName("IkAppearanceDialog");
    QVBoxLayout *root = new QVBoxLayout(this);

    // ---- layout ---------------------------------------------------------
    QGroupBox *layoutBox = new QGroupBox(i18n("Interface layout"), this);
    QVBoxLayout *lv = new QVBoxLayout(layoutBox);
    m_layoutGroup = new QButtonGroup(this);
    for (IkInterfaceManager::Mode m : {IkInterfaceManager::Classic, IkInterfaceManager::Minimal, IkInterfaceManager::Radial}) {
        QRadioButton *rb = new QRadioButton(IkInterfaceManager::modeDisplayName(m), layoutBox);
        QLabel *desc = new QLabel(IkInterfaceManager::modeDescription(m), layoutBox);
        desc->setWordWrap(true);
        desc->setIndent(24);
        desc->setForegroundRole(QPalette::PlaceholderText);
        m_layoutGroup->addButton(rb, int(m));
        lv->addWidget(rb);
        lv->addWidget(desc);
    }
    m_followDefault = new QCheckBox(i18n("Use the default for this device (%1)",
                                         IkInterfaceManager::modeDisplayName(IkInterfaceManager::platformDefaultMode())),
                                    layoutBox);
    lv->addWidget(m_followDefault);
    root->addWidget(layoutBox);

    connect(m_followDefault, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            if (QAbstractButton *b = m_layoutGroup->button(int(IkInterfaceManager::platformDefaultMode()))) b->setChecked(true);
        }
    });
    connect(m_layoutGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int id) {
        if (id != int(IkInterfaceManager::platformDefaultMode())) m_followDefault->setChecked(false);
    });

    // ---- skin -----------------------------------------------------------
    QGroupBox *skinBox = new QGroupBox(i18n("Skin"), this);
    QFormLayout *form = new QFormLayout(skinBox);
    QHBoxLayout *skinRow = new QHBoxLayout();
    m_skin = new QComboBox(skinBox);
    m_skin->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    skinRow->addWidget(m_skin, 1);
    QPushButton *import = new QPushButton(i18n("Import..."), skinBox);
    import->setToolTip(i18n("Add a .ikskin or .colors file"));
    skinRow->addWidget(import);
    QPushButton *folder = new QPushButton(i18n("Open Folder"), skinBox);
    folder->setToolTip(i18n("Where your own skins live. Copy a bundled skin here to start a new one."));
    skinRow->addWidget(folder);
    form->addRow(i18n("Skin:"), skinRow);
    m_skinDescription = new QLabel(skinBox);
    m_skinDescription->setWordWrap(true);
    m_skinDescription->setForegroundRole(QPalette::PlaceholderText);
    form->addRow(QString(), m_skinDescription);

    QHBoxLayout *accentRow = new QHBoxLayout();
    m_overrideAccent = new QCheckBox(i18n("Custom accent"), skinBox);
    m_accentButton = new QPushButton(skinBox);
    m_accentButton->setMinimumWidth(96);
    accentRow->addWidget(m_overrideAccent);
    accentRow->addWidget(m_accentButton);
    accentRow->addStretch(1);
    form->addRow(i18n("Accent:"), accentRow);

    QHBoxLayout *tintRow = new QHBoxLayout();
    m_tintButton = new QPushButton(skinBox);
    m_tintButton->setMinimumWidth(96);
    m_tintStrength = new QSlider(Qt::Horizontal, skinBox);
    m_tintStrength->setRange(0, 100);
    m_tintStrength->setAccessibleName(i18n("Tint strength"));
    m_tintValue = new QLabel(skinBox);
    m_tintValue->setMinimumWidth(48);
    tintRow->addWidget(m_tintButton);
    tintRow->addWidget(m_tintStrength, 1);
    tintRow->addWidget(m_tintValue);
    form->addRow(i18n("Tint:"), tintRow);

    m_preview = new IkSkinPreview(skinBox);
    form->addRow(i18n("Preview:"), m_preview);
    root->addWidget(skinBox);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { slotApply(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &IkAppearanceDialog::slotApply);
    connect(import, &QPushButton::clicked, this, &IkAppearanceDialog::slotImportSkin);
    connect(folder, &QPushButton::clicked, this, &IkAppearanceDialog::slotOpenSkinsFolder);
    connect(m_skin, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &IkAppearanceDialog::slotUpdatePreview);
    connect(m_overrideAccent, &QCheckBox::toggled, this, [this](bool on) {
        m_accentButton->setEnabled(on);
        slotUpdatePreview();
    });
    connect(m_accentButton, &QPushButton::clicked, this, [this]() {
        m_accent = pickColor(m_accentButton, m_accent, i18n("Accent colour"));
        slotUpdatePreview();
    });
    connect(m_tintButton, &QPushButton::clicked, this, [this]() {
        m_tint = pickColor(m_tintButton, m_tint, i18n("Tint colour"));
        if (m_tintStrength->value() == 0) m_tintStrength->setValue(35);
        slotUpdatePreview();
    });
    connect(m_tintStrength, &QSlider::valueChanged, this, [this](int v) {
        m_tintValue->setText(v == 0 ? i18nc("tint strength", "Off") : i18n("%1%", v));
        slotUpdatePreview();
    });
}

void IkAppearanceDialog::reload()
{
    const IkSkinManager *skins = IkSkinManager::instance();
    const IkSkinManager::Settings s = skins->settings();

    if (QAbstractButton *b = m_layoutGroup->button(int(m_manager->mode()))) b->setChecked(true);
    m_followDefault->setChecked(m_manager->followsPlatformDefault());

    m_skin->blockSignals(true);
    m_skin->clear();
    const QList<IkSkin> list = skins->availableSkins();
    int current = 0;
    for (const IkSkin &k : list) {
        QString label = k.name;
        if (k.userSkin) label = i18nc("skin name, user-installed", "%1 (yours)", k.name);
        m_skin->addItem(label, k.id);
        m_skin->setItemData(m_skin->count() - 1, k.description, Qt::ToolTipRole);
        if (k.id == s.skinId) current = m_skin->count() - 1;
    }
    m_skin->setCurrentIndex(current);
    m_skin->blockSignals(false);

    m_accent = s.accent.isValid() ? s.accent : QColor(0xF2, 0xA5, 0x41);
    m_tint = s.tint.isValid() ? s.tint : QColor(0x5B, 0xA8, 0xE8);
    m_overrideAccent->setChecked(s.overrideAccent);
    m_accentButton->setEnabled(s.overrideAccent);
    setButtonColor(m_accentButton, m_accent);
    setButtonColor(m_tintButton, m_tint);
    m_tintStrength->setValue(s.tintStrength);
    m_tintValue->setText(s.tintStrength == 0 ? i18nc("tint strength", "Off") : i18n("%1%", s.tintStrength));
    slotUpdatePreview();
}

IkSkinManager::Settings IkAppearanceDialog::currentSettings() const
{
    IkSkinManager::Settings s;
    s.skinId = m_skin->currentData().toString();
    s.overrideAccent = m_overrideAccent->isChecked();
    s.accent = m_accent;
    s.tint = m_tint;
    s.tintStrength = m_tintStrength->value();
    return s;
}

void IkAppearanceDialog::slotApply()
{
    const IkInterfaceManager::Mode mode = IkInterfaceManager::Mode(qMax(0, m_layoutGroup->checkedId()));
    if (m_followDefault->isChecked()) {
        m_manager->setFollowPlatformDefault(true);
    } else {
        m_manager->setFollowPlatformDefault(false);
        m_manager->setMode(mode);
    }

    const IkSkinManager::Settings s = currentSettings();
    IkSkinManager::instance()->setSettings(s, true);
}

void IkAppearanceDialog::slotUpdatePreview()
{
    const IkSkin skin = IkSkinManager::instance()->skin(m_skin->currentData().toString());
    m_skinDescription->setText(skin.description);
    if (skin.filePath.isEmpty()) return;

    KConfig cfg(skin.filePath, KConfig::SimpleConfig);
    auto bg = [&](const char *group) { return KConfigGroup(&cfg, group).readEntry("BackgroundNormal", QColor(Qt::darkGray)); };
    const int strength = m_tintStrength->value();
    const QColor window = IkSkinManager::tinted(bg("Colors:Window"), m_tint, strength);
    const QColor view = IkSkinManager::tinted(bg("Colors:View"), m_tint, strength);
    const QColor button = IkSkinManager::tinted(bg("Colors:Button"), m_tint, strength);
    const QColor text = KConfigGroup(&cfg, "Colors:Window").readEntry("ForegroundNormal", QColor(Qt::white));
    const QColor accent = m_overrideAccent->isChecked() ? m_accent : skin.accent;
    static_cast<IkSkinPreview *>(m_preview)->setColors(window, view, button, text, accent,
                                                       IkSkinManager::readableTextOn(accent));
}

void IkAppearanceDialog::slotImportSkin()
{
    // KoFileDialog uses the platform picker, which on Android returns a
    // content:// URI rather than a file path.
    KoFileDialog dialog(this, KoFileDialog::OpenFile, "IKriateImportSkin");
    dialog.setCaption(i18n("Import Skin"));
    dialog.setNameFilter(i18n("Skins (*.ikskin *.colors)"));
    QString path = dialog.filename();
    if (path.isEmpty()) return;

    // Work on a local copy so the skin can be parsed even when it came from a
    // document provider; the copy keeps the original file name.
    QTemporaryDir tmp;
    if (path.startsWith(QLatin1String("content:")) && tmp.isValid()) {
        QString name = QUrl::fromPercentEncoding(QFileInfo(path).fileName().toUtf8()).section('/', -1);
        if (!name.endsWith(QLatin1String(".ikskin")) && !name.endsWith(QLatin1String(".colors"))) {
            name += QStringLiteral(".ikskin");
        }
        const QString local = tmp.filePath(name);
        if (!QFile::copy(path, local)) {
            QMessageBox::warning(this, i18n("Import Skin"), i18n("Could not read the selected file."));
            return;
        }
        path = local;
    }
    QString error;
    const QString id = IkSkinManager::instance()->importSkin(path, &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, i18n("Import Skin"), error);
        return;
    }
    reload();
    const int idx = m_skin->findData(id);
    if (idx >= 0) m_skin->setCurrentIndex(idx);
}

void IkAppearanceDialog::slotOpenSkinsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(IkSkinManager::instance()->userSkinsFolder()));
}

void IkAppearanceDialog::setButtonColor(QPushButton *button, const QColor &color)
{
    QPixmap pm(28, 16);
    pm.fill(color);
    button->setIcon(QIcon(pm));
    button->setText(color.name().toUpper());
}

QColor IkAppearanceDialog::pickColor(QPushButton *button, const QColor &initial, const QString &title)
{
    const QColor c = QColorDialog::getColor(initial, this, title);
    if (!c.isValid()) return initial;
    setButtonColor(button, c);
    return c;
}
