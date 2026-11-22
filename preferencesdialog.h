#pragma once

#include <QDialog>

class NoteManager;

// Preferences: start on login, light / dark theme, backup export / import.
class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(NoteManager *manager, QWidget *parent = nullptr);
};
