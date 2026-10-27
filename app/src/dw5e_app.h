// dw5e - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

void Dw5eRegisterSnapshotBind();  // src/dw5e_snapshot.cpp

class Dw5eApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Dw5eApp>(new Dw5eApp(ctx, "dw5e",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  void OnPostSetup() override { Dw5eRegisterSnapshotBind(); }
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      auto exe = rex::filesystem::GetExecutableFolder();
      auto candidate = exe.parent_path() / "game" / "files";
      if (std::filesystem::exists(candidate)) {
        paths.game_data_root = candidate;
      }
    }
    if (paths.user_data_root.empty()) {
      auto exe = rex::filesystem::GetExecutableFolder();
      paths.user_data_root = exe.parent_path();
    }
  }
};
