// Copyright 2016 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <QListWidgetItem>
#include "borked3ds_qt/configuration/configure_audio.h"
#include "borked3ds_qt/configuration/configure_camera.h"
#include "borked3ds_qt/configuration/configure_debug.h"
#include "borked3ds_qt/configuration/configure_dialog.h"
#include "borked3ds_qt/configuration/configure_enhancements.h"
#include "borked3ds_qt/configuration/configure_general.h"
#include "borked3ds_qt/configuration/configure_graphics.h"
#include "borked3ds_qt/configuration/configure_hotkeys.h"
#include "borked3ds_qt/configuration/configure_input.h"
#include "borked3ds_qt/configuration/configure_layout.h"
#include "borked3ds_qt/configuration/configure_online.h"
#include "borked3ds_qt/configuration/configure_storage.h"
#include "borked3ds_qt/configuration/configure_system.h"
#include "borked3ds_qt/configuration/configure_ui.h"
#include "borked3ds_qt/hotkeys.h"
#include "common/settings.h"
#include "core/core.h"
#include "ui_configure.h"
#ifdef CONFIG_SCROLLABLE //gvx64 - configuration menu implement scrollbar functionality (experimental) if specified as cmake flag
#include <QScrollArea>
#endif

#ifdef CONFIG_SCROLLABLE
// gvx64: Helper function to wrap config tabs with scroll support
static QScrollArea* MakeScrollable(QWidget* inner_widget) {
    auto* scroll = new QScrollArea;
    scroll->setWidget(inner_widget);
    scroll->setWidgetResizable(true);
    scroll->setFrameStyle(QFrame::NoFrame);
    return scroll;
}
#endif

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_, Core::System& system_,
                                 QString gl_renderer, std::span<const QString> physical_devices,
                                 bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry{registry_},
      system{system_}, is_powered_on{system.IsPoweredOn()},
      general_tab{std::make_unique<ConfigureGeneral>(is_powered_on, this)},
      system_tab{std::make_unique<ConfigureSystem>(system, this)},
      input_tab{std::make_unique<ConfigureInput>(system, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(this)},
      graphics_tab{
          std::make_unique<ConfigureGraphics>(gl_renderer, physical_devices, is_powered_on, this)},
      enhancements_tab{std::make_unique<ConfigureEnhancements>(this)},
      layout_tab{std::make_unique<ConfigureLayout>(this)},
      audio_tab{std::make_unique<ConfigureAudio>(is_powered_on, this)},
      camera_tab{std::make_unique<ConfigureCamera>(this)},
      debug_tab{std::make_unique<ConfigureDebug>(is_powered_on, this)},
      storage_tab{std::make_unique<ConfigureStorage>(is_powered_on, this)},
      online_tab{std::make_unique<ConfigureOnline>(this)},
      ui_tab{std::make_unique<ConfigureUi>(this)} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);

#ifdef CONFIG_SCROLLABLE
    ui->tabWidget->addTab(MakeScrollable(general_tab.get()), tr("General")); //gvx64 - enable experimental scroll bar in config menus
    ui->tabWidget->addTab(MakeScrollable(system_tab.get()), tr("System"));
    ui->tabWidget->addTab(MakeScrollable(input_tab.get()), tr("Input"));
    ui->tabWidget->addTab(MakeScrollable(hotkeys_tab.get()), tr("Hotkeys"));
    ui->tabWidget->addTab(MakeScrollable(graphics_tab.get()), tr("Graphics"));
    ui->tabWidget->addTab(MakeScrollable(enhancements_tab.get()), tr("Enhancements"));
    ui->tabWidget->addTab(MakeScrollable(layout_tab.get()), tr("Layout"));
    ui->tabWidget->addTab(MakeScrollable(audio_tab.get()), tr("Audio"));
    ui->tabWidget->addTab(MakeScrollable(camera_tab.get()), tr("Camera"));
    ui->tabWidget->addTab(MakeScrollable(debug_tab.get()), tr("Debug"));
    ui->tabWidget->addTab(MakeScrollable(storage_tab.get()), tr("Storage"));
    ui->tabWidget->addTab(MakeScrollable(online_tab.get()), tr("Online"));
    ui->tabWidget->addTab(MakeScrollable(ui_tab.get()), tr("UI"));
#else
    ui->tabWidget->addTab(general_tab.get(), tr("General"));
    ui->tabWidget->addTab(system_tab.get(), tr("System"));
    ui->tabWidget->addTab(input_tab.get(), tr("Input"));
    ui->tabWidget->addTab(hotkeys_tab.get(), tr("Hotkeys"));
    ui->tabWidget->addTab(graphics_tab.get(), tr("Graphics"));
    ui->tabWidget->addTab(enhancements_tab.get(), tr("Enhancements"));
    ui->tabWidget->addTab(layout_tab.get(), tr("Layout"));
    ui->tabWidget->addTab(audio_tab.get(), tr("Audio"));
    ui->tabWidget->addTab(camera_tab.get(), tr("Camera"));
    ui->tabWidget->addTab(debug_tab.get(), tr("Debug"));
    ui->tabWidget->addTab(storage_tab.get(), tr("Storage"));
    ui->tabWidget->addTab(online_tab.get(), tr("Online"));
    ui->tabWidget->addTab(ui_tab.get(), tr("UI"));
#endif
    hotkeys_tab->Populate(registry);

    PopulateSelectionList();

    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(ui->selectorList, &QListWidget::itemSelectionChanged, this,
            &ConfigureDialog::UpdateVisibleTabs);

    adjustSize();
    ui->selectorList->setCurrentRow(0);

    // Set up used key list synchronisation
    connect(input_tab.get(), &ConfigureInput::InputKeysChanged, hotkeys_tab.get(),
            &ConfigureHotkeys::OnInputKeysChanged);
    connect(hotkeys_tab.get(), &ConfigureHotkeys::HotkeysChanged, input_tab.get(),
            &ConfigureInput::OnHotkeysChanged);

    // Synchronise lists upon initialisation
    input_tab->EmitInputKeysChanged();
    hotkeys_tab->EmitHotkeysChanged();
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::SetConfiguration() {
    general_tab->SetConfiguration();
    system_tab->SetConfiguration();
    input_tab->LoadConfiguration();
    graphics_tab->SetConfiguration();
    enhancements_tab->SetConfiguration();
    layout_tab->SetConfiguration();
    audio_tab->SetConfiguration();
    camera_tab->SetConfiguration();
    debug_tab->SetConfiguration();
    online_tab->SetConfiguration();
    ui_tab->SetConfiguration();
    storage_tab->SetConfiguration();
}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    input_tab->ApplyProfile();
    hotkeys_tab->ApplyConfiguration(registry);
    graphics_tab->ApplyConfiguration();
    enhancements_tab->ApplyConfiguration();
    layout_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    camera_tab->ApplyConfiguration();
    debug_tab->ApplyConfiguration();
    online_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    storage_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

Q_DECLARE_METATYPE(QList<QWidget*>);

void ConfigureDialog::PopulateSelectionList() {
    ui->selectorList->clear();

    const std::array<std::pair<QString, QList<QWidget*>>, 5> items{
        {{tr("General"), {general_tab.get(), online_tab.get(), debug_tab.get(), ui_tab.get()}},
         {tr("System"), {system_tab.get(), camera_tab.get(), storage_tab.get()}},
         {tr("Graphics"), {enhancements_tab.get(), layout_tab.get(), graphics_tab.get()}},
         {tr("Audio"), {audio_tab.get()}},
         {tr("Controls"), {input_tab.get(), hotkeys_tab.get()}}}};

    for (const auto& entry : items) {
        auto* const item = new QListWidgetItem(entry.first);
        item->setData(Qt::UserRole, QVariant::fromValue(entry.second));

        ui->selectorList->addItem(item);
    }
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    // first apply the configuration, and then restore the display
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}

void ConfigureDialog::RetranslateUI() {
    int old_row = ui->selectorList->currentRow();
    int old_index = ui->tabWidget->currentIndex();
    ui->retranslateUi(this);
    PopulateSelectionList();
    // restore selection after repopulating
    ui->selectorList->setCurrentRow(old_row);
    ui->tabWidget->setCurrentIndex(old_index);

    general_tab->RetranslateUI();
    system_tab->RetranslateUI();
    input_tab->RetranslateUI();
    hotkeys_tab->RetranslateUI();
    graphics_tab->RetranslateUI();
    enhancements_tab->RetranslateUI();
    layout_tab->RetranslateUI();
    audio_tab->RetranslateUI();
    camera_tab->RetranslateUI();
    debug_tab->RetranslateUI();
    online_tab->RetranslateUI();
    ui_tab->RetranslateUI();
    storage_tab->RetranslateUI();
}

void ConfigureDialog::UpdateVisibleTabs() {
    const auto items = ui->selectorList->selectedItems();
    if (items.isEmpty())
        return;

    const std::map<QWidget*, QString> widgets = {{general_tab.get(), tr("General")},
                                                 {system_tab.get(), tr("System")},
                                                 {input_tab.get(), tr("Input")},
                                                 {hotkeys_tab.get(), tr("Hotkeys")},
                                                 {enhancements_tab.get(), tr("Enhancements")},
                                                 {layout_tab.get(), tr("Layout")},
                                                 {graphics_tab.get(), tr("Advanced")},
                                                 {audio_tab.get(), tr("Audio")},
                                                 {camera_tab.get(), tr("Camera")},
                                                 {debug_tab.get(), tr("Debug")},
                                                 {storage_tab.get(), tr("Storage")},
                                                 {online_tab.get(), tr("Online")},
                                                 {ui_tab.get(), tr("UI")}};

    ui->tabWidget->clear();

    const QList<QWidget*> tabs = qvariant_cast<QList<QWidget*>>(items[0]->data(Qt::UserRole));

    for (const auto tab : tabs)
        ui->tabWidget->addTab(tab, widgets.at(tab));
}
