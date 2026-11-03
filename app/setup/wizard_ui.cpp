#include "wizard_ui.h"
#include "install_state.h"
#include "install_location.h"
#include "xdvdfs.h"

#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

namespace dw5e::setup {

namespace fs = std::filesystem;

struct DialogState
{
    std::mutex mutex;
    std::string selectedFile;
    bool hasResult = false;
};

struct Preparation {
    WizardConfig config;
    InstallFolderSelection selection;
    std::atomic<bool> done{false};
    bool ok=false;
    std::thread worker;
    Preparation(const WizardConfig &source, const InstallFolderSelection &folder, bool uninstall)
        : config(source), selection(folder) {
        worker=std::thread([this, uninstall] {
            try {
                ok = uninstall
                    ? uninstallGameFiles(selection.destination, selection.launcher,
                                         !selection.explicitOverride, selection.error)
                    : prepareWizardSelection(config, selection);
            }
            catch(const std::exception &error) { selection.error=error.what(); }
            done.store(true,std::memory_order_release);
        });
    }
    ~Preparation() { if(worker.joinable()) worker.join(); }
};

static void SDLCALL onFileDialogCallback(void *userdata, const char * const *filelist, int filter)
{
    (void)filter;
    auto *state = static_cast<DialogState *>(userdata);
    if (filelist && *filelist && **filelist)
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->selectedFile = *filelist;
        state->hasResult = true;
    }
    else if (!filelist)
    {
#ifndef _WIN32
        std::thread([state]() {
            for (const char *cmd : {
                "kdialog --getopenfilename . '*.iso *.ISO|ISO Disc Images (*.iso)\n*|All Files' 2>/dev/null",
                "zenity --file-selection --file-filter='ISO Disc Images (*.iso) | *.iso *.ISO' --file-filter='All Files | *' 2>/dev/null"
            }) {
                FILE *pipe = popen(cmd, "r");
                if (pipe) {
                    char buf[1024] = {0};
                    if (fgets(buf, sizeof(buf), pipe)) {
                        std::string path(buf);
                        while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
                            path.pop_back();
                        pclose(pipe);
                        if (!path.empty()) {
                            std::lock_guard<std::mutex> lock(state->mutex);
                            state->selectedFile = path;
                            state->hasResult = true;
                            return;
                        }
                    } else {
                        pclose(pipe);
                    }
                }
            }
        }).detach();
#endif
    }
}

static void SDLCALL onFolderDialog(void *userdata, const char * const *files, int)
{
    std::unique_ptr<std::shared_ptr<DialogState>> owner(static_cast<std::shared_ptr<DialogState> *>(userdata));
    if(files && *files && **files) {
        std::lock_guard<std::mutex> lock((*owner)->mutex);
        (*owner)->selectedFile=*files;
        (*owner)->hasResult=true;
    }
}

int runWizardConsole(const SetupOptions &options)
{
    std::cout << "====================================================\n";
    std::cout << " Dynasty Warriors 5: Empires - Setup Wizard\n";
    std::cout << "====================================================\n";
    std::cout << "Data directory: " << pathText(options.effectiveDataDir()) << "\n";
    if (!options.discIso.empty())
        std::cout << "Disc image:     " << pathText(options.discIso) << "\n";
    std::cout << "----------------------------------------------------\n";

    if (options.discIso.empty() &&
        installState(options.effectiveDataDir()) != InstallState::Installed)
    {
        std::cerr << "\nERROR: No disc image specified.\n"
                  << "To run in console mode: pass --iso /path/to/dw5e.iso\n"
                  << "Otherwise, launch from a desktop session with display available.\n";
        return 1;
    }

    SetupRun run(options);
    run.start();

    Step lastStep = Step::Done;
    int lastPct = -1;

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SetupStatus status = run.status();

        int pct = static_cast<int>(status.overall * 100.0);
        if (status.step != lastStep || pct != lastPct)
        {
            lastStep = status.step;
            lastPct = pct;
            std::cout << "[" << stepName(status.step) << "] (" << pct << "%) " << status.detail << std::endl;
        }

        if (!status.running)
        {
            if (status.failed)
            {
                std::cerr << "\nERROR: " << status.error << std::endl;
                return 1;
            }
            if (status.cancelled)
            {
                std::cout << "\nSetup cancelled by user." << std::endl;
                return 2;
            }
            if (status.finished)
            {
                std::cout << "\nSUCCESS: Game built and ready at " << pathText(status.executable) << std::endl;
                return 0;
            }
        }
    }
}

bool runWizardWindow(WizardConfig &config)
{
    InstallFolderSelection destination{config.launcherDir, config.base.effectiveDataDir(),
                                       config.explicitDestination, false, config.destinationError};
    const auto console = [&] {
        if (!config.layoutError.empty()) {
            std::cerr << "Setup error: " << config.layoutError << std::endl; return false;
        }
        if(!prepareWizardSelection(config,destination)) {
            std::cerr<<"Setup destination/save-location error: "<<destination.error<<std::endl; return false;
        }
        if(config.manageOnly) { std::cout<<"SUCCESS: save location updated without rebuilding the game."<<std::endl;return true; }
        return runWizardConsole(config.base) == 0;
    };
    if (config.noWindow) return console();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "SDL_Init failed (" << SDL_GetError() << "), falling back to console setup." << std::endl;
        return console();
    }

    const int kWindowW = 680;
    const int kWindowH = 440;
    SDL_Window *window = SDL_CreateWindow("Dynasty Warriors 5: Empires - Setup",
                                          kWindowW, kWindowH,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed (" << SDL_GetError() << "), falling back to console setup." << std::endl;
        SDL_Quit();
        return console();
    }

    SDL_SetWindowMinimumSize(window, 360, 280);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        std::cerr << "SDL_CreateRenderer failed (" << SDL_GetError() << "), falling back to console setup." << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return console();
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // The wizard has no persistent window layout outside the installation.
    const auto fontPath = config.base.root / "rexglue-sdk/thirdparty/imgui/misc/fonts/Roboto-Medium.ttf";
    std::ifstream fontFile(fontPath, std::ios::binary | std::ios::ate);
    const auto fontSize = fontFile.tellg();
    if (fontSize > 0 && fontSize <= 1024 * 1024) {
        const auto bytes = static_cast<int>(fontSize);
        if (void *fontData = ImGui::MemAlloc(bytes)) {
            fontFile.seekg(0);
            if (fontFile.read(static_cast<char *>(fontData), bytes))
                io.FontDefault = io.Fonts->AddFontFromMemoryTTF(fontData, bytes, 16.0f);
            else
                ImGui::MemFree(fontData);
        }
    }
    ImGui::StyleColorsDark();

    // Shared installer chrome, spacing and button hierarchy.
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.WindowPadding = ImVec2(20, 14);
    style.ItemSpacing = ImVec2(9, 9);
    style.FramePadding = ImVec2(12, 7);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.19f, 0.22f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.30f, 0.37f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.25f, 0.65f, 0.45f, 1.0f);

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    char isoPathBuf[1024] = {0};
    if (!config.prefilledIso.empty())
    {
        std::string s = pathText(config.prefilledIso);
        std::snprintf(isoPathBuf, sizeof(isoPathBuf), "%s", s.c_str());
    }

    DialogState dialogState;
    auto folderDialog = std::make_shared<DialogState>();
    char folderInput[32768] = {};
    std::string folderError;
    std::string checkedFolder;
    fs::path folderChoice;
    bool createFolder = false;
    enum class Page { Saves, Disc, Existing, Other, Manage, Uninstall, Uninstalled, Copied, Saved, PreparationFailed };
    const auto hasDisc = [&] { return installState(destination.destination) == InstallState::Installed; };
    Installation initialInstallation;
    std::string initialMetadataError;
    const bool savedMode = !destination.destination.empty() &&
        loadInstallation(destination.destination, initialInstallation, initialMetadataError) && initialInstallation.recorded;
    Page page = config.manageOnly ? Page::Manage
              : !config.importSource.empty() ? Page::Saves
              : hasDisc() ? Page::Existing
              : !config.discovered.empty() || !destination.error.empty() ? Page::Other
              : savedMode || config.modeChosen || config.launcherDir.empty() ? Page::Disc : Page::Saves;
    Page returnFromOptions = page;
    const auto openOptions = [&] {
        folderDialog = std::make_shared<DialogState>();
        folderError.clear();
        checkedFolder.clear();
        folderChoice.clear();
        createFolder = false;
        const auto path = page == Page::Saves ? std::string{} : pathText(destination.destination);
        std::snprintf(folderInput, sizeof(folderInput), "%s", path.c_str());
        returnFromOptions = page;
        page = Page::Other;
    };
    std::string actionError;
    fs::path preparationLog;
    Page pageBeforeFailure = page;
    std::string saveConfirmation;
    bool returnToExisting = false;
    bool copyFromManagement = false;
#ifdef _WIN32
    const char *copyLabel = "Copy installation beside this launcher";
#else
    const char *copyLabel = "Copy installation beside this AppImage";
#endif
    const auto selectDestination = [&](const fs::path &path) {
        if (!destination.choose(path))
            return false;
        actionError.clear();
        saveConfirmation.clear();
        return true;
    };
    std::unique_ptr<SetupRun> activeRun;
    std::unique_ptr<Preparation> preparation;
    bool closeAfterPreparation=false;
    const auto beginPreparation = [&](bool uninstall = false) {
        preparation = std::make_unique<Preparation>(config, destination, uninstall);
        destination.frozen = true;
    };
    bool quit = false;
    bool succeeded = false;

    std::string validatedIsoPath = "__uninit__";
    bool isoValid = false;
    std::string isoError;

    while (!quit)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
            {
                if (activeRun) activeRun->cancel();
                if(preparation) closeAfterPreparation=true;
                else quit = true;
            }
            else if (event.type == SDL_EVENT_DROP_FILE && !activeRun && !preparation)
            {
                if (event.drop.data)
                {
                    std::snprintf(isoPathBuf, sizeof(isoPathBuf), "%s", event.drop.data);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(dialogState.mutex);
            if (dialogState.hasResult && !activeRun && !preparation)
            {
                std::snprintf(isoPathBuf, sizeof(isoPathBuf), "%s", dialogState.selectedFile.c_str());
                dialogState.hasResult = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(folderDialog->mutex);
            if (folderDialog->hasResult) {
                if (page == Page::Other && !activeRun && !preparation) {
                    std::snprintf(folderInput, sizeof(folderInput), "%s", folderDialog->selectedFile.c_str());
                    checkedFolder.clear();
                }
                folderDialog->hasResult = false;
            }
        }

        if (preparation && preparation->done.load(std::memory_order_acquire)) {
            const bool ok = preparation->ok;
            config = std::move(preparation->config);
            destination = std::move(preparation->selection);
            preparation.reset();
            if (ok && page == Page::Uninstall) {
                config.manageOnly = true;
                succeeded = true;
                page = Page::Uninstalled;
                quit = closeAfterPreparation;
            } else if (closeAfterPreparation)
                quit = true;
            else if (!ok) {
                actionError = std::move(destination.error);
                destination.error.clear();
                if (actionError.empty())
                    actionError = "Setup could not complete this action.";
                std::cerr << "Setup: " << actionError << std::endl;
                const auto root = config.importSource.empty() ? destination.destination : config.importSource;
                preparationLog.clear();
                if (!root.empty()) {
                    const auto logPath = root / "logs" / "installation-management.log";
                    std::ofstream log(logPath, std::ios::app);
                    if (log << actionError << '\n' << std::flush)
                        preparationLog = logPath;
                }
                pageBeforeFailure = page;
                page = Page::PreparationFailed;
            } else if (config.manageOnly) {
                if (copyFromManagement) {
                    succeeded = true;
                    page = Page::Copied;
                } else {
                    saveConfirmation = config.desiredMode == SaveMode::Portable
                        ? "Save location updated. Saves and settings now stay with the game."
                        : "Save location updated. Saves and settings now use your user profile.";
                    config.modeChosen = false;
                    config.conflict = SaveConflict::Ask;
                    actionError.clear();
                    if (returnToExisting) {
                        config.manageOnly = false;
                        returnToExisting = false;
                        page = Page::Existing;
                    } else {
                        succeeded = true;
                        page = Page::Saved;
                    }
                }
            } else if (config.importSource.empty() && config.base.discIso.empty() &&
                       wizardBuildIsCurrent(config.base, destination.destination, config.releaseId)) {
                config.launchExistingBuild = true;
                succeeded = true;
                quit = true;
            } else {
                config.importSource.clear();
                actionError.clear();
                activeRun = std::make_unique<SetupRun>(config.base);
                activeRun->start();
            }
        }
        destination.frozen=bool(activeRun)||bool(preparation);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w), static_cast<float>(h)));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

        if (ImGui::Begin("SetupWindow", nullptr, flags))
        {
            constexpr float buttonHeight = 36.0f;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float footerHeight = buttonHeight + 2 * ImGui::GetStyle().ItemSpacing.y + 1;
            const float bodyHeight = ImGui::GetContentRegionAvail().y - footerHeight;
            const bool preparing = bool(preparation);
            ImGui::PushID(activeRun ? -2 : static_cast<int>(page));
            ImGui::BeginChild("PageBody", ImVec2(0, bodyHeight));
            if (preparing && closeAfterPreparation)
                ImGui::TextWrapped("Closing when the current operation finishes.");
            ImGui::BeginDisabled(preparing);

            struct FooterResult { bool back = false; bool primary = false; };
            bool footerDrawn = false;
            const auto footer = [&](const char *back = nullptr, const char *primary = nullptr,
                                    bool primaryEnabled = true, bool destructive = false) {
                ImGui::EndDisabled();
                ImGui::EndChild();
                footerDrawn = true;
                ImGui::Separator();
                FooterResult result;
                const float available = ImGui::GetContentRegionAvail().x;
                const bool paired = back && primary;
                const float width = std::min(180.0f, paired ? (available - gap) / 2 : available);
                const float total = paired ? 2 * width + gap : width;
                if (total > 0 && available > total)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - total);
                if (back) {
                    ImGui::BeginDisabled(preparing);
                    result.back = ImGui::Button(back, ImVec2(width, buttonHeight));
                    ImGui::EndDisabled();
                }
                if (primary) {
                    if (back)
                        ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, destructive
                        ? ImVec4(0.62f, 0.21f, 0.24f, 1.0f) : ImVec4(0.22f, 0.43f, 0.66f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, destructive
                        ? ImVec4(0.74f, 0.26f, 0.30f, 1.0f) : ImVec4(0.28f, 0.52f, 0.76f, 1.0f));
                    ImGui::BeginDisabled(preparing || !primaryEnabled);
                    result.primary = ImGui::Button(primary, ImVec2(width, buttonHeight));
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor(2);
                }
                return result;
            };
            const auto link = [](const char *label) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.80f, 1.0f, 1.0f));
                const bool pressed = ImGui::Button(label);
                const ImVec2 first = ImGui::GetItemRectMin();
                const ImVec2 last = ImGui::GetItemRectMax();
                const ImVec2 padding = ImGui::GetStyle().FramePadding;
                const float underline = last.y - padding.y;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(first.x + padding.x, underline),
                                                   ImVec2(last.x - padding.x, underline),
                                                   ImGui::GetColorU32(ImGuiCol_Text));
                ImGui::PopStyleColor(2);
                return pressed;
            };

            const auto showError = [](const std::string &error) {
                if (error.empty())
                    return;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
                ImGui::TextWrapped("%s", error.c_str());
                ImGui::PopStyleColor();
            };
            const auto chooseMode = [&](SaveMode initial) {
                const SaveMode desired = config.modeChosen ? config.desiredMode : initial;
                if (ImGui::RadioButton("On this PC (recommended)", desired == SaveMode::Normal)) {
                    config.desiredMode = SaveMode::Normal;
                    config.modeChosen = true;
                    actionError.clear();
                    config.conflict = SaveConflict::Ask;
                }
                if (ImGui::RadioButton("With the game (portable)", desired == SaveMode::Portable)) {
                    config.desiredMode = SaveMode::Portable;
                    config.modeChosen = true;
                    actionError.clear();
                    config.conflict = SaveConflict::Ask;
                }
                Installation preview;
                preview.root = destination.destination;
                configureSaveMode(preview, config.modeChosen ? config.desiredMode : initial);
                ImGui::TextWrapped("Saves: %s", pathText(preview.userData).c_str());
                if (preview.configDir != preview.userData)
                    ImGui::TextWrapped("Settings: %s", pathText(preview.configDir).c_str());
            };
            const auto chooseConflict = [&] {
                if (actionError.find("conflict") == std::string::npos &&
                    config.conflict == SaveConflict::Ask)
                    return;
                ImGui::TextWrapped("Both locations contain progress. Choose which files to use.");
                if (ImGui::RadioButton("Use current saves and back up destination saves",
                                       config.conflict == SaveConflict::UseSource))
                    config.conflict = SaveConflict::UseSource;
                if (ImGui::RadioButton("Keep destination saves",
                                       config.conflict == SaveConflict::KeepDestination))
                    config.conflict = SaveConflict::KeepDestination;
            };
            const auto browseFolder = [&] {
                folderDialog = std::make_shared<DialogState>();
                SDL_ShowOpenFolderDialog(onFolderDialog, new std::shared_ptr<DialogState>(folderDialog),
                                         window, nullptr, false);
            };
            const auto folderButton = [&](const char *id, const char *tooltip) {
                const bool pressed = ImGui::Button(id, ImVec2(buttonHeight, buttonHeight));
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("%s", tooltip);
                const auto origin = ImGui::GetItemRectMin();
                const float x = origin.x + 9.0f, y = origin.y + 11.0f;
                const ImVec2 outline[] = {
                    {x, y + 14}, {x, y}, {x + 7, y}, {x + 10, y + 3},
                    {x + 18, y + 3}, {x + 18, y + 14}
                };
                ImGui::GetWindowDrawList()->AddPolyline(outline, 6,
                    ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_Closed, 1.5f);
                return pressed;
            };
            if (!config.layoutError.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
                ImGui::TextWrapped("Setup Error: %s", config.layoutError.c_str());
                ImGui::PopStyleColor();
            }
            else if (!activeRun && page == Page::PreparationFailed) {
                ImGui::TextUnformatted(pageBeforeFailure == Page::Manage
                    ? "Could not save the new location" : "Setup could not continue");
                showError(actionError);
                if (!preparationLog.empty())
                    ImGui::TextWrapped("Details saved to %s", pathText(preparationLog).c_str());
                ImGui::TextWrapped("Go back to review your choice and try again.");
                if (footer("Back").back)
                    page = pageBeforeFailure;
            }
            else if (!activeRun && page == Page::Uninstall) {
                ImGui::TextUnformatted("Uninstall game files?");
                ImGui::TextWrapped("%s", pathText(destination.destination).c_str());
                ImGui::TextWrapped("This removes the extracted game files, generated code and compiled build.");
                ImGui::TextWrapped("The launcher, your saves and settings will stay. You will need the ISO to reinstall this copy.");
                const auto actions = footer("Back", preparing ? "Removing..." : "Uninstall game files",
                                            destination.error.empty(), true);
                if (actions.back)
                    page = Page::Manage;
                if (actions.primary)
                    beginPreparation(true);
            }
            else if (!activeRun && page == Page::Uninstalled) {
                ImGui::TextUnformatted("Game files removed");
                ImGui::TextWrapped("Your saves and settings are still in place. Open the launcher again to reinstall from an ISO.");
                if (footer(nullptr, "Done").primary)
                    quit = true;
            }
            else if (!activeRun && (page == Page::Copied || page == Page::Saved)) {
                if (page == Page::Copied) {
                    ImGui::TextUnformatted("Installation copied");
                    ImGui::TextWrapped("%s", pathText(destination.destination).c_str());
                    ImGui::TextWrapped("Open this launcher to play from the new copy. Your original installation is still in place.");
                } else {
                    ImGui::TextWrapped("%s", saveConfirmation.c_str());
                    ImGui::TextWrapped("Open the launcher when you are ready to update or play.");
                }
                if (footer(nullptr, "Done").primary)
                    quit = true;
            }
            else if (!activeRun && page == Page::Manage) {
                ImGui::TextUnformatted("Manage installation");
                ImGui::TextWrapped("%s", pathText(destination.destination).c_str());
                Installation current;
                std::string metadataError;
                const bool loaded = destination.error.empty() &&
                                    loadInstallation(destination.destination, current, metadataError);
                const bool canManage = loaded && hasDisc();
                if (canManage) {
                    ImGui::TextWrapped("Current saves: %s", pathText(current.userData).c_str());
                    chooseMode(current.mode);
                    chooseConflict();
                    ImGui::TextWrapped("Close the game before switching. Your original saves are kept.");
                } else if (loaded) {
                    ImGui::TextWrapped("Choose an installed copy of DW5E to manage its saves.");
                }
                showError(destination.error);
                showError(metadataError);
                showError(actionError);
                const auto copyDestination = config.launcherDir / "game-files";
                if (canManage && !destination.explicitOverride && !config.launcherDir.empty() &&
                    destination.destination != copyDestination &&
                    installState(copyDestination) != InstallState::Installed) {
                    ImGui::Separator();
                    ImGui::TextWrapped("Taking DW5E to another drive or PC? Make a second copy of this installation.");
                    if (ImGui::Button(copyLabel, ImVec2(0, buttonHeight))) {
                        const auto source = destination.destination;
                        if (selectDestination(copyDestination)) {
                            config.importSource = source;
                            config.modeChosen = false;
                            config.conflict = SaveConflict::Ask;
                            copyFromManagement = true;
                            page = Page::Saves;
                        }
                    }
                }
                if (!canManage && !destination.explicitOverride &&
                    ImGui::SmallButton("Choose installation..."))
                    openOptions();
                if (canUninstallGameFiles(destination.destination)) {
                    ImGui::Separator();
                    if (link("Uninstall game files..."))
                        page = Page::Uninstall;
                }
                const auto actions = footer(returnToExisting ? "Back" : nullptr,
                                            preparing ? "Saving..." : "Save change",
                                            canManage && config.modeChosen && config.desiredMode != current.mode);
                if (actions.back) {
                    config.manageOnly = false;
                    config.modeChosen = false;
                    config.conflict = SaveConflict::Ask;
                    actionError.clear();
                    returnToExisting = false;
                    page = Page::Existing;
                }
                if (actions.primary)
                    beginPreparation();
            }
            else if (!activeRun && page == Page::Saves) {
                ImGui::TextDisabled(config.importSource.empty() ? "Step 1 of 2" : "Copy installation");
                ImGui::TextUnformatted(copyFromManagement ? "Where should the copy keep its saves?"
                                                         : "Where should saves and settings be stored?");
                Installation selectedMode;
                std::string modeError;
                const auto defaultMode = config.importSource.empty() &&
                    loadInstallation(destination.destination, selectedMode, modeError)
                    ? selectedMode.mode : SaveMode::Normal;
                chooseMode(defaultMode);
                if (copyFromManagement)
                    ImGui::TextWrapped("Choose Portable if you want saves and settings to travel with the copy.");
                showError(actionError);
                ImGui::Spacing();
                if (!destination.explicitOverride && config.importSource.empty() &&
                    link("Find an existing installation..."))
                    openOptions();
                const auto actions = footer(copyFromManagement ? "Back" : nullptr,
                                            "Continue");
                if (actions.back) {
                    if (copyFromManagement && selectDestination(config.importSource)) {
                        config.importSource.clear();
                        config.modeChosen = false;
                        config.conflict = SaveConflict::Ask;
                        copyFromManagement = false;
                        page = Page::Manage;
                    }
                }
                if (actions.primary) {
                    page = config.importSource.empty() ? Page::Disc : Page::Existing;
                }
            }
            else if (!activeRun && page == Page::Existing) {
                const bool importing = !config.importSource.empty();
                ImGui::TextUnformatted(importing ? copyLabel : "We found your DW5E installation");
                if (importing) {
                    ImGui::TextWrapped("From: %s", pathText(config.importSource).c_str());
                    ImGui::TextWrapped("To: %s", pathText(destination.destination).c_str());
                    ImGui::TextWrapped("This creates a second copy. Your current installation stays where it is. No ISO needed.");
                    chooseConflict();
                } else {
                    if (!saveConfirmation.empty())
                        ImGui::TextWrapped("%s", saveConfirmation.c_str());
                    ImGui::TextWrapped("Update if needed and play from this folder. No ISO needed.");
                    const auto path = pathText(destination.destination);
                    const ImVec2 padding(12, 9);
                    const bool canChangeFolder = !destination.explicitOverride;
                    const float folderButtonWidth = 36.0f;
                    const float reserved = canChangeFolder ? folderButtonWidth + gap : 0.0f;
                    const float wrapWidth = ImGui::GetContentRegionAvail().x - 2 * padding.x - reserved - 2;
                    const float contentHeight = ImGui::GetTextLineHeightWithSpacing() +
                        ImGui::CalcTextSize(path.c_str(), nullptr, false, wrapWidth).y;
                    const float panelHeight = std::max(contentHeight, canChangeFolder ? folderButtonWidth : 0.0f)
                        + 2 * padding.y + 2;
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.17f, 0.21f, 1.0f));
                    ImGui::BeginChild("DetectedFolder", ImVec2(0, panelHeight),
                                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
                    const float folderButtonX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - folderButtonWidth;
                    ImGui::BeginGroup();
                    ImGui::TextDisabled("Installation folder");
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
                    ImGui::TextUnformatted(path.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    if (canChangeFolder) {
                        ImGui::SameLine(folderButtonX);
                        if (folderButton("##ChangeInstallationFolder", "Change installation folder"))
                            openOptions();
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar(2);
                }
                showError(destination.error);
                showError(actionError);
                ImGui::Spacing();
                if (!importing) {
                    if (ImGui::Button("Manage installation", ImVec2(0, buttonHeight))) {
                        config.manageOnly = true;
                        returnToExisting = true;
                        actionError.clear();
                        page = Page::Manage;
                    }
                }
                const char *primary = copyFromManagement ? "Copy installation" : importing ? "Copy & Play" :
                    wizardBuildIsCurrent(config.base, destination.destination, config.releaseId)
                        ? "Play" : "Update & Play";
                if (preparing)
                    primary = importing ? "Copying..." : "Preparing...";
                const auto actions = footer(importing ? "Back" : nullptr, primary, destination.error.empty());
                if (actions.back)
                    page = Page::Saves;
                if (actions.primary) {
                    config.base.discIso.clear();
                    beginPreparation();
                }
            }
            else if (!activeRun && page == Page::Other) {
                const bool newDestination = returnFromOptions == Page::Disc;
                ImGui::TextUnformatted(newDestination ? "Change installation folder" : "Choose an installed DW5E folder");
                ImGui::TextWrapped("%s", newDestination
                    ? "Choose where DW5E should install its game files."
                    : "Enter or browse to a folder where DW5E is already installed.");
                showError(destination.error);
                ImGui::BeginDisabled(destination.explicitOverride);
                ImGui::TextDisabled("Installation folder");
                ImGui::SetNextItemWidth(-buttonHeight - gap);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, (buttonHeight - ImGui::GetFontSize()) / 2));
                ImGui::InputText("##InstallationFolderPath", folderInput, sizeof(folderInput));
                ImGui::PopStyleVar();
                ImGui::SameLine();
                if (folderButton("##BrowseInstallationFolder", "Browse for a folder"))
                    browseFolder();

                const auto checkFolder = [&] {
                    checkedFolder = folderInput;
                    folderChoice.clear();
                    folderError.clear();
                    createFolder = false;
                    if (checkedFolder.empty())
                        return;
                    auto path = pathFromText(checkedFolder);
                    if (!path.is_absolute())
                        path = config.launcherDir / path;
                    std::error_code ec;
                    const auto status = fs::status(path, ec);
                    if (ec && ec != std::errc::no_such_file_or_directory) {
                        folderError = "Cannot read this folder: " + pathText(path);
                    } else if (!fs::exists(status)) {
                        if (newDestination) {
                            createFolder = true;
                            folderChoice = path;
                        } else {
                            folderError = "This folder does not exist: " + pathText(path);
                        }
                    } else if (!fs::is_directory(status)) {
                        folderError = "This path is a file. Choose a folder.";
                    } else if (auto existing = existingInstallationRoot(path); !existing.empty()) {
                        folderChoice = existing;
                    } else if (newDestination && installState(path) != InstallState::Installed) {
                        folderChoice = path;
                    } else {
                        folderError = "This folder exists, but no verified DW5E installation was found.";
                    }
                };
                if (checkedFolder != folderInput)
                    checkFolder();
                showError(folderError);
                if (createFolder)
                    ImGui::TextWrapped("This folder does not exist yet. DW5E will create it when you start installation.");

                const auto chooseExisting = [&](const fs::path &existing) {
                    if (!selectDestination(existing))
                        return;
                    config.importSource.clear();
                    config.modeChosen = false;
                    config.conflict = SaveConflict::Ask;
                    page = config.manageOnly ? Page::Manage : Page::Existing;
                };
                if (!newDestination) {
                    for (size_t i = 0; i < config.discovered.size(); ++i) {
                        if (config.discovered[i] == destination.destination && returnFromOptions == Page::Existing)
                            continue;
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::TextWrapped("%s", pathText(config.discovered[i]).c_str());
                        if (ImGui::Button("Use this installation", ImVec2(0, buttonHeight))) {
                            const auto existing = existingInstallationRoot(config.discovered[i]);
                            if (!existing.empty())
                                chooseExisting(existing);
                            else
                                folderError = "This installation is no longer available.";
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndDisabled();
                const auto actions = footer(returnFromOptions != Page::Other ? "Back" : nullptr,
                                            createFolder ? "Use new folder" : "Use this folder",
                                            !destination.explicitOverride && !folderChoice.empty());
                if (actions.back)
                    page = returnFromOptions;
                if (actions.primary) {
                    const bool alreadyAcknowledged = createFolder;
                    checkFolder();
                    if (!folderChoice.empty() && (!createFolder || alreadyAcknowledged)) {
                        if (installState(folderChoice) == InstallState::Installed)
                            chooseExisting(folderChoice);
                        else if (newDestination && selectDestination(folderChoice))
                            page = Page::Disc;
                    }
                }
            }
            else if (!activeRun)
            {
                const std::string currentIso = isoPathBuf;
                if (currentIso != validatedIsoPath)
                {
                    validatedIsoPath = currentIso;
                    isoError.clear();
                    isoValid = false;
                    if (!currentIso.empty())
                    {
                        std::error_code ec;
                        fs::path p = pathFromText(currentIso);
                        if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec))
                        {
                            isoError = "File not found: " + currentIso;
                        }
                        else
                        {
                            uint64_t partBase = 0;
                            std::string err;
                            if (!checkDiscImage(p, partBase, err))
                            {
                                isoError = err;
                            }
                            else
                            {
                                isoValid = true;
                            }
                        }
                    }
                }

                if (!config.launcherDir.empty())
                    ImGui::TextDisabled("Step 2 of 2");
                ImGui::TextUnformatted("Install from ISO");
                ImGui::TextWrapped("Choose your legally owned European PAL disc image (.iso).");
                ImGui::Spacing();

                ImGui::Text("Disc Image (.iso):");
                const float browseWidth = ImGui::CalcTextSize("Browse...").x +
                    2 * ImGui::GetStyle().FramePadding.x;
                ImGui::SetNextItemWidth(-browseWidth - gap);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, (buttonHeight - ImGui::GetFontSize()) / 2));
                ImGui::InputText("##IsoPath", isoPathBuf, sizeof(isoPathBuf));
                ImGui::PopStyleVar();
                ImGui::SameLine();
                if (ImGui::Button("Browse...", ImVec2(browseWidth, buttonHeight)))
                {
#ifdef _WIN32
                    OPENFILENAMEW ofn = {};
                    wchar_t szFile[MAX_PATH] = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = L"Disc Image (*.iso)\0*.iso\0All Files (*.*)\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                    if (GetOpenFileNameW(&ofn))
                    {
                        int len = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, nullptr, 0, nullptr, nullptr);
                        if (len > 0)
                        {
                            std::string u8(len, 0);
                            WideCharToMultiByte(CP_UTF8, 0, szFile, -1, u8.data(), len, nullptr, nullptr);
                            while (!u8.empty() && u8.back() == '\0') u8.pop_back();
                            std::snprintf(isoPathBuf, sizeof(isoPathBuf), "%s", u8.c_str());
                        }
                    }
#else
                    static const SDL_DialogFileFilter filters[] = {
                        {"Disc Images (*.iso)", "iso"},
                        {"All Files (*)", "*"}
                    };
                    SDL_ShowOpenFileDialog(onFileDialogCallback, &dialogState, window, filters, 2, nullptr, false);
#endif
                }

                if (!validatedIsoPath.empty())
                {
                    if (!isoValid)
                    {
                        ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "Error: %s", isoError.c_str());
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.45f, 1.0f), "Valid Dynasty Warriors 5: Empires disc image detected.");
                    }
                }
                else
                {
                    ImGui::TextDisabled("(Drag and drop dw5e.iso onto this window)");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextDisabled("Estimated time on first launch: ~2 to 4 minutes.");
                ImGui::Spacing();

                if (!config.launcherDir.empty() || destination.explicitOverride) {
                    ImGui::TextWrapped("Install to: %s", pathText(destination.destination).c_str());
                    if (!destination.explicitOverride &&
                        ImGui::Button("Change folder...", ImVec2(0, buttonHeight)))
                        openOptions();
                }
                showError(destination.error);
                showError(actionError);
                ImGui::Spacing();

                const bool canStart = isoValid && currentIso == isoPathBuf && destination.error.empty() &&
                                      !destination.destination.empty() && !hasDisc();
                const auto actions = footer(config.launcherDir.empty() ? nullptr : "Back",
                                            preparing ? "Preparing..." : "Install & Play", canStart);
                if (actions.back)
                    page = Page::Saves;
                if (actions.primary) {
                    config.base.discIso = pathFromText(validatedIsoPath);
                    beginPreparation();
                }
            }
            else
            {
                // Active Run Progress
                SetupStatus st = activeRun->status();

                ImGui::Text("Phase: %s", stepName(st.step));
                ImGui::TextWrapped("Current step: %s", st.detail.c_str());
                ImGui::Spacing();

                float progressVal = static_cast<float>(st.overall);
                char overlay[64];
                std::snprintf(overlay, sizeof(overlay), "%.0f%%", progressVal * 100.0f);
                ImGui::ProgressBar(progressVal, ImVec2(-1, 26), overlay);

                ImGui::Spacing();

                if (st.running)
                {
                    if (footer("Cancel").back)
                    {
                        activeRun->cancel();
                    }
                }
                else if (st.failed)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.25f, 0.25f, 1.0f));
                    ImGui::TextWrapped("Failed: %s", st.error.c_str());
                    ImGui::PopStyleColor();
                    ImGui::Spacing();
                    if (footer("Back").back)
                    {
                        activeRun.reset();
                        if (hasDisc())
                            page = Page::Existing;
                    }
                }
                else if (st.cancelled)
                {
                    ImGui::TextWrapped("Setup cancelled. You can retry with your installed files or choose a disc image.");
                    if (footer("Back").back) {
                        activeRun.reset();
                        if (hasDisc())
                            page = Page::Existing;
                    }
                }
                else if (st.finished)
                {
                    ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.45f, 1.0f), "Build finished successfully! Starting game...");
                    succeeded = true;
                    if (config.closeWhenFinished)
                    {
                        quit = true;
                    }
                }
            }
            if (!footerDrawn)
                footer();
            ImGui::PopID();
        }
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 25, 28, 33, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return succeeded;
}

} // namespace dw5e::setup
