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
    const int kWindowH = 640;
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
                if (!activeRun && !preparation && destination.choose(pathFromText(folderDialog->selectedFile)))
                    std::snprintf(destinationBuf, sizeof(destinationBuf), "%s", pathText(destination.destination).c_str());
                folderDialog->hasResult=false;
            }
        }
        {
            std::lock_guard<std::mutex> lock(existingDialog->mutex);
            if(existingDialog->hasResult) {
                if(!activeRun && !preparation) {
                    const auto existing=existingInstallationRoot(pathFromText(existingDialog->selectedFile));
                    if(existing.empty()) destination.error="Choose a folder containing a verified DW5E installation.";
                    else { destination.choose(existing);config.modeChosen=false;config.importSource.clear();
                        std::snprintf(destinationBuf,sizeof(destinationBuf),"%s",pathText(existing).c_str()); }
                }
                existingDialog->hasResult=false;
            }
        }
        if(preparation && preparation->done.load(std::memory_order_acquire)) {
            const bool ok=preparation->ok;
            config=std::move(preparation->config);
            destination=std::move(preparation->selection);
            preparation.reset();
            if(closeAfterPreparation) quit=true;
            else if(ok) {
                if(config.manageOnly) {succeeded=true;quit=true;}
                else {activeRun=std::make_unique<SetupRun>(config.base);activeRun->start();}
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

            if (!config.launcherDir.empty() || config.explicitDestination) {
                ImGui::TextUnformatted("Installation folder:");
                ImGui::BeginDisabled(destination.frozen || destination.explicitOverride);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##InstallFolder", destinationBuf, sizeof(destinationBuf))) {
                    if (*destinationBuf) destination.choose(pathFromText(destinationBuf));
                    else destination.error="Choose an installation folder before continuing.";
                }
                if (ImGui::Button("Use game-files beside launcher")) {
                    destination.choose(config.launcherDir / "game-files");
                    std::snprintf(destinationBuf,sizeof(destinationBuf),"%s",pathText(destination.destination).c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("Choose folder..."))
                    SDL_ShowOpenFolderDialog(onFolderDialog,new std::shared_ptr<DialogState>(folderDialog),
                                             window,destinationBuf,false);
                ImGui::EndDisabled();
                if(destination.explicitOverride)
                    ImGui::TextWrapped("This run uses an explicit folder override. The remembered choice is unchanged.");
                ImGui::TextWrapped("Destination: %s",pathText(destination.destination).c_str());
                if(!destination.error.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.95f,0.30f,0.30f,1.0f));
                    ImGui::TextWrapped("%s",destination.error.c_str()); ImGui::PopStyleColor();
                }
                ImGui::Separator();
            }

            if(!activeRun && !preparation && (!config.launcherDir.empty() || config.explicitDestination)) {
                for(const auto &candidate:config.discovered) {
                    const auto label="Use "+pathText(candidate);
                    if(ImGui::Button(label.c_str())) {
                        destination.choose(candidate);config.modeChosen=false;config.importSource.clear();
                        std::snprintf(destinationBuf,sizeof(destinationBuf),"%s",pathText(candidate).c_str());
                    }
                }
                if(ImGui::Button("Use existing installation..."))
                    SDL_ShowOpenFolderDialog(onFolderDialog,new std::shared_ptr<DialogState>(existingDialog),window,nullptr,false);
                Installation selected;std::string selectedError;
                if(loadInstallation(destination.destination,selected,selectedError)) {
                    Installation preview=selected;
                    const bool choosing=!selected.recorded && installState(selected.root)!=InstallState::Installed;
                    if(choosing || config.manageOnly) {
                        ImGui::TextUnformatted("Where should saves and settings be stored?");
                        const auto desired=config.modeChosen?config.desiredMode:selected.mode;
                        if(ImGui::RadioButton("On this PC (recommended)",desired==SaveMode::Normal)) {
                            config.desiredMode=SaveMode::Normal;config.modeChosen=true;destination.error.clear();
                        }
                        if(ImGui::RadioButton("With the game (portable)",desired==SaveMode::Portable)) {
                            config.desiredMode=SaveMode::Portable;config.modeChosen=true;destination.error.clear();
                        }
                        if(config.modeChosen) configureSaveMode(preview,config.desiredMode);
                    } else {
                        ImGui::Text("Save location: %s",selected.mode==SaveMode::Normal?"On this PC":selected.mode==SaveMode::Portable?"With the game (portable)":"Existing legacy installation");
                        if(ImGui::Button("Manage save location")) config.manageOnly=true;
                    }
                    ImGui::TextWrapped("Saves: %s",pathText(preview.userData).c_str());
                    ImGui::TextWrapped("Settings: %s",pathText(preview.configDir).c_str());
                    if(config.manageOnly) {
                        ImGui::TextWrapped("Close the game before switching. Original saves are kept as a backup.");
                        if(destination.error.find("conflict")!=std::string::npos || config.conflict!=SaveConflict::Ask) {
                            ImGui::TextWrapped("Both locations contain progress. Choose which files to use:");
                            if(ImGui::RadioButton("Use current saves (back up conflicting destination files)",config.conflict==SaveConflict::UseSource)) {config.conflict=SaveConflict::UseSource;destination.error.clear();}
                            if(ImGui::RadioButton("Keep destination saves",config.conflict==SaveConflict::KeepDestination)) {config.conflict=SaveConflict::KeepDestination;destination.error.clear();}
                        }
                    }
                    if(selected.mode==SaveMode::Legacy && selected.root!=config.launcherDir/"game-files") {
                        if(ImGui::Button("Import into game-files (keep original installation)")) {
                            config.importSource=selected.root;destination.choose(config.launcherDir/"game-files");
                            config.modeChosen=true;config.desiredMode=SaveMode::Normal;config.manageOnly=false;
                            std::snprintf(destinationBuf,sizeof(destinationBuf),"%s",pathText(destination.destination).c_str());
                        }
                    }
                } else if(destination.error.empty()) destination.error=selectedError;
                ImGui::Separator();
            }

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
            else if(config.manageOnly && !activeRun) {
                if(ImGui::Button("Save change",ImVec2(160,36))) {
                    beginPreparation();
                }
                ImGui::SameLine();if(ImGui::Button("Close",ImVec2(100,36))) quit=true;
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

                ImGui::TextWrapped("To play, select your legally owned retail European PAL disc image (.iso). "
                                   "The wizard will extract game assets, translate the PowerPC code, and compile the native executable.");
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

                bool canStart = false;
                if (isoValid)
                {
                    canStart = true;
                }
                else if (validatedIsoPath.empty() &&
                         installState(destination.destination) == InstallState::Installed)
                {
                    canStart = true;
                }

                canStart = canStart && destination.error.empty() && !destination.destination.empty();
                if (!canStart)
                    ImGui::BeginDisabled();

                if (ImGui::Button("Install & Play", ImVec2(160, 36)))
                {
                    if(isoValid) config.base.discIso=pathFromText(validatedIsoPath);
                    beginPreparation();
                }

                if (!canStart)
                    ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::Button("Exit", ImVec2(100, 36)))
                    quit = true;
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
                    }
                }
                else if (st.cancelled)
                {
                    ImGui::TextWrapped("Setup cancelled. You can retry with your installed files or choose a disc image.");
                    if (ImGui::Button("Back", ImVec2(120, 32)))
                        activeRun.reset();
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
