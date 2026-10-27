#include "wizard_ui.h"
#include "install_state.h"
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

int runWizardConsole(const SetupOptions &options)
{
    std::cout << "====================================================\n";
    std::cout << " Dynasty Warriors 5: Empires - Setup Wizard\n";
    std::cout << "====================================================\n";
    std::cout << "Data directory: " << options.effectiveDataDir().string() << "\n";
    if (!options.discIso.empty())
        std::cout << "Disc image:     " << options.discIso.string() << "\n";
    std::cout << "----------------------------------------------------\n";

    if (options.discIso.empty())
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
                std::cout << "\nSUCCESS: Game built and ready at " << status.executable.string() << std::endl;
                return 0;
            }
        }
    }
}

bool runWizardWindow(const WizardConfig &config)
{
    if (config.noWindow)
        return runWizardConsole(config.base) == 0;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "SDL_Init failed (" << SDL_GetError() << "), falling back to console setup." << std::endl;
        return runWizardConsole(config.base) == 0;
    }

    const int kWindowW = 680;
    const int kWindowH = 460;
    SDL_Window *window = SDL_CreateWindow("Dynasty Warriors 5: Empires - Setup",
                                          kWindowW, kWindowH,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed (" << SDL_GetError() << "), falling back to console setup." << std::endl;
        SDL_Quit();
        return runWizardConsole(config.base) == 0;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        std::cerr << "SDL_CreateRenderer failed (" << SDL_GetError() << "), falling back to console setup." << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return runWizardConsole(config.base) == 0;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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
        std::string s = config.prefilledIso.string();
        std::snprintf(isoPathBuf, sizeof(isoPathBuf), "%s", s.c_str());
    }

    DialogState dialogState;
    std::unique_ptr<SetupRun> activeRun;
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
                if (activeRun)
                    activeRun->cancel();
                quit = true;
            }
            else if (event.type == SDL_EVENT_DROP_FILE)
            {
                if (event.drop.data)
                {
                    std::snprintf(isoPathBuf, sizeof(isoPathBuf), "%s", event.drop.data);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(dialogState.mutex);
            if (dialogState.hasResult)
            {
                std::snprintf(isoPathBuf, sizeof(isoPathBuf), "%s", dialogState.selectedFile.c_str());
                dialogState.hasResult = false;
            }
        }

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

            if (!config.layoutError.empty())
            {
                ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.30f, 1.0f), "Setup Error: %s", config.layoutError.c_str());
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
                        fs::path p(currentIso);
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
                else if (config.discInstalled && validatedIsoPath.empty())
                {
                    canStart = true;
                }

                if (!canStart)
                    ImGui::BeginDisabled();

                if (ImGui::Button("Install & Play", ImVec2(160, 36)))
                {
                    SetupOptions opts = config.base;
                    if (isoValid)
                        opts.discIso = fs::path(validatedIsoPath);
                    activeRun = std::make_unique<SetupRun>(opts);
                    activeRun->start();
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
                    ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "Failed: %s", st.error.c_str());
                    ImGui::Spacing();
                    if (ImGui::Button("Back", ImVec2(120, 32)))
                    {
                        activeRun.reset();
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
