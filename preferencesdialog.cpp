#include "preferencesdialog.h"
#include "autostart.h"
#include "notemanager.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(NoteManager *manager, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("QNotes Preferences"));
    setMinimumWidth(420);

    // --- General
    auto *autostart = new QCheckBox(tr("Start QNotes when I log in"), this);
    autostart->setChecked(Autostart::isEnabled());
    autostart->setEnabled(Autostart::isSupported());
    connect(autostart, &QCheckBox::toggled, this, [this, autostart](bool on) {
        if (!Autostart::setEnabled(on)) {
            QMessageBox::warning(this, windowTitle(), tr("Could not change the login startup setting."));
            const QSignalBlocker block(autostart);
            autostart->setChecked(Autostart::isEnabled());
        }
    });
    auto *general = new QGroupBox(tr("General"), this);
    (new QVBoxLayout(general))->addWidget(autostart);

    // --- Appearance
    auto *theme = new QComboBox(this);
    theme->addItem(tr("Same as system"), int(Theme::Mode::System));
    theme->addItem(tr("Light"), int(Theme::Mode::Light));
    theme->addItem(tr("Dark"), int(Theme::Mode::Dark));
    theme->setCurrentIndex(theme->findData(int(Theme::mode())));
    connect(theme, &QComboBox::currentIndexChanged, this, [manager, theme] {
        Theme::setMode(Theme::Mode(theme->currentData().toInt()));
        manager->applyTheme();
    });
    auto *appearance = new QGroupBox(tr("Appearance"), this);
    auto *form = new QFormLayout(appearance);
    form->addRow(tr("Notes window theme:"), theme);

    // --- Backup
    auto *info = new QLabel(tr("Save all notes, including secret values, to one file protected by a password. "
                               "Importing adds the notes from a backup that aren't here yet."),
                            this);
    info->setWordWrap(true);
    auto *exportBtn = new QPushButton(tr("Export notes…"), this);
    auto *importBtn = new QPushButton(tr("Import notes…"), this);
    connect(exportBtn, &QPushButton::clicked, this, [this, manager] { manager->exportBackup(this); });
    connect(importBtn, &QPushButton::clicked, this, [this, manager] { manager->importBackup(this); });
    auto *backupButtons = new QHBoxLayout;
    backupButtons->addWidget(exportBtn);
    backupButtons->addWidget(importBtn);
    backupButtons->addStretch();
    auto *backup = new QGroupBox(tr("Backup"), this);
    auto *backupLayout = new QVBoxLayout(backup);
    backupLayout->addWidget(info);
    backupLayout->addLayout(backupButtons);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(general);
    layout->addWidget(appearance);
    layout->addWidget(backup);
    layout->addStretch();
    layout->addWidget(buttons);
}
