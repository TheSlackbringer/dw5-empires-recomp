#include "wizard_ui.h"
#include "install_state.h"
#include "install_location.h"
#include "xdvdfs.h"

#include <chrono>
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
    Preparation(const WizardConfig &source,const InstallFolderSelection &folder):config(source),selection(folder) {
        worker=std::thread([this] {
            try { ok=prepareWizardSelection(config,selection); }
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
    ImGui::StyleColorsDark();

    // Dark sleek theme
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.38f, 0.58f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.48f, 0.72f, 1.0f);
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
    auto existingDialog = std::make_shared<DialogState>();
    char destinationBuf[32768] = {};
    std::snprintf(destinationBuf, sizeof(destinationBuf), "%s", pathText(destination.destination).c_str());
    enum class Page { Saves, Disc, Existing, Other, Manage, Copied };
    const auto hasDisc = [&] { return installState(destination.destination) == InstallState::Installed; };
    Page page = config.manageOnly ? Page::Manage
              : !config.importSource.empty() ? Page::Saves
              : hasDisc() ? Page::Existing
              : !config.discovered.empty() || !destination.error.empty() ? Page::Other
              : config.modeChosen || config.launcherDir.empty() ? Page::Disc : Page::Saves;
    Page returnFromOptions = page;
    const auto openOptions = [&] {
        returnFromOptions = page;
        page = Page::Other;
    };
    std::string actionError;
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
        std::snprintf(destinationBuf, sizeof(destinationBuf), "%s", pathText(destination.destination).c_str());
        return true;
    };
    std::unique_ptr<SetupRun> activeRun;
    std::unique_ptr<Preparation> preparation;
    bool closeAfterPreparation=false;
    const auto beginPreparation=[&] { preparation=std::make_unique<Preparation>(config,destination);destination.frozen=true; };
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
                if (!activeRun && !preparation && selectDestination(pathFromText(folderDialog->selectedFile))) {
                    if (hasDisc()) {
                        config.modeChosen = false;
                        config.importSource.clear();
                        page = Page::Existing;
                    } else if (page == Page::Other) {
                        page = Page::Saves;
                    }
                }
                folderDialog->hasResult = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(existingDialog->mutex);
            if (existingDialog->hasResult) {
                if (!activeRun && !preparation) {
                    const auto existing = existingInstallationRoot(pathFromText(existingDialog->selectedFile));
                    if (existing.empty())
                        actionError = "Choose a folder containing a verified DW5E installation.";
                    else if (selectDestination(existing)) {
                        config.modeChosen = false;
                        config.importSource.clear();
                        config.conflict = SaveConflict::Ask;
                        page = config.manageOnly ? Page::Manage : Page::Existing;
                    }
                }
                existingDialog->hasResult = false;
            }
        }
        if (preparation && preparation->done.load(std::memory_order_acquire)) {
            const bool ok = preparation->ok;
            config = std::move(preparation->config);
            destination = std::move(preparation->selection);
            preparation.reset();
            if (closeAfterPreparation)
                quit = true;
            else if (!ok) {
                actionError = std::move(destination.error);
                destination.error.clear();
            } else if (config.manageOnly) {
                succeeded = true;
                if (copyFromManagement)
                    page = Page::Copied;
                else
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
            ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Dynasty Warriors 5: Empires (Xbox 360)");
            ImGui::TextDisabled("Native PC Port via Static Recompilation");
            ImGui::Separator();
            ImGui::Spacing();

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
            const auto browseExisting = [&] {
                SDL_ShowOpenFolderDialog(onFolderDialog, new std::shared_ptr<DialogState>(existingDialog),
                                         window, nullptr, false);
            };
            const auto browseDestination = [&] {
                SDL_ShowOpenFolderDialog(onFolderDialog, new std::shared_ptr<DialogState>(folderDialog),
                                         window, destinationBuf, false);
            };
            const auto exitButton = [&] {
                ImGui::SameLine();
                if (ImGui::Button("Exit", ImVec2(100, 36)))
                    quit = true;
            };

            if (!config.layoutError.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
                ImGui::TextWrapped("Setup Error: %s", config.layoutError.c_str());
                ImGui::PopStyleColor();
                if (ImGui::Button("Exit", ImVec2(100, 36)))
                    quit = true;
            }
            else if(preparation) {
                ImGui::TextWrapped("Preparing the installation and verifying file copies...");
                ImGui::TextWrapped("Original saves are retained. The destination is locked until this finishes.");
                if(closeAfterPreparation) ImGui::TextWrapped("Closing after the current safe copy finishes.");
            }
            else if (!activeRun && page == Page::Copied) {
                ImGui::TextUnformatted("Installation copied");
                ImGui::TextWrapped("%s", pathText(destination.destination).c_str());
                ImGui::TextWrapped("Open this launcher to play from the new copy. Your original installation is still in place.");
                if (ImGui::Button("Done", ImVec2(100, 36)))
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
                    chooseMode(current.mode == SaveMode::Portable ? SaveMode::Portable : SaveMode::Normal);
                    chooseConflict();
                    ImGui::TextWrapped("Close the game before switching. Your original saves are kept.");
                } else if (loaded) {
                    ImGui::TextWrapped("Choose an installed copy of DW5E to manage its saves.");
                }
                showError(destination.error);
                showError(metadataError);
                showError(actionError);
                ImGui::BeginDisabled(!canManage || !config.modeChosen || config.desiredMode == current.mode);
                if (ImGui::Button("Save change", ImVec2(160, 36)))
                    beginPreparation();
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(returnToExisting ? "Back" : "Close", ImVec2(100, 36))) {
                    if (returnToExisting) {
                        config.manageOnly = false;
                        config.modeChosen = false;
                        config.conflict = SaveConflict::Ask;
                        actionError.clear();
                        returnToExisting = false;
                        page = Page::Existing;
                    } else {
                        quit = true;
                    }
                }
                const auto copyDestination = config.launcherDir / "game-files";
                if (canManage && !destination.explicitOverride && !config.launcherDir.empty() &&
                    destination.destination != copyDestination &&
                    installState(copyDestination) != InstallState::Installed) {
                    ImGui::Separator();
                    ImGui::TextWrapped("Taking DW5E to another drive or PC? Make a second copy of this installation.");
                    if (ImGui::Button(copyLabel)) {
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
            }
            else if (!activeRun && page == Page::Saves) {
                ImGui::TextDisabled(config.importSource.empty() ? "Step 1 of 2" : "Copy installation");
                ImGui::TextUnformatted(copyFromManagement ? "Where should the copy keep its saves?"
                                                         : "Where should saves and settings be stored?");
                chooseMode(SaveMode::Normal);
                if (copyFromManagement)
                    ImGui::TextWrapped("Choose Portable if you want saves and settings to travel with the copy.");
                showError(actionError);
                ImGui::Spacing();
                if (ImGui::Button("Continue", ImVec2(160, 36))) {
                    config.modeChosen = true;
                    page = config.importSource.empty() ? Page::Disc : Page::Existing;
                }
                if (copyFromManagement) {
                    ImGui::SameLine();
                    if (ImGui::Button("Back", ImVec2(100, 36)) && selectDestination(config.importSource)) {
                        config.importSource.clear();
                        config.modeChosen = false;
                        config.conflict = SaveConflict::Ask;
                        copyFromManagement = false;
                        page = Page::Manage;
                    }
                } else if (!destination.explicitOverride && config.importSource.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Other options", ImVec2(120, 36)))
                        openOptions();
                }
                exitButton();
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
                    ImGui::TextWrapped("%s", pathText(destination.destination).c_str());
                    ImGui::TextWrapped("Update if needed and play from this folder. No ISO needed.");
                }
                showError(destination.error);
                showError(actionError);
                ImGui::Spacing();
                ImGui::BeginDisabled(!destination.error.empty());
                const char *primary = copyFromManagement ? "Copy installation"
                                      : importing ? "Copy & Play" : "Use this installation";
                if (ImGui::Button(primary, ImVec2(180, 36))) {
                    config.base.discIso.clear();
                    beginPreparation();
                }
                ImGui::EndDisabled();
                if (importing) {
                    ImGui::SameLine();
                    if (ImGui::Button("Back", ImVec2(100, 36)))
                        page = Page::Saves;
                } else if (!destination.explicitOverride) {
                    ImGui::SameLine();
                    if (ImGui::Button("Other options", ImVec2(120, 36)))
                        openOptions();
                }
                exitButton();
                if (!importing && ImGui::SmallButton("Manage installation")) {
                    config.manageOnly = true;
                    returnToExisting = true;
                    actionError.clear();
                    page = Page::Manage;
                }
            }
            else if (!activeRun && page == Page::Other) {
                ImGui::TextUnformatted(config.manageOnly ? "Choose an installation to manage" : "Installation options");
                showError(destination.error);
                showError(actionError);
                ImGui::BeginDisabled(destination.explicitOverride);
                for (size_t i = 0; i < config.discovered.size(); ++i) {
                    if (config.discovered[i] == destination.destination && returnFromOptions == Page::Existing)
                        continue;
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TextWrapped("%s", pathText(config.discovered[i]).c_str());
                    if (ImGui::SmallButton("Use this installation") && selectDestination(config.discovered[i])) {
                        config.importSource.clear();
                        config.modeChosen = false;
                        config.conflict = SaveConflict::Ask;
                        page = config.manageOnly ? Page::Manage : Page::Existing;
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Browse for existing installation..."))
                    browseExisting();
                if (!config.manageOnly) {
                    const auto defaultFolder = config.launcherDir / "game-files";
                    if (ImGui::Button("Start a new installation")) {
                        config.importSource.clear();
                        config.modeChosen = false;
                        config.conflict = SaveConflict::Ask;
                        if (installState(defaultFolder) == InstallState::Installed) {
                            actionError = "Choose a separate folder for a new installation.";
                            browseDestination();
                        } else if (selectDestination(defaultFolder)) {
                            page = Page::Saves;
                        }
                    }
                }
                ImGui::EndDisabled();
                ImGui::Spacing();
                if (returnFromOptions != Page::Other) {
                    if (ImGui::Button("Back", ImVec2(100, 36)))
                        page = returnFromOptions;
                    exitButton();
                } else if (ImGui::Button("Exit", ImVec2(100, 36))) {
                    quit = true;
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
                ImGui::TextUnformatted("Select your disc image");
                ImGui::TextWrapped("Choose your legally owned European PAL disc image (.iso).");
                ImGui::Spacing();

                ImGui::Text("Disc Image (.iso):");
                ImGui::SetNextItemWidth(-80);
                ImGui::InputText("##IsoPath", isoPathBuf, sizeof(isoPathBuf));
                ImGui::SameLine();
                if (ImGui::Button("Browse..."))
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
                    ImGui::TextWrapped("Install to: %s", destinationBuf);
                    if (!destination.explicitOverride && ImGui::SmallButton("Change folder..."))
                        browseDestination();
                }
                showError(destination.error);
                showError(actionError);
                ImGui::Spacing();

                const bool canStart = isoValid && currentIso == isoPathBuf && destination.error.empty() &&
                                      !destination.destination.empty() && !hasDisc();
                ImGui::BeginDisabled(!canStart);
                if (ImGui::Button("Install & Play", ImVec2(160, 36))) {
                    config.base.discIso = pathFromText(validatedIsoPath);
                    beginPreparation();
                }
                ImGui::EndDisabled();

                if (!config.launcherDir.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Back", ImVec2(100, 36)))
                        page = Page::Saves;
                }
                exitButton();
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
                    if (ImGui::Button("Cancel", ImVec2(120, 32)))
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
                    if (ImGui::Button("Back", ImVec2(120, 32)))
                    {
                        activeRun.reset();
                        if (hasDisc())
                            page = Page::Existing;
                    }
                }
                else if (st.cancelled)
                {
                    ImGui::TextWrapped("Setup cancelled. You can retry with your installed files or choose a disc image.");
                    if (ImGui::Button("Back", ImVec2(120, 32))) {
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
